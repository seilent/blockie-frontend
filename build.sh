#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cache=${BLOCKIE_FE_CACHE:-$HOME/.cache/blockie-frontend}
out=${BLOCKIE_FE_OUT:-$cache/out}
jobs=${BLOCKIE_FE_JOBS:-$(nproc)}
cc=${CC:-cc}
cxx=${CXX:-g++}
appcflags=${BLOCKIE_FE_CFLAGS:-}
appldflags=${BLOCKIE_FE_LDFLAGS:-}
if [[ -n ${BLOCKIE_FE_DEP_HARDEN+x} ]]; then
    dep_harden=$BLOCKIE_FE_DEP_HARDEN
elif [[ -n $appcflags ]]; then
    dep_harden="-fPIE -D_FORTIFY_SOURCE=3 -fstack-protector-strong"
else
    dep_harden=
fi
fe_commit=${BLOCKIE_FE_COMMIT:-}
[[ $fe_commit =~ ^[0-9a-f]*$ ]] || { echo "build.sh: BLOCKIE_FE_COMMIT '$fe_commit' is not a hex commit" >&2; exit 2; }
destdir=${DESTDIR:-}
prefix=${PREFIX:-/usr}
target=all
for a in "$@"; do
    case $a in
        DESTDIR=*) destdir=${a#DESTDIR=} ;;
        PREFIX=*) prefix=${a#PREFIX=} ;;
        all | generic | app | scan | test | test-dbus | test-tsan | test-input | test-pad | test-portmsg | \
            test-powermenu | scraper | install | shot) target=$a ;;
        *) echo "build.sh: arg $a, want a target (all generic app scan test test-dbus test-tsan test-input test-pad" \
            "test-portmsg test-powermenu scraper install shot) or DESTDIR=/PREFIX=" >&2
            exit 2 ;;
    esac
done

stb_url=https://raw.githubusercontent.com/nothings/stb/6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760/stb_truetype.h
stb_sha=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
spng_ver=0.7.4
spng_url=https://github.com/randy408/libspng/archive/refs/tags/v${spng_ver}.tar.gz
spng_sha=47ec02be6c0a6323044600a9221b049f63e1953faf816903e7383d4dc4234487
zlib_ver=1.3.1
zlib_url=https://github.com/madler/zlib/releases/download/v${zlib_ver}/zlib-${zlib_ver}.tar.gz
zlib_sha=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
qr_rev=720f62bddb7226106071d4728c292cb1df519ceb
qr_url=https://raw.githubusercontent.com/nayuki/QR-Code-generator/$qr_rev/c
qr_c_sha=300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403
qr_h_sha=e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589
fb_ver=1.0.16
fb_url=https://github.com/fribidi/fribidi/releases/download/v${fb_ver}/fribidi-${fb_ver}.tar.xz
fb_sha=1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c
hb_ver=14.3.0
hb_url=https://github.com/harfbuzz/harfbuzz/releases/download/${hb_ver}/harfbuzz-${hb_ver}.tar.xz
hb_sha=16070d77cfc4ba1f1e7327e83bf9b3f55898081cabdb94e56a33e04fc8874eae
xdg_url=https://gitlab.freedesktop.org/wayland/wayland-protocols/-/raw/1.41/stable/xdg-shell/xdg-shell.xml
xdg_sha=f241ab95c262eb45af6507bf0178b8dcd88d5e1cc8fc6ec4a9944d9e5b15ac98
wlr_rev=1106a4a1c57562ddd438c547da4c0d6dc150d624
wlr_url=https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/$wlr_rev/unstable/wlr-layer-shell-unstable-v1.xml
wlr_sha=87e0b9c837aecd6977f76f3c47d73088b7159871f5d979dc1840f6cadb5e2ed8
pt_url=https://gitlab.freedesktop.org/wayland/wayland-protocols/-/raw/1.41/stable/presentation-time/presentation-time.xml
pt_sha=dffac93bcb2bb1d8c385e72b8a8c2c0d4d79a336866322f3ba886dce2b27b1e2
wl_proto=(xdg-shell wlr-layer-shell-unstable-v1 presentation-time)
sqlite_ver=3540100
sqlite_year=2026
sqlite_url=https://www.sqlite.org/$sqlite_year/sqlite-amalgamation-$sqlite_ver.zip
sqlite_sha=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
font_url=https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip
font_sha=3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423

spng_dir=$cache/src/libspng-$spng_ver
zlib_dir=$cache/src/zlib-$zlib_ver
qr_dir=$cache/src/qrcodegen-${qr_rev:0:8}
fb_dir=$cache/src/fribidi-$fb_ver
hb_dir=$cache/src/harfbuzz-$hb_ver
sqlite_dir=$cache/src/sqlite-amalgamation-$sqlite_ver
gen=$out/gen
depobj=$out/deps/obj
genobj=$out/generic/obj
appobj=$out/app/obj
scanobj=$out/scan/obj
scraperobj=$out/scraper/obj
launchobj=$out/launch/obj
testobj=$out/test/obj
shotobj=$out/shot/obj
imgobj=$out/image/obj
tsanobj=$out/tsan/obj
fonts_dir=$out/fonts
wl_cflags=
wl_libs=

fetch() {
    local url=$1 dst=$2 sha=$3
    if [[ -f $dst ]] && echo "$sha  $dst" | sha256sum -c --status; then
        return
    fi
    curl -sSfL --retry 4 --retry-all-errors -o "$dst.part" "$url" ||
        curl -sSfL --http1.1 --retry 4 --retry-all-errors -o "$dst.part" "$url" ||
        { echo "build.sh: download failed for $url" >&2; exit 1; }
    echo "$sha  $dst.part" | sha256sum -c --status || { echo "build.sh: sha256 mismatch for $url" >&2; exit 1; }
    mv "$dst.part" "$dst"
}

unpack() {
    local url=$1 sha=$2 dir=$3 name
    name=$(basename "$dir").tar.gz
    fetch "$url" "$cache/$name" "$sha"
    if [[ ! -d $dir ]]; then
        rm -rf "$dir.part"
        mkdir -p "$dir.part"
        tar -C "$dir.part" --strip-components=1 -xzf "$cache/$name"
        mv "$dir.part" "$dir"
    fi
}

deps() {
    mkdir -p "$cache/inc" "$cache/src"
    fetch "$stb_url" "$cache/inc/stb_truetype.h" "$stb_sha"
    unpack "$spng_url" "$spng_sha" "$spng_dir"
    unpack "$zlib_url" "$zlib_sha" "$zlib_dir"
    mkdir -p "$qr_dir"
    fetch "$qr_url/qrcodegen.c" "$qr_dir/qrcodegen.c" "$qr_c_sha"
    fetch "$qr_url/qrcodegen.h" "$qr_dir/qrcodegen.h" "$qr_h_sha"
    fetch "$fb_url" "$cache/fribidi-$fb_ver.tar.xz" "$fb_sha"
    if [[ ! -d $fb_dir ]]; then
        local v
        IFS=. read -r -a v <<< "$fb_ver"
        rm -rf "$fb_dir.part"
        mkdir -p "$fb_dir.part"
        tar -C "$fb_dir.part" --strip-components=1 -xJf "$cache/fribidi-$fb_ver.tar.xz"
        sed -e "s/@configure_input@/made by blockie-frontend build.sh/" -e "s/@PACKAGE@/fribidi/" \
            -e "s/@PACKAGE_NAME@/GNU FriBidi/" -e "s|@PACKAGE_BUGREPORT@|https://github.com/fribidi/fribidi/issues/new|" \
            -e "s/@FRIBIDI_VERSION@/$fb_ver/" -e "s/@FRIBIDI_MAJOR_VERSION@/${v[0]}/" -e "s/@FRIBIDI_MINOR_VERSION@/${v[1]}/" \
            -e "s/@FRIBIDI_MICRO_VERSION@/${v[2]}/" -e "s/@FRIBIDI_INTERFACE_VERSION@/4/g" -e "s/@SIZEOF_INT@/4/" \
            -e "s/@FRIBIDI_MSVC_BUILD_PLACEHOLDER@/#undef FRIBIDI_BUILT_WITH_MSVC/" \
            "$fb_dir.part/lib/fribidi-config.h.in" > "$fb_dir.part/lib/fribidi-config.h"
        mv "$fb_dir.part" "$fb_dir"
    fi
    fetch "$hb_url" "$cache/harfbuzz-$hb_ver.tar.xz" "$hb_sha"
    if [[ ! -d $hb_dir ]]; then
        rm -rf "$hb_dir.part"
        mkdir -p "$hb_dir.part"
        tar -C "$hb_dir.part" --strip-components=1 -xJf "$cache/harfbuzz-$hb_ver.tar.xz" "harfbuzz-$hb_ver/COPYING" \
            "harfbuzz-$hb_ver/src"
        mv "$hb_dir.part" "$hb_dir"
    fi
}

