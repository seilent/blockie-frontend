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
destdir=${DESTDIR:-}
prefix=${PREFIX:-/usr}
target=all
for a in "$@"; do
    case $a in
        DESTDIR=*) destdir=${a#DESTDIR=} ;;
        PREFIX=*) prefix=${a#PREFIX=} ;;
        all | generic | app | scan | test | scraper | install | shot) target=$a ;;
        *) echo "build.sh: arg $a, want a target (all generic app scan test scraper install shot) or DESTDIR=/PREFIX=" >&2
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
fonts_dir=$out/fonts

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
' "$root/ui/tokens-blockie.txt" > "$gen/tokens-blockie.h.tmp"
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
}

cpp_extra() {
    case $1 in
        gfx/stb.cpp) echo "-w" ;;
        gfx/bidi.cpp) echo "-I$fb_dir/lib" ;;
        gfx/shape.cpp) echo "-I$hb_dir/src" ;;
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
    "$cxx" "${gobjs[@]}" "${dobjs[@]}" $appldflags -lpthread -lm -o "$out/generic/blockie-generic.tmp" || return 1
    mv "$out/generic/blockie-generic.tmp" "$out/generic/blockie-generic"
    echo "build.sh: $out/generic/blockie-generic"
}

build_app() {
    command -v wayland-scanner > /dev/null || { echo "build.sh: wayland-scanner not found" >&2; exit 1; }
    pkg-config --exists wayland-client || { echo "build.sh: wayland-client not found" >&2; exit 1; }
    build_sqlite_obj || return 1
    mkdir -p "$gen"
    fetch "$xdg_url" "$cache/xdg-shell-1.41.xml" "$xdg_sha"
    wayland-scanner client-header "$cache/xdg-shell-1.41.xml" "$gen/xdg-shell-client-protocol.h"
    wayland-scanner private-code "$cache/xdg-shell-1.41.xml" "$gen/xdg-shell-protocol.c"
    pkg-config --exists libcurl || { echo "build.sh: libcurl not found" >&2; return 1; }
    local wl_cflags wl_libs xkb_cflags xkb_libs curl_cflags curl_libs
    wl_cflags=$(pkg-config --cflags wayland-client)
    wl_libs=$(pkg-config --libs wayland-client)
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
    toolver=$("$cc" --version | head -n 1)
    pool "$appobj/xdg-shell-protocol.o" "$cc" -O2 -w $wl_cflags $appcflags "$gen/xdg-shell-protocol.c"
    drain
    toolver=$("$cxx" --version | head -n 1)
    local s extra
    local -a gobjs=()
    while IFS= read -r s; do
        extra=$(cpp_extra "$s")
        pool "$genobj/${s//\//_}.o" "$cxx" "${gflags[@]}" $extra $appcflags "$root/$s"
        gobjs+=("$genobj/${s//\//_}.o")
    done < <(generic_srcs)
    pool "$appobj/screen_selector.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/screen_selector.cpp"
    pool "$appobj/text_wrap.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/text_wrap.cpp"
    pool "$appobj/library.o" "$cxx" "${scanflags[@]}" $appcflags "$root/blockie/library.cpp"
    pool "$appobj/image.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$appobj/input_pad.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/input_pad.cpp"
    pool "$appobj/repeat.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/repeat.cpp"
    pool "$appobj/nav.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/nav.cpp"
    pool "$appobj/launch.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/launch.cpp"
    pool "$appobj/scraper.o" "$cxx" "${scanflags[@]}" $curl_cflags $appcflags "$root/blockie/scraper.cpp"
    pool "$appobj/present_wl.o" "$cxx" "${gflags[@]}" $wl_cflags $xkb_cflags $appcflags -I"$gen" \
        "$root/blockie/present_wl.cpp"
    pool "$appobj/main.o" "$cxx" "${gflags[@]}" -pthread $wl_cflags $xkb_cflags $appcflags -I"$gen" "$root/blockie/main.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/app"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "$appobj/screen_selector.o" "$appobj/text_wrap.o" "$appobj/library.o" "$appobj/image.o" \
        "$appobj/input_pad.o" "$appobj/repeat.o" "$appobj/nav.o" "$appobj/launch.o" "$appobj/scraper.o" \
        "$appobj/present_wl.o" "$appobj/main.o" \
        "$appobj/xdg-shell-protocol.o" "$depobj/sqlite3.o" "${dobjs[@]}" $appldflags $wl_libs $xkb_libs $curl_libs \
        -lpthread -lm -o "$out/app/blockie-frontend.tmp" || return 1
    mv "$out/app/blockie-frontend.tmp" "$out/app/blockie-frontend"
    echo "build.sh: $out/app/blockie-frontend"
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
    pool "$testobj/launch_test.o" "$cxx" "${launchflags[@]}" $appcflags "$root/blockie/tests/launch_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/launch"
    "$cxx" "$launchobj/launch.o" "$testobj/launch_test.o" $appldflags -o "$out/launch/blockie-launch-test.tmp" || return 1
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

imgflags=(-std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers "${img_inc[@]}" -DSPNG_STATIC)

build_image_test() {
    deps_img
    toolver=$("$cc" --version | head -n 1)
    local c
    for c in "${zlib_c[@]}"; do
        pool "$depobj/zlib_$c.o" "$cc" "${cflags[@]}" $dep_harden "$zlib_dir/$c.c"
    done
    pool "$depobj/spng.o" "$cc" "${cflags[@]}" $dep_harden "$spng_dir/spng/spng.c"
    drain
    ((fail)) && return 1
    toolver=$("$cxx" --version | head -n 1)
    pool "$imgobj/image.o" "$cxx" "${imgflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$testobj/image_test.o" "$cxx" "${imgflags[@]}" $appcflags "$root/blockie/tests/image_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/image"
    "$cxx" "$imgobj/image.o" "$testobj/image_test.o" "$depobj/spng.o" "$depobj"/zlib_*.o $appldflags -lpthread -lm \
        -o "$out/image/blockie-image-test.tmp" || return 1
    mv "$out/image/blockie-image-test.tmp" "$out/image/blockie-image-test"
    echo "build.sh: $out/image/blockie-image-test"
    "$out/image/blockie-image-test"
}

build_input_test() {
    pool "$testobj/input_pad.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/input_pad.cpp"
    pool "$testobj/repeat.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/repeat.cpp"
    pool "$testobj/nav.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/nav.cpp"
    pool "$testobj/repeat_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/repeat_test.cpp"
    pool "$testobj/nav_test.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/tests/nav_test.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/input"
    "$cxx" "$testobj/input_pad.o" "$testobj/repeat.o" "$testobj/repeat_test.o" $appldflags \
        -o "$out/input/blockie-repeat-test.tmp" || return 1
    mv "$out/input/blockie-repeat-test.tmp" "$out/input/blockie-repeat-test"
    "$cxx" "$testobj/input_pad.o" "$testobj/nav.o" "$testobj/nav_test.o" $appldflags \
        -o "$out/input/blockie-nav-test.tmp" || return 1
    mv "$out/input/blockie-nav-test.tmp" "$out/input/blockie-nav-test"
    echo "build.sh: $out/input/blockie-repeat-test $out/input/blockie-nav-test"
    "$out/input/blockie-repeat-test" && "$out/input/blockie-nav-test"
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

build_shot() {
    deps_fonts
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
    pool "$shotobj/image.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/image.cpp"
    pool "$shotobj/shot.o" "$cxx" "${gflags[@]}" $appcflags "$root/blockie/shot.cpp"
    drain
    ((fail)) && return 1
    mkdir -p "$out/shot"
    local -a dobjs=()
    mapfile -t dobjs < <(dep_objs)
    "$cxx" "${gobjs[@]}" "$shotobj/screen_selector.o" "$shotobj/text_wrap.o" "$shotobj/image.o" "$shotobj/shot.o" \
        "${dobjs[@]}" $appldflags -lpthread -lm -o "$out/shot/blockie-shot.tmp" || return 1
    mv "$out/shot/blockie-shot.tmp" "$out/shot/blockie-shot"
    echo "build.sh: $out/shot/blockie-shot"
    echo "build.sh: fonts $fonts_dir"
}

build_install() {
    [[ -x $out/app/blockie-frontend ]] || { echo "build.sh: run 'build.sh app' before install" >&2; return 1; }
    deps_fonts
    install -Dm755 "$out/app/blockie-frontend" "$destdir$prefix/libexec/blockie/blockie-frontend" || return 1
    install -Dm644 "$root/dist/blockie-frontend.service" "$destdir$prefix/lib/systemd/user/blockie-frontend.service" ||
        return 1
    install -Dm644 "$root/LICENSE" "$destdir$prefix/share/licenses/blockie-frontend/LICENSE" || return 1
    local f
    for f in selawkl.ttf selawk.ttf selawkb.ttf selawksl.ttf; do
        install -Dm644 "$fonts_dir/$f" "$destdir$prefix/share/blockie/fonts/$f" || return 1
    done
    echo "build.sh: installed to $destdir$prefix"
}

fail=0
mkdir -p "$out" "$gen" "$depobj" "$genobj" "$appobj" "$scanobj" "$scraperobj" "$launchobj" "$testobj" "$shotobj" \
    "$imgobj"
case $target in
    all | generic | app | shot)
        deps
        gen_tokens
        build_deps_obj
        ((fail)) && { echo "build.sh: dependency objects failed" >&2; exit 1; }
        ;;
esac
[[ $target == generic || $target == all ]] && { build_generic || { echo "build.sh: generic profile failed" >&2; exit 1; }; }
[[ $target == app || $target == all ]] && { build_app || { echo "build.sh: app failed" >&2; exit 1; }; }
[[ $target == scan ]] && { build_scan || { echo "build.sh: scan failed" >&2; exit 1; }; }
[[ $target == scraper ]] && { build_scraper_test || { echo "build.sh: scraper failed" >&2; exit 1; }; }
[[ $target == test ]] && { build_test && build_scraper_test && build_launch_test && build_image_test && build_pad_test && build_wrap_test && build_input_test || { echo "build.sh: test failed" >&2; exit 1; }; }
[[ $target == install ]] && { build_install || { echo "build.sh: install failed" >&2; exit 1; }; }
[[ $target == shot ]] && { build_shot || { echo "build.sh: shot failed" >&2; exit 1; }; }
exit 0