deps_img() {
    mkdir -p "$cache/src"
    unpack "$spng_url" "$spng_sha" "$spng_dir"
    unpack "$zlib_url" "$zlib_sha" "$zlib_dir"
}

deps_sqlite() {
    command -v unzip > /dev/null || { echo "build.sh: unzip not found" >&2; exit 1; }
    mkdir -p "$cache/src"
    fetch "$sqlite_url" "$cache/sqlite-amalgamation-$sqlite_ver.zip" "$sqlite_sha"
    if [[ ! -d $sqlite_dir ]]; then
        rm -rf "$sqlite_dir.part"
        mkdir -p "$sqlite_dir.part"
        unzip -q -j "$cache/sqlite-amalgamation-$sqlite_ver.zip" -d "$sqlite_dir.part"
        mv "$sqlite_dir.part" "$sqlite_dir"
    fi
}

deps_fonts() {
    command -v unzip > /dev/null || { echo "build.sh: unzip not found" >&2; exit 1; }
    mkdir -p "$fonts_dir"
    fetch "$font_url" "$cache/Selawik_Release.zip" "$font_sha"
    unzip -o -q -j "$cache/Selawik_Release.zip" selawkl.ttf selawk.ttf selawkb.ttf selawksl.ttf -d "$fonts_dir"
}

gen_tokens() {
    mkdir -p "$gen"
    LC_ALL=C awk '
BEGIN { n = 0 }
NF == 0 { next }
NF != 3 { printf "build.sh: tokens-blockie.txt bad line %s\n", $0 > "/dev/stderr"; bad = 1; exit 1 }
{
    if ($1 in seen) { printf "build.sh: tokens-blockie.txt duplicate %s\n", $1 > "/dev/stderr"; bad = 1; exit 1 }
    seen[$1] = 1
    name[n] = $1; value[n] = $2; unit[n] = $3
    if ($2 ~ /^-?[0-9]+(\.[0-9]+)?$/) {
        v = $2 * 65536
        num[n] = v < 0 ? int(v - 0.5) : int(v + 0.5)
        kind[n] = "num"
    } else if ($2 ~ /^#[0-9A-Fa-f]{6}$/) {
        num[n] = sprintf("0x%s", toupper(substr($2, 2)))
        kind[n] = "rgb"
    } else {
        num[n] = 0
        kind[n] = "str"
    }
    n++
}
END {
    if (bad) exit 1
    print "#pragma once"
    print "#include <cstdint>"
    print ""
    print "namespace fd::tok {"
    print ""
    print "struct Token {"
    print "    const char* name;"
    print "    const char* value;"
    print "    const char* unit;"
    print "    int64_t num;"
    print "};"
    print ""
    print "inline constexpr Token kTokens[] = {"
    for (i = 0; i < n; i++)
        printf "    {\"%s\", \"%s\", \"%s\", %s},\n", name[i], value[i], unit[i], num[i]
    print "};"
    printf "inline constexpr int kTokenCount = %d;\n", n
    print ""
    for (i = 0; i < n; i++) {
        id = name[i]
        gsub(/\./, "_", id)
        if (kind[i] == "num")
            printf "inline constexpr int32_t %s = (int32_t)kTokens[%d].num;\n", id, i
        else if (kind[i] == "rgb")
            printf "inline constexpr uint32_t %s = (uint32_t)kTokens[%d].num;\n", id, i
        else
            printf "inline constexpr const char* %s = kTokens[%d].value;\n", id, i
    }
    print ""
    print "}"
}
' "$root/ui/tokens-blockie.txt" > "$gen/tokens-blockie.h.tmp" || { rm -f "$gen/tokens-blockie.h.tmp"; exit 1; }
    if ! cmp -s "$gen/tokens-blockie.h.tmp" "$root/ui/tokens-blockie.h"; then
        cp "$gen/tokens-blockie.h.tmp" "$root/ui/tokens-blockie.h"
    fi
    rm -f "$gen/tokens-blockie.h.tmp"
}

deps_of() {
    sed -e ':a' -e '/\\$/{N;s/\\\n//;ba' -e '}' -e q "$1" | sed 's/^[^:]*://'
}

obj() {
    local o=$1
    shift
    local key="$toolver $*"
    if [[ -f $o && -f $o.key && $(cat "$o.key") == "$key" ]]; then
        local f fresh=1
        if [[ -f $o.d ]]; then
            for f in $(deps_of "$o.d"); do
                [[ -f $f && $f -nt $o ]] && { fresh=0; break; }
            done
        fi
        ((fresh)) && return 0
    fi
    mkdir -p "$(dirname "$o")"
    "$@" -MMD -MP -MF "$o.d" -c -o "$o.tmp" || return 1
    mv "$o.tmp" "$o"
    printf '%s' "$key" > "$o.key"
}

pids=()
pool() {
    while (($(jobs -rp | wc -l) >= jobs)); do
        wait -n || { fail=1; }
    done
    obj "$@" &
    pids+=($!)
}

drain() {
    local p
    for p in "${pids[@]}"; do
        wait "$p" || fail=1
    done
    pids=()
}

img_inc=(-I"$spng_dir/spng" -I"$zlib_dir" -I"$qr_dir" -I"$root/lib")
cflags=(-O2 -w "${img_inc[@]}" -DSPNG_STATIC -DSPNG_DISABLE_OPT)
fbflags=(-I"$fb_dir/lib" -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DSTDC_HEADERS=1
    -DHAVE_STRINGIZE=1 -DHAVE_MEMMOVE=1 -DHAVE_MEMSET=1 -DHAVE_STRDUP=1 -DFRIBIDI_BUILD -DFRIBIDI_LIB_STATIC)
zlib_c=(adler32 compress crc32 deflate inffast inflate inftrees trees zutil)
gflags=(-std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$cache/inc" "${img_inc[@]}" -DSPNG_STATIC
    -DFD_BOARD_HEADER="\"$root/board/blockie.h\"" -DFD_TOKENS_HEADER="\"tokens-blockie.h\"")

jpeg_pkg() {
    pkg-config --exists libturbojpeg || { echo "build.sh: libturbojpeg not found" >&2; return 1; }
    jpeg_cflags=$(pkg-config --cflags libturbojpeg)
    jpeg_libs=$(pkg-config --libs libturbojpeg)
}

build_deps_obj() {
    toolver=$("$cc" --version | head -n 1)
    local c m
    for c in "${zlib_c[@]}"; do
        pool "$depobj/zlib_$c.o" "$cc" "${cflags[@]}" $dep_harden "$zlib_dir/$c.c"
    done
    pool "$depobj/spng.o" "$cc" "${cflags[@]}" $dep_harden "$spng_dir/spng/spng.c"
    pool "$depobj/qrcodegen.o" "$cc" "${cflags[@]}" $dep_harden "$qr_dir/qrcodegen.c"
    for c in "$fb_dir"/lib/*.c; do
        m=$(basename "${c%.c}")
        pool "$depobj/fb_$m.o" "$cc" -O2 -w -ffunction-sections -fdata-sections "${fbflags[@]}" $dep_harden "$c"
    done
    drain
    toolver=$("$cxx" --version | head -n 1)
    pool "$depobj/harfbuzz.o" "$cxx" -DHB_TINY -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        $dep_harden "$hb_dir/src/harfbuzz.cc"
    drain
}

dep_objs() {
    printf '%s\n' "$depobj/harfbuzz.o" "$depobj"/fb_*.o "$depobj/spng.o" "$depobj"/zlib_*.o "$depobj/qrcodegen.o"
    local p
    for p in "${wl_proto[@]}"; do
        echo "$depobj/wl_$p.o"
    done
}

wl_gen() {
    command -v wayland-scanner > /dev/null || { echo "build.sh: wayland-scanner not found" >&2; exit 1; }
    pkg-config --exists wayland-client || { echo "build.sh: wayland-client not found" >&2; exit 1; }
    mkdir -p "$gen" "$cache"
    fetch "$xdg_url" "$cache/xdg-shell-1.41.xml" "$xdg_sha"
    fetch "$wlr_url" "$cache/wlr-layer-shell-${wlr_rev:0:8}.xml" "$wlr_sha"
    fetch "$pt_url" "$cache/presentation-time-1.41.xml" "$pt_sha"
    cp "$cache/xdg-shell-1.41.xml" "$gen/xdg-shell.xml"
    cp "$cache/presentation-time-1.41.xml" "$gen/presentation-time.xml"
    sed 's/<arg name="namespace"/<arg name="layer_namespace"/' "$cache/wlr-layer-shell-${wlr_rev:0:8}.xml" \
        > "$gen/wlr-layer-shell-unstable-v1.xml"
    wl_cflags=$(pkg-config --cflags wayland-client)
    wl_libs=$(pkg-config --libs wayland-client)
    local p f
    for p in "${wl_proto[@]}"; do
        wayland-scanner client-header "$gen/$p.xml" "$gen/$p-client-protocol.h.tmp" &&
            wayland-scanner private-code "$gen/$p.xml" "$gen/$p-protocol.c.tmp" ||
            { echo "build.sh: wayland-scanner $p failed" >&2; exit 1; }
        for f in "$gen/$p-client-protocol.h" "$gen/$p-protocol.c"; do
            cmp -s "$f.tmp" "$f" || cp "$f.tmp" "$f"
            rm -f "$f.tmp"
        done
    done
    toolver=$("$cc" --version | head -n 1)
    for p in "${wl_proto[@]}"; do
        pool "$depobj/wl_$p.o" "$cc" -O2 -w $wl_cflags $appcflags "$gen/$p-protocol.c"
    done
    drain
    ((fail)) && { echo "build.sh: wayland protocol objects failed" >&2; exit 1; }
    return 0
}

cpp_extra() {
    case $1 in
        gfx/stb.cpp) echo "-w" ;;
        gfx/bidi.cpp) echo "-I$fb_dir/lib" ;;
        gfx/shape.cpp) echo "-I$hb_dir/src" ;;
        gfx/wayland.cpp) echo "-I$gen $wl_cflags" ;;
        blockie/scale.cpp) echo "-O3" ;;
    esac
}

generic_srcs() {
    local f
    while IFS= read -r f || [[ -n $f ]]; do
        [[ $f == *.cpp ]] && echo "$f"
    done < "$root/generic.txt"
}

build_generic() {
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    pool "$genobj/tools_generic.cpp.o" "$cxx" "${gflags[@]}" $appcflags "$root/tools/generic.cpp"
    gobjs+=("$genobj/tools_generic.cpp.o")
    drain
    ((fail)) && return 1
    mkdir -p "$out/generic"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm -o "$out/generic/blockie-generic.tmp" ||
        return 1
    mv "$out/generic/blockie-generic.tmp" "$out/generic/blockie-generic"
    echo "build.sh: $out/generic/blockie-generic"
}

build_app() {
    build_sqlite_obj || return 1
    pkg-config --exists libcurl || { echo "build.sh: libcurl not found" >&2; return 1; }
    pkg-config --exists dbus-1 || { echo "build.sh: dbus-1 not found" >&2; return 1; }
    local xkb_cflags xkb_libs curl_cflags curl_libs dbus_cflags dbus_libs jpeg_cflags jpeg_libs
    jpeg_pkg || return 1
    dbus_cflags=$(pkg-config --cflags dbus-1)
    dbus_libs=$(pkg-config --libs dbus-1)
    curl_cflags=$(pkg-config --cflags libcurl)
    curl_libs=$(pkg-config --libs libcurl)
    xkb_cflags=
    xkb_libs=
    if pkg-config --exists xkbcommon; then
        xkb_cflags="-DBLOCKIE_HAVE_XKB $(pkg-config --cflags xkbcommon)"
        xkb_libs=$(pkg-config --libs xkbcommon)
        echo "build.sh: keyboard via xkbcommon"
    else
        echo "build.sh: keyboard via raw evdev keycodes (xkbcommon not found)"
    fi
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    pool "$appobj/screen_selector.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_selector.cpp"
    pool "$appobj/vtext.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/vtext.cpp"
    pool "$appobj/text_wrap.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/text_wrap.cpp"
    pool "$appobj/theme.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/theme.cpp"
    pool "$appobj/ink.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/ink.cpp"
    pool "$appobj/screen_settings.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_settings.cpp"
    pool "$appobj/fold.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/fold.cpp"
    pool "$appobj/pagemove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/pagemove.cpp"
    pool "$appobj/bus.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/bus.cpp"
    pool "$appobj/theme_setting.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/theme_setting.cpp"
    pool "$appobj/setd_key.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/setd_key.cpp"
    pool "$appobj/zones.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/zones.cpp"
    pool "$appobj/bt.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/bt.cpp"
    pool "$appobj/bt_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/bt_model.cpp"
    pool "$appobj/screen_bt.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_bt.cpp"
    pool "$appobj/children.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/children.cpp"
    pool "$appobj/volume.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/volume.cpp"
    pool "$appobj/ready.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/ready.cpp"
    pool "$appobj/backlight.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/backlight.cpp"
    pool "$appobj/wifi_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/wifi_model.cpp"
    pool "$appobj/wifi.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/wifi.cpp"
    pool "$appobj/sysinfo.o" "$cxx" "${gflags[@]}" -DBLOCKIE_FE_COMMIT="\"$fe_commit\"" $appcflags \
        "$root/blockie/sysinfo.cpp"
    pool "$appobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    pool "$appobj/image.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$appobj/jpeg.o" "$cxx" "${gflags[@]}" $jpeg_cflags $appcflags "$root/blockie/jpeg.cpp"
    pool "$appobj/ports.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/ports.cpp"
    pool "$appobj/input_pad.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/input_pad.cpp"
    pool "$appobj/repeat.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/repeat.cpp"
    pool "$appobj/nav.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/nav.cpp"
    pool "$appobj/decodeq.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/decodeq.cpp"
    pool "$appobj/leftmove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/leftmove.cpp"
    for s in launchflow toast_strip toast_clock powermenu powermenu_draw layer_present layer_mem pm_input pm_run \
        present_layer powermenu_main portmsg portmsg_view portmsg_main; do
        pool "$appobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    for s in homeflow home motion slide glide t9 scale bench; do
        pool "$appobj/$s.o" "$cxx" "${gflags[@]}" $appcflags $(cpp_extra "blockie/$s.cpp") "$root/blockie/$s.cpp"
    done
    pool "$appobj/launch.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/launch.cpp"
    pool "$appobj/scraper.o" "$cxx" "${scanflags[@]}" $curl_cflags $appcflags "$root/blockie/scraper.cpp"
    pool "$appobj/wl_keys.o" "$cxx" "${gflags[@]}" $xkb_cflags $appcflags "$root/blockie/wl_keys.cpp"
    pool "$appobj/frame_gate.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/frame_gate.cpp"
    pool "$appobj/main.o" "$cxx" "${gflags[@]}" -pthread $appcflags "$root/blockie/main.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/app"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "$appobj/screen_selector.o" "$appobj/vtext.o" "$appobj/text_wrap.o" "$appobj/theme.o" "$appobj/ink.o" \
        "$appobj/screen_settings.o" "$appobj/fold.o" "$appobj/pagemove.o" "$appobj/bus.o" "$appobj/theme_setting.o" \
        "$appobj/setd_key.o" "$appobj/zones.o" "$appobj/bt.o" "$appobj/bt_model.o" "$appobj/screen_bt.o" \
        "$appobj/children.o" "$appobj/volume.o" "$appobj/ready.o" "$appobj/backlight.o" "$appobj/wifi_model.o" \
        "$appobj/wifi.o" "$appobj/sysinfo.o" "$appobj/library.o" "$appobj/image.o" "$appobj/jpeg.o" "$appobj/ports.o" \
        "$appobj/input_pad.o" "$appobj/repeat.o" "$appobj/nav.o" "$appobj/decodeq.o" "$appobj/launch.o" \
        "$appobj/homeflow.o" "$appobj/home.o" "$appobj/motion.o" "$appobj/slide.o" "$appobj/glide.o" "$appobj/t9.o" \
        "$appobj/leftmove.o" "$appobj/launchflow.o" "$appobj/toast_strip.o" "$appobj/toast_clock.o" \
        "$appobj/powermenu_draw.o" "$appobj/layer_present.o" "$appobj/layer_mem.o" \
        "$appobj/scale.o" "$appobj/bench.o" "$appobj/scraper.o" "$appobj/wl_keys.o" "$appobj/frame_gate.o" \
        "$appobj/main.o" "$depobj/sqlite3.o" "${dobjs[@]}" $appldflags $wl_libs $xkb_libs $curl_libs $dbus_libs \
        $jpeg_libs -lpthread -lm -o "$out/app/blockie-frontend.tmp" || return 1
    mv "$out/app/blockie-frontend.tmp" "$out/app/blockie-frontend"
    echo "build.sh: $out/app/blockie-frontend"
    "$cxx" "${gobjs[@]}" "$appobj/powermenu.o" "$appobj/powermenu_draw.o" "$appobj/toast_clock.o" \
        "$appobj/layer_present.o" "$appobj/pm_input.o" "$appobj/pm_run.o" "$appobj/input_pad.o" "$appobj/repeat.o" \
        "$appobj/screen_settings.o" "$appobj/theme.o" "$appobj/ink.o" "$appobj/zones.o" "$appobj/leftmove.o" \
        "$appobj/bus.o" "$appobj/theme_setting.o" "$appobj/setd_key.o" "$appobj/present_layer.o" \
        "$appobj/powermenu_main.o" "${dobjs[@]}" $appldflags $wl_libs $dbus_libs -lpthread -lm \
        -o "$out/app/blockie-powermenu.tmp" || return 1
    mv "$out/app/blockie-powermenu.tmp" "$out/app/blockie-powermenu"
    echo "build.sh: $out/app/blockie-powermenu"
    "$cxx" "${gobjs[@]}" "$appobj/portmsg.o" "$appobj/portmsg_view.o" "$appobj/portmsg_main.o" "$appobj/input_pad.o" \
        "$appobj/repeat.o" "$appobj/glide.o" "$appobj/ink.o" "$appobj/theme.o" "$appobj/layer_present.o" \
        "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm -o "$out/app/blockie-portmsg.tmp" || return 1
    mv "$out/app/blockie-portmsg.tmp" "$out/app/blockie-portmsg"
    echo "build.sh: $out/app/blockie-portmsg"
}

sqlite_defs=(-DSQLITE_OMIT_LOAD_EXTENSION)
scanflags=(-std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$sqlite_dir")

build_sqlite_obj() {
    deps_sqlite
    toolver=$("$cc" --version | head -n 1)
    pool "$depobj/sqlite3.o" "$cc" -O2 -w "${sqlite_defs[@]}" $dep_harden "$sqlite_dir/sqlite3.c"
    drain
    ((fail)) && return 1
    return 0
}

build_scan() {
    build_sqlite_obj || return 1
    toolver=$("$cxx" --version | head -n 1)
    pool "$scanobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    pool "$scanobj/scan_main.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/scan_main.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/scan"
    "$cxx" "$scanobj/library.o" "$scanobj/scan_main.o" "$depobj/sqlite3.o" $appldflags -lpthread -lm \
        -o "$out/scan/blockie-scan.tmp" || return 1
    mv "$out/scan/blockie-scan.tmp" "$out/scan/blockie-scan"
    echo "build.sh: $out/scan/blockie-scan"
}

build_test() {
    build_sqlite_obj || return 1
    toolver=$("$cxx" --version | head -n 1)
    pool "$scanobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    pool "$testobj/library_test.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/tests/library_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/test"
    "$cxx" "$scanobj/library.o" "$testobj/library_test.o" "$depobj/sqlite3.o" $appldflags -lpthread -lm \
        -o "$out/test/blockie-test.tmp" || return 1
    mv "$out/test/blockie-test.tmp" "$out/test/blockie-test"
    echo "build.sh: $out/test/blockie-test"
    "$out/test/blockie-test"
}

build_scraper_test() {
    build_sqlite_obj || return 1
    pkg-config --exists libcurl || { echo "build.sh: libcurl not found" >&2; return 1; }
    local curl_cflags curl_libs
    curl_cflags=$(pkg-config --cflags libcurl)
    curl_libs=$(pkg-config --libs libcurl)
    toolver=$("$cxx" --version | head -n 1)
    pool "$scanobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    pool "$scraperobj/scraper.o" "$cxx" "${scanflags[@]}" $curl_cflags $appcflags "$root/blockie/scraper.cpp"
    pool "$testobj/scraper_test.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/tests/scraper_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/scraper"
    "$cxx" "$scanobj/library.o" "$scraperobj/scraper.o" "$testobj/scraper_test.o" "$depobj/sqlite3.o" \
        $appldflags $curl_libs -lpthread -lm -o "$out/scraper/blockie-scraper-test.tmp" || return 1
    mv "$out/scraper/blockie-scraper-test.tmp" "$out/scraper/blockie-scraper-test"
    echo "build.sh: $out/scraper/blockie-scraper-test"
    "$out/scraper/blockie-scraper-test"
}

launchflags=(-std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers)

build_launch_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$launchobj/launch.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/launch.cpp"
    pool "$launchobj/ports.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/ports.cpp"
    pool "$testobj/launch_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/launch_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/launch"
    "$cxx" "$launchobj/launch.o" "$launchobj/ports.o" "$testobj/launch_test.o" $appldflags \
        -o "$out/launch/blockie-launch-test.tmp" || return 1
    mv "$out/launch/blockie-launch-test.tmp" "$out/launch/blockie-launch-test"
    echo "build.sh: $out/launch/blockie-launch-test"
    "$out/launch/blockie-launch-test"
}

build_pad_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$launchobj/input_pad.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/input_pad.cpp"
    pool "$testobj/pad_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/pad_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/launch"
    "$cxx" "$launchobj/input_pad.o" "$testobj/pad_test.o" $appldflags -o "$out/launch/blockie-pad-test.tmp" || return 1
    mv "$out/launch/blockie-pad-test.tmp" "$out/launch/blockie-pad-test"
    echo "build.sh: $out/launch/blockie-pad-test"
    "$out/launch/blockie-pad-test"
}

build_image_test() {
    deps_img
    local jpeg_cflags jpeg_libs
    jpeg_pkg || return 1
    toolver=$("$cc" --version | head -n 1)
    local c
    for c in "${zlib_c[@]}"; do
        pool "$depobj/zlib_$c.o" "$cc" "${cflags[@]}" $dep_harden "$zlib_dir/$c.c"
    done
    pool "$depobj/spng.o" "$cc" "${cflags[@]}" $dep_harden "$spng_dir/spng/spng.c"
    drain
    ((fail)) && return 1
    toolver=$("$cxx" --version | head -n 1)
    pool "$genobj/gfx_canvas.cpp.o" "$cxx" "${gflags[@]}" $appcflags "$root/gfx/canvas.cpp"
    pool "$imgobj/image.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$imgobj/jpeg.o" "$cxx" "${gflags[@]}" $jpeg_cflags $appcflags "$root/blockie/jpeg.cpp"
    pool "$testobj/image_test.o" "$cxx" "${gflags[@]}" $jpeg_cflags $appcflags "$root/blockie/tests/image_test.cpp"
    pool "$imgobj/scale.o" "$cxx" "${gflags[@]}" $appcflags $(cpp_extra blockie/scale.cpp) "$root/blockie/scale.cpp"
    pool "$testobj/scale_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/scale_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/image"
    "$cxx" "$imgobj/image.o" "$imgobj/jpeg.o" "$testobj/image_test.o" "$genobj/gfx_canvas.cpp.o" "$depobj/spng.o" \
        "$depobj"/zlib_*.o $appldflags $jpeg_libs -lpthread -lm -o "$out/image/blockie-image-test.tmp" || return 1
    mv "$out/image/blockie-image-test.tmp" "$out/image/blockie-image-test"
    "$cxx" "$imgobj/scale.o" "$testobj/scale_test.o" "$genobj/gfx_canvas.cpp.o" $appldflags -lpthread -lm \
        -o "$out/image/blockie-scale-test.tmp" || return 1
    mv "$out/image/blockie-scale-test.tmp" "$out/image/blockie-scale-test"
    echo "build.sh: $out/image/blockie-image-test $out/image/blockie-scale-test"
    local rc=0
    "$out/image/blockie-image-test" || rc=1
    "$out/image/blockie-scale-test" || rc=1
    return $rc
}

build_tsan_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$genobj/gfx_canvas.cpp.o" "$cxx" "${gflags[@]}" $appcflags "$root/gfx/canvas.cpp"
    pool "$tsanobj/scale.o" "$cxx" "${gflags[@]}" -g -fsanitize=thread "$root/blockie/scale.cpp"
    pool "$tsanobj/scale_test.o" "$cxx" "${gflags[@]}" -g -fsanitize=thread "$root/blockie/tests/scale_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/tsan"
    "$cxx" "$tsanobj/scale.o" "$tsanobj/scale_test.o" "$genobj/gfx_canvas.cpp.o" -fsanitize=thread -lpthread -lm \
        -o "$out/tsan/blockie-scale-tsan-test.tmp" || return 1
    mv "$out/tsan/blockie-scale-tsan-test.tmp" "$out/tsan/blockie-scale-tsan-test"
    echo "build.sh: $out/tsan/blockie-scale-tsan-test"
    TSAN_OPTIONS=halt_on_error=1 "$out/tsan/blockie-scale-tsan-test"
}

build_input_test() {
    pool "$testobj/input_pad.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/input_pad.cpp"
    pool "$testobj/repeat.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/repeat.cpp"
    pool "$testobj/nav.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/nav.cpp"
    pool "$testobj/repeat_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/repeat_test.cpp"
    pool "$testobj/nav_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/nav_test.cpp"
    pool "$testobj/home.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/home.cpp"
    pool "$genobj/core_curves.cpp.o" "$cxx" "${gflags[@]}" $appcflags "$root/core/curves.cpp"
    pool "$testobj/glide.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/glide.cpp"
    pool "$testobj/home_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/home_test.cpp"
    pool "$testobj/tile_grid_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/tile_grid_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/input"
    "$cxx" "$testobj/input_pad.o" "$testobj/repeat.o" "$testobj/repeat_test.o" $appldflags \
        -o "$out/input/blockie-repeat-test.tmp" || return 1
    mv "$out/input/blockie-repeat-test.tmp" "$out/input/blockie-repeat-test"
    "$cxx" "$testobj/input_pad.o" "$testobj/nav.o" "$testobj/nav_test.o" $appldflags \
        -o "$out/input/blockie-nav-test.tmp" || return 1
    mv "$out/input/blockie-nav-test.tmp" "$out/input/blockie-nav-test"
    "$cxx" "$testobj/home.o" "$testobj/glide.o" "$testobj/home_test.o" "$genobj/core_curves.cpp.o" $appldflags -lm \
        -o "$out/input/blockie-home-test.tmp" || return 1
    mv "$out/input/blockie-home-test.tmp" "$out/input/blockie-home-test"
    "$cxx" "$testobj/tile_grid_test.o" $appldflags -o "$out/input/blockie-tile-grid-test.tmp" || return 1
    mv "$out/input/blockie-tile-grid-test.tmp" "$out/input/blockie-tile-grid-test"
    echo "build.sh: $out/input/blockie-repeat-test $out/input/blockie-nav-test $out/input/blockie-home-test" \
        "$out/input/blockie-tile-grid-test"
    "$out/input/blockie-repeat-test" && "$out/input/blockie-nav-test" && "$out/input/blockie-home-test" &&
        "$out/input/blockie-tile-grid-test"
}

build_motion_test() {
    if grep -n 'motion_pivot' "$root/blockie/leftmove.cpp" "$root/blockie/leftmove.h"; then
        echo "build.sh: leftmove reads a motion.pivot token" >&2
        return 1
    fi
    toolver=$("$cxx" --version | head -n 1)
    local s
    for s in core/curves.cpp gfx/canvas.cpp; do
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $appcflags "$root/$s"
    done
    pool "$testobj/fold.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/fold.cpp"
    pool "$testobj/pagemove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/pagemove.cpp"
    pool "$testobj/ink.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/ink.cpp"
    pool "$testobj/theme.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/theme.cpp"
    pool "$testobj/fold_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/fold_test.cpp"
    pool "$testobj/pagemove_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/pagemove_test.cpp"
    pool "$testobj/ink_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/ink_test.cpp"
    pool "$testobj/leftmove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/leftmove.cpp"
    pool "$testobj/leftmove_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/leftmove_test.cpp"
    for s in motion glide slide home nav repeat pops t9; do
        pool "$testobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    for s in motion_test glide_test slide_test pops_test; do
        pool "$testobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/$s.cpp"
    done
    drain
    ((fail)) && return 1
    mkdir -p "$out/motion"
    "$cxx" "$testobj/fold.o" "$testobj/fold_test.o" "$genobj/gfx_canvas.cpp.o" $appldflags -lpthread -lm \
        -o "$out/motion/blockie-fold-test" || return 1
    "$cxx" "$testobj/pagemove.o" "$testobj/pagemove_test.o" "$genobj/core_curves.cpp.o" $appldflags -lpthread -lm \
        -o "$out/motion/blockie-pagemove-test" || return 1
    "$cxx" "$testobj/ink.o" "$testobj/theme.o" "$testobj/ink_test.o" "$genobj/gfx_canvas.cpp.o" $appldflags \
        -lpthread -lm -o "$out/motion/blockie-ink-test" || return 1
    "$cxx" "$testobj/motion.o" "$testobj/fold.o" "$testobj/slide.o" "$testobj/home.o" "$testobj/nav.o" \
        "$testobj/leftmove.o" \
        "$testobj/glide.o" "$testobj/repeat.o" "$testobj/theme.o" "$testobj/t9.o" "$testobj/motion_test.o" \
        "$genobj/core_curves.cpp.o" "$genobj/gfx_canvas.cpp.o" \
        $appldflags -lpthread -lm -o "$out/motion/blockie-motion-test" || return 1
    "$cxx" "$testobj/glide.o" "$testobj/glide_test.o" "$genobj/core_curves.cpp.o" $appldflags -lm \
        -o "$out/motion/blockie-glide-test" || return 1
    "$cxx" "$testobj/slide.o" "$testobj/fold.o" "$testobj/motion.o" "$testobj/slide_test.o" \
        "$testobj/leftmove.o" \
        "$genobj/core_curves.cpp.o" "$genobj/gfx_canvas.cpp.o" $appldflags -lpthread -lm \
        -o "$out/motion/blockie-slide-test" || return 1
    "$cxx" "$testobj/leftmove.o" "$testobj/slide.o" "$testobj/fold.o" "$testobj/leftmove_test.o" \
        "$genobj/core_curves.cpp.o" "$genobj/gfx_canvas.cpp.o" $appldflags -lpthread -lm \
        -o "$out/motion/blockie-leftmove-test" || return 1
    "$cxx" "$testobj/pops.o" "$testobj/pops_test.o" "$genobj/gfx_canvas.cpp.o" $appldflags -lpthread -lm \
        -o "$out/motion/blockie-pops-test" || return 1
    echo "build.sh: $out/motion"
    "$out/motion/blockie-fold-test" && "$out/motion/blockie-pagemove-test" && "$out/motion/blockie-ink-test" &&
        "$out/motion/blockie-motion-test" && "$out/motion/blockie-glide-test" && "$out/motion/blockie-slide-test" &&
        "$out/motion/blockie-leftmove-test" &&
        "$out/motion/blockie-pops-test"
}

build_gate_test() {
    toolver=$("$cxx" --version | head -n 1)
    local xkb_cflags= xkb_libs= xkb_test=
    if pkg-config --exists xkbcommon; then
        xkb_cflags="-DBLOCKIE_HAVE_XKB $(pkg-config --cflags xkbcommon)"
        xkb_test="-DBLOCKIE_KEYS_XKB $(pkg-config --cflags xkbcommon)"
        xkb_libs=$(pkg-config --libs xkbcommon)
        echo "build.sh: key test via xkbcommon"
    else
        echo "build.sh: key test via raw evdev keycodes (xkbcommon not found)"
    fi
    pool "$testobj/frame_gate.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/frame_gate.cpp"
    pool "$testobj/frame_gate_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/frame_gate_test.cpp"
    pool "$testobj/wl_keys.o" "$cxx" "${launchflags[@]}" $xkb_cflags $appcflags "$root/blockie/wl_keys.cpp"
    pool "$testobj/wl_keys_test.o" "$cxx" "${launchflags[@]}" $xkb_test $appcflags \
        "$root/blockie/tests/wl_keys_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/gate"
    "$cxx" "$testobj/frame_gate.o" "$testobj/frame_gate_test.o" $appldflags -o "$out/gate/blockie-gate-test.tmp" ||
        return 1
    mv "$out/gate/blockie-gate-test.tmp" "$out/gate/blockie-gate-test"
    "$cxx" "$testobj/wl_keys.o" "$testobj/wl_keys_test.o" $appldflags $xkb_libs -o "$out/gate/blockie-keys-test.tmp" ||
        return 1
    mv "$out/gate/blockie-keys-test.tmp" "$out/gate/blockie-keys-test"
    echo "build.sh: $out/gate/blockie-gate-test $out/gate/blockie-keys-test"
    "$out/gate/blockie-gate-test" && "$out/gate/blockie-keys-test"
}

build_bt_model_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$testobj/bt_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/bt_model.cpp"
    pool "$testobj/bt_model_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/bt_model_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/bt"
    "$cxx" "$testobj/bt_model.o" "$testobj/bt_model_test.o" $appldflags -o "$out/bt/blockie-bt-model-test.tmp" || return 1
    mv "$out/bt/blockie-bt-model-test.tmp" "$out/bt/blockie-bt-model-test"
    echo "build.sh: $out/bt/blockie-bt-model-test"
    "$out/bt/blockie-bt-model-test"
}

build_sysinfo_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$testobj/sysinfo.o" "$cxx" "${launchflags[@]}" -DBLOCKIE_FE_COMMIT="\"$fe_commit\"" $appcflags \
        "$root/blockie/sysinfo.cpp"
    pool "$testobj/sysinfo_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/sysinfo_test.cpp"
    pool "$testobj/children.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/children.cpp"
    pool "$testobj/volume.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/volume.cpp"
    pool "$testobj/volume_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/volume_test.cpp"
    pool "$testobj/ready.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/ready.cpp"
    pool "$testobj/ready_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/ready_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/sysinfo"
    "$cxx" "$testobj/sysinfo.o" "$testobj/sysinfo_test.o" $appldflags -lpthread \
        -o "$out/sysinfo/blockie-sysinfo-test.tmp" || return 1
    mv "$out/sysinfo/blockie-sysinfo-test.tmp" "$out/sysinfo/blockie-sysinfo-test"
    "$cxx" "$testobj/children.o" "$testobj/volume.o" "$testobj/volume_test.o" $appldflags \
        -o "$out/sysinfo/blockie-volume-test.tmp" || return 1
    mv "$out/sysinfo/blockie-volume-test.tmp" "$out/sysinfo/blockie-volume-test"
    "$cxx" "$testobj/children.o" "$testobj/ready.o" "$testobj/ready_test.o" $appldflags \
        -o "$out/sysinfo/blockie-ready-test.tmp" || return 1
    mv "$out/sysinfo/blockie-ready-test.tmp" "$out/sysinfo/blockie-ready-test"
    echo "build.sh: $out/sysinfo/blockie-sysinfo-test $out/sysinfo/blockie-volume-test $out/sysinfo/blockie-ready-test"
    "$out/sysinfo/blockie-sysinfo-test" "$fe_commit" && "$out/sysinfo/blockie-volume-test" &&
        "$out/sysinfo/blockie-ready-test"
}

build_wifi_model_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$testobj/wifi_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/wifi_model.cpp"
    pool "$testobj/wifi_model_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/wifi_model_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/wifi"
    "$cxx" "$testobj/wifi_model.o" "$testobj/wifi_model_test.o" $appldflags \
        -o "$out/wifi/blockie-wifi-model-test.tmp" || return 1
    mv "$out/wifi/blockie-wifi-model-test.tmp" "$out/wifi/blockie-wifi-model-test"
    echo "build.sh: $out/wifi/blockie-wifi-model-test"
    "$out/wifi/blockie-wifi-model-test"
}

build_test_dbus() {
    toolver=$("$cxx" --version | head -n 1)
    pkg-config --exists dbus-1 || { echo "build.sh: dbus-1 not found" >&2; return 1; }
    command -v dbus-daemon > /dev/null || { echo "build.sh: dbus-daemon not found" >&2; return 1; }
    local dbus_cflags dbus_libs
    dbus_cflags=$(pkg-config --cflags dbus-1)
    dbus_libs=$(pkg-config --libs dbus-1)
    pool "$testobj/bt_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/bt_model.cpp"
    pool "$testobj/bt.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/bt.cpp"
    pool "$testobj/bus.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/bus.cpp"
    pool "$testobj/bt_dbus_test.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/tests/bt_dbus_test.cpp"
    pool "$testobj/setd_key.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/setd_key.cpp"
    pool "$testobj/theme_setting.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/theme_setting.cpp"
    pool "$testobj/setd_dbus_test.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags \
        "$root/blockie/tests/setd_dbus_test.cpp"
    pool "$testobj/wifi_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/wifi_model.cpp"
    pool "$testobj/wifi.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/wifi.cpp"
    pool "$testobj/wifi_dbus_test.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags \
        "$root/blockie/tests/wifi_dbus_test.cpp"
    pool "$testobj/backlight.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags "$root/blockie/backlight.cpp"
    pool "$testobj/backlight_dbus_test.o" "$cxx" "${gflags[@]}" $dbus_cflags $appcflags \
        "$root/blockie/tests/backlight_dbus_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/btdbus"
    "$cxx" "$testobj/bt_model.o" "$testobj/bt.o" "$testobj/bus.o" "$testobj/bt_dbus_test.o" $appldflags $dbus_libs \
        -o "$out/btdbus/blockie-bt-dbus-test.tmp" || return 1
    mv "$out/btdbus/blockie-bt-dbus-test.tmp" "$out/btdbus/blockie-bt-dbus-test"
    "$cxx" "$testobj/setd_key.o" "$testobj/theme_setting.o" "$testobj/bus.o" "$testobj/setd_dbus_test.o" $appldflags \
        $dbus_libs -o "$out/btdbus/blockie-setd-dbus-test.tmp" || return 1
    mv "$out/btdbus/blockie-setd-dbus-test.tmp" "$out/btdbus/blockie-setd-dbus-test"
    "$cxx" "$testobj/wifi_model.o" "$testobj/wifi.o" "$testobj/bus.o" "$testobj/wifi_dbus_test.o" $appldflags \
        $dbus_libs -o "$out/btdbus/blockie-wifi-dbus-test.tmp" || return 1
    mv "$out/btdbus/blockie-wifi-dbus-test.tmp" "$out/btdbus/blockie-wifi-dbus-test"
    "$cxx" "$testobj/backlight.o" "$testobj/wifi_model.o" "$testobj/bus.o" "$testobj/backlight_dbus_test.o" \
        $appldflags $dbus_libs -o "$out/btdbus/blockie-backlight-dbus-test.tmp" || return 1
    mv "$out/btdbus/blockie-backlight-dbus-test.tmp" "$out/btdbus/blockie-backlight-dbus-test"
    echo "build.sh: $out/btdbus/blockie-bt-dbus-test $out/btdbus/blockie-setd-dbus-test" \
        "$out/btdbus/blockie-wifi-dbus-test $out/btdbus/blockie-backlight-dbus-test"
    python3 "$root/blockie/tests/fake_bluez.py" "$out/btdbus/blockie-bt-dbus-test" &&
        python3 "$root/blockie/tests/fake_setd.py" "$out/btdbus/blockie-setd-dbus-test" &&
        python3 "$root/blockie/tests/fake_nm.py" "$out/btdbus/blockie-wifi-dbus-test" &&
        python3 "$root/blockie/tests/fake_logind.py" "$out/btdbus/blockie-backlight-dbus-test"
}

build_wrap_test() {
    pool "$testobj/text_wrap.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/text_wrap.cpp"
    pool "$testobj/text_wrap_test.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/tests/text_wrap_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/wrap"
    "$cxx" "$testobj/text_wrap.o" "$testobj/text_wrap_test.o" $appldflags -o "$out/wrap/blockie-wrap-test.tmp" || return 1
    mv "$out/wrap/blockie-wrap-test.tmp" "$out/wrap/blockie-wrap-test"
    echo "build.sh: $out/wrap/blockie-wrap-test"
    "$out/wrap/blockie-wrap-test"
}

build_zones_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$testobj/zones.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/zones.cpp"
    pool "$testobj/zones_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/zones_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/zones"
    "$cxx" "$testobj/zones.o" "$testobj/zones_test.o" $appldflags -o "$out/zones/blockie-zones-test.tmp" || return 1
    mv "$out/zones/blockie-zones-test.tmp" "$out/zones/blockie-zones-test"
    echo "build.sh: $out/zones/blockie-zones-test"
    "$out/zones/blockie-zones-test"
}

build_decodeq_test() {
    toolver=$("$cxx" --version | head -n 1)
    pool "$testobj/decodeq.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/decodeq.cpp"
    pool "$testobj/decodeq_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/decodeq_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/decodeq"
    "$cxx" "$testobj/decodeq.o" "$testobj/decodeq_test.o" $appldflags -o "$out/decodeq/blockie-decodeq-test.tmp" ||
        return 1
    mv "$out/decodeq/blockie-decodeq-test.tmp" "$out/decodeq/blockie-decodeq-test"
    echo "build.sh: $out/decodeq/blockie-decodeq-test"
    "$out/decodeq/blockie-decodeq-test"
}

build_vtext_test() {
    deps
    build_deps_obj
    ((fail)) && return 1
    deps_fonts
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    for s in vtext theme t9 screen_selector screen_settings ink homeflow home motion nav glide slide pagemove fold \
        bt_model; do
        pool "$testobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    pool "$testobj/text_wrap_g.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/text_wrap.cpp"
    pool "$testobj/leftmove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/leftmove.cpp"
    pool "$testobj/zones_g.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/zones.cpp"
    for s in launchflow toast_strip launch repeat; do
        pool "$testobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    for s in powermenu_draw layer_present layer_mem toast_clock; do
        pool "$testobj/pm_$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    pool "$testobj/frame_gate.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/frame_gate.cpp"
    pool "$testobj/launchmove_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/launchmove_test.cpp"
    pool "$testobj/settings_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/settings_test.cpp"
    pool "$testobj/vtext_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/vtext_test.cpp"
    pool "$testobj/t9_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/t9_test.cpp"
    pool "$testobj/selector_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/selector_test.cpp"
    pool "$testobj/homeflow_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/homeflow_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/vtext"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "$testobj/vtext.o" "$testobj/theme.o" "$testobj/vtext_test.o" "${dobjs[@]}" $appldflags \
        $wl_libs -lpthread -lm -o "$out/vtext/blockie-vtext-test.tmp" || return 1
    mv "$out/vtext/blockie-vtext-test.tmp" "$out/vtext/blockie-vtext-test"
    "$cxx" "${gobjs[@]}" "$testobj/t9.o" "$testobj/text_wrap_g.o" "$testobj/screen_selector.o" "$testobj/vtext.o" \
        "$testobj/theme.o" "$testobj/t9_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/vtext/blockie-t9-test.tmp" || return 1
    mv "$out/vtext/blockie-t9-test.tmp" "$out/vtext/blockie-t9-test"
    "$cxx" "${gobjs[@]}" "$testobj/text_wrap_g.o" "$testobj/screen_selector.o" "$testobj/vtext.o" "$testobj/theme.o" \
        "$testobj/selector_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/vtext/blockie-selector-test.tmp" || return 1
    mv "$out/vtext/blockie-selector-test.tmp" "$out/vtext/blockie-selector-test"
    "$cxx" "${gobjs[@]}" "$testobj/screen_settings.o" "$testobj/ink.o" "$testobj/theme.o" "$testobj/zones_g.o" \
        "$testobj/leftmove.o" "$testobj/pm_toast_clock.o" "$testobj/bt_model.o" "$testobj/settings_test.o" \
        "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm -o "$out/vtext/blockie-settings-test.tmp" || return 1
    mv "$out/vtext/blockie-settings-test.tmp" "$out/vtext/blockie-settings-test"
    "$cxx" "${gobjs[@]}" "$testobj/homeflow.o" "$testobj/home.o" "$testobj/motion.o" "$testobj/nav.o" \
        "$testobj/glide.o" "$testobj/slide.o" "$testobj/pagemove.o" "$testobj/fold.o" "$testobj/t9.o" \
        "$testobj/leftmove.o" "$testobj/launchflow.o" "$testobj/launch.o" "$testobj/repeat.o" \
        "$testobj/text_wrap_g.o" "$testobj/screen_selector.o" "$testobj/vtext.o" "$testobj/theme.o" \
        "$testobj/homeflow_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/vtext/blockie-homeflow-test.tmp" || return 1
    mv "$out/vtext/blockie-homeflow-test.tmp" "$out/vtext/blockie-homeflow-test"
    "$cxx" "${gobjs[@]}" "$testobj/homeflow.o" "$testobj/home.o" "$testobj/motion.o" "$testobj/nav.o" \
        "$testobj/glide.o" "$testobj/slide.o" "$testobj/pagemove.o" "$testobj/fold.o" "$testobj/t9.o" \
        "$testobj/leftmove.o" "$testobj/launchflow.o" "$testobj/launch.o" "$testobj/repeat.o" "$testobj/toast_strip.o" \
        "$testobj/pm_powermenu_draw.o" "$testobj/pm_layer_present.o" "$testobj/pm_layer_mem.o" \
        "$testobj/pm_toast_clock.o" "$testobj/frame_gate.o" \
        "$testobj/screen_settings.o" "$testobj/ink.o" "$testobj/zones_g.o" \
        "$testobj/text_wrap_g.o" "$testobj/screen_selector.o" "$testobj/vtext.o" "$testobj/theme.o" \
        "$testobj/launchmove_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/vtext/blockie-launchmove-test.tmp" || return 1
    mv "$out/vtext/blockie-launchmove-test.tmp" "$out/vtext/blockie-launchmove-test"
    echo "build.sh: $out/vtext/blockie-vtext-test $out/vtext/blockie-t9-test $out/vtext/blockie-selector-test" \
        "$out/vtext/blockie-settings-test $out/vtext/blockie-homeflow-test $out/vtext/blockie-launchmove-test"
    local cjk
    cjk=$(fc-list ':family=Noto Sans CJK JP:style=Light' file 2> /dev/null | head -n 1 | sed 's/: *$//' || true)
    [[ -n $cjk && -f $cjk ]] || cjk=
    echo "build.sh: vtext CJK fallback ${cjk:-none}"
    "$out/vtext/blockie-vtext-test" "$fonts_dir" "$cjk" && "$out/vtext/blockie-t9-test" "$fonts_dir" &&
        "$out/vtext/blockie-selector-test" "$fonts_dir" && "$out/vtext/blockie-settings-test" "$fonts_dir" &&
        "$out/vtext/blockie-homeflow-test" "$fonts_dir" && "$out/vtext/blockie-launchmove-test" "$fonts_dir"
}

build_powermenu_test() {
    deps
    build_deps_obj
    ((fail)) && return 1
    deps_fonts
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=() pobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    for s in powermenu powermenu_draw toast_clock layer_present layer_mem pm_input pm_run input_pad repeat \
        screen_settings theme ink zones leftmove; do
        pool "$testobj/pm_$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
        pobjs+=("$testobj/pm_$s.o")
    done
    pool "$testobj/powermenu_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/powermenu_test.cpp"
    pool "$testobj/powermenu_main.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/powermenu_main.cpp"
    pool "$testobj/present_layer.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/present_layer.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/powermenu"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "${pobjs[@]}" "$testobj/powermenu_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/powermenu/blockie-powermenu-test.tmp" || return 1
    mv "$out/powermenu/blockie-powermenu-test.tmp" "$out/powermenu/blockie-powermenu-test"
    echo "build.sh: $out/powermenu/blockie-powermenu-test"
    "$out/powermenu/blockie-powermenu-test" "$fonts_dir"
}

build_portmsg_test() {
    deps
    build_deps_obj
    ((fail)) && return 1
    deps_fonts
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=() mobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    for s in portmsg portmsg_view input_pad repeat glide ink theme layer_present; do
        pool "$testobj/msg_$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
        mobjs+=("$testobj/msg_$s.o")
    done
    pool "$testobj/portmsg_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/portmsg_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/portmsg"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "${mobjs[@]}" "$testobj/portmsg_test.o" "${dobjs[@]}" $appldflags $wl_libs -lpthread -lm \
        -o "$out/portmsg/blockie-portmsg-test.tmp" || return 1
    mv "$out/portmsg/blockie-portmsg-test.tmp" "$out/portmsg/blockie-portmsg-test"
    echo "build.sh: $out/portmsg/blockie-portmsg-test"
    "$out/portmsg/blockie-portmsg-test" "$fonts_dir"
}

build_shot() {
    deps_fonts
    build_sqlite_obj || return 1
    local jpeg_cflags jpeg_libs
    jpeg_pkg || return 1
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    pool "$shotobj/screen_selector.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_selector.cpp"
    pool "$shotobj/text_wrap.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/text_wrap.cpp"
    pool "$shotobj/theme.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/theme.cpp"
    pool "$shotobj/ink.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/ink.cpp"
    pool "$shotobj/screen_settings.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_settings.cpp"
    pool "$shotobj/zones.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/zones.cpp"
    pool "$shotobj/fold.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/fold.cpp"
    pool "$shotobj/bt_model.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/bt_model.cpp"
    pool "$shotobj/screen_bt.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_bt.cpp"
    pool "$shotobj/leftmove.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/leftmove.cpp"
    pool "$shotobj/image.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$shotobj/jpeg.o" "$cxx" "${gflags[@]}" $jpeg_cflags $appcflags "$root/blockie/jpeg.cpp"
    pool "$shotobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    for s in vtext nav home homeflow motion glide t9 slide pagemove repeat pops powermenu powermenu_draw layer_present \
        layer_mem launchflow toast_strip toast_clock launch sysinfo decodeq portmsg portmsg_view input_pad; do
        pool "$shotobj/$s.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/$s.cpp"
    done
    pool "$shotobj/shot.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/shot.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/shot"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "$shotobj/screen_selector.o" "$shotobj/text_wrap.o" "$shotobj/theme.o" "$shotobj/ink.o" \
        "$shotobj/screen_settings.o" "$shotobj/zones.o" "$shotobj/fold.o" "$shotobj/bt_model.o" \
        "$shotobj/screen_bt.o" "$shotobj/image.o" "$shotobj/vtext.o" "$shotobj/nav.o" "$shotobj/home.o" \
        "$shotobj/leftmove.o" \
        "$shotobj/motion.o" "$shotobj/glide.o" "$shotobj/t9.o" "$shotobj/slide.o" "$shotobj/pagemove.o" \
        "$shotobj/repeat.o" "$shotobj/pops.o" "$shotobj/homeflow.o" "$shotobj/powermenu.o" \
        "$shotobj/powermenu_draw.o" "$shotobj/layer_present.o" "$shotobj/layer_mem.o" "$shotobj/launchflow.o" \
        "$shotobj/toast_strip.o" "$shotobj/toast_clock.o" "$shotobj/launch.o" "$shotobj/sysinfo.o" "$shotobj/shot.o" \
        "$shotobj/jpeg.o" "$shotobj/library.o" "$shotobj/decodeq.o" "$shotobj/portmsg.o" "$shotobj/portmsg_view.o" \
        "$shotobj/input_pad.o" "$depobj/sqlite3.o" "${dobjs[@]}" $appldflags $wl_libs $jpeg_libs -lpthread -lm \
        -o "$out/shot/blockie-shot.tmp" || return 1
    mv "$out/shot/blockie-shot.tmp" "$out/shot/blockie-shot"
    echo "build.sh: $out/shot/blockie-shot"
    echo "build.sh: fonts $fonts_dir"
}

build_install() {
    [[ -x $out/app/blockie-frontend && -x $out/app/blockie-powermenu && -x $out/app/blockie-portmsg ]] ||
        { echo "build.sh: run 'build.sh app' before install" >&2; return 1; }
    deps_fonts
    install -Dm755 "$out/app/blockie-frontend" "$destdir$prefix/libexec/blockie/blockie-frontend" || return 1
    install -Dm755 "$out/app/blockie-powermenu" "$destdir$prefix/libexec/blockie/blockie-powermenu" || return 1
    install -Dm755 "$out/app/blockie-portmsg" "$destdir$prefix/libexec/blockie/blockie-portmsg" || return 1
    install -Dm644 "$root/dist/blockie-frontend.service" "$destdir$prefix/lib/systemd/user/blockie-frontend.service" ||
        return 1
    install -Dm644 "$root/LICENSE" "$destdir$prefix/share/licenses/blockie-frontend/LICENSE" || return 1
    install -Dm644 "$root/dist/schema/20-frontend.json" "$destdir$prefix/share/blockie/schema.d/20-frontend.json" || return 1
    local f
    for f in selawkl.ttf selawk.ttf selawkb.ttf selawksl.ttf; do
        install -Dm644 "$fonts_dir/$f" "$destdir$prefix/share/blockie/fonts/$f" || return 1
    done
    echo "build.sh: installed to $destdir$prefix"
}

fail=0
mkdir -p "$out" "$gen" "$depobj" "$genobj" "$appobj" "$scanobj" "$scraperobj" "$launchobj" "$testobj" "$shotobj" \
    "$imgobj" "$tsanobj"
case $target in
    all | generic | app | shot)
        deps
        gen_tokens
        wl_gen
        build_deps_obj
        ((fail)) && { echo "build.sh: dependency objects failed" >&2; exit 1; }
        ;;
    test | test-input | test-pad | test-portmsg | test-powermenu)
        gen_tokens
        wl_gen
        ;;
esac
[[ $target == generic || $target == all ]] && { build_generic || { echo "build.sh: generic profile failed" >&2; exit 1; }; }
[[ $target == app || $target == all ]] && { build_app || { echo "build.sh: app failed" >&2; exit 1; }; }
[[ $target == scan ]] && { build_scan || { echo "build.sh: scan failed" >&2; exit 1; }; }
[[ $target == scraper ]] && { build_scraper_test || { echo "build.sh: scraper failed" >&2; exit 1; }; }
[[ $target == test ]] && { build_test && build_scraper_test && build_launch_test && build_image_test && build_pad_test && build_wrap_test && build_zones_test && build_decodeq_test && build_input_test && build_gate_test && build_motion_test && build_bt_model_test && build_sysinfo_test && build_wifi_model_test && build_vtext_test && build_powermenu_test && build_portmsg_test || { echo "build.sh: test failed" >&2; exit 1; }; }
[[ $target == test-dbus ]] && { build_test_dbus || { echo "build.sh: test-dbus failed" >&2; exit 1; }; }
[[ $target == test-input ]] && { build_input_test || { echo "build.sh: test-input failed" >&2; exit 1; }; }
[[ $target == test-pad ]] && { build_pad_test || { echo "build.sh: test-pad failed" >&2; exit 1; }; }
[[ $target == test-portmsg ]] && { build_portmsg_test || { echo "build.sh: test-portmsg failed" >&2; exit 1; }; }
[[ $target == test-powermenu ]] && { build_powermenu_test || { echo "build.sh: test-powermenu failed" >&2; exit 1; }; }
[[ $target == test-tsan ]] && { build_tsan_test || { echo "build.sh: test-tsan failed" >&2; exit 1; }; }
[[ $target == install ]] && { build_install || { echo "build.sh: install failed" >&2; exit 1; }; }
[[ $target == shot ]] && { build_shot || { echo "build.sh: shot failed" >&2; exit 1; }; }
exit 0
