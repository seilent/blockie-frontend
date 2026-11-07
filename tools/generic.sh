#!/usr/bin/env bash
set -euo pipefail

(($# >= 2)) || { echo "usage: generic.sh PLAYER_DIR OUT_DIR [-I DIR ...]" >&2; exit 2; }

norm() {
    local p=$1 part parts out=()
    [[ $p == /* ]] || p=$PWD/$p
    IFS=/ read -r -a parts <<< "$p"
    for part in "${parts[@]}"; do
        case $part in
            "" | .) ;;
            ..) out=("${out[@]:0:${#out[@]}-1}") ;;
            *) out+=("$part") ;;
        esac
    done
    local IFS=/
    n="/${out[*]}"
}

norm "$1"
top=$n
norm "$2"
gen=$n
shift 2
inc=()
while (($#)); do
    case $1 in
        -I) (($# >= 2)) || { echo "generic.sh: -I wants a dir" >&2; exit 2; }
            norm "$2"; inc+=("$n"); shift 2 ;;
        -I*) norm "${1#-I}"; inc+=("$n"); shift ;;
        *) echo "generic.sh: $1, want -I DIR" >&2; exit 2 ;;
    esac
done
[[ -f $top/generic.txt ]] || { echo "generic.sh: no $top/generic.txt" >&2; exit 2; }

rel() {
    if [[ $1 == "$gen"/* ]]; then
        r=${1#"$gen"/}
    elif [[ $1 == "$top"/* ]]; then
        r=${1#"$top"/}
    else
        r=
        return 1
    fi
}

declare -A ok
list=()
fail=0
while IFS= read -r f || [[ -n $f ]]; do
    [[ -n $f ]] || continue
    list+=("$f")
    ok[$f]=1
    [[ -f $top/$f ]] || { echo "generic.sh: $f is listed but missing" >&2; fail=1; }
done < "$top/generic.txt"
ok[board/board.h]=1
ok[ui/tokens.h]=1

re='^[[:space:]]*#[[:space:]]*include[[:space:]]*([<"])([^>"]+)[>"]'
for f in "${list[@]}"; do
    [[ -f $top/$f ]] || continue
    dir=$top
    [[ $f == */* ]] && dir=$top/${f%/*}
    while IFS=: read -r no line; do
        [[ $line =~ $re ]] || continue
        kind=${BASH_REMATCH[1]} name=${BASH_REMATCH[2]} hit=
        dirs=("${inc[@]}")
        [[ $kind == '"' ]] && dirs=("$dir" "${inc[@]}")
        for d in "${dirs[@]}"; do
            if [[ -f $d/$name ]]; then
                norm "$d/$name"
                hit=$n
                break
            fi
        done
        if [[ -z $hit ]]; then
            [[ $kind == '"' ]] && { echo "generic.sh: $f:$no includes $name" >&2; fail=1; }
            continue
        fi
        rel "$hit" || continue
        [[ -n ${ok[$r]:-} ]] || { echo "generic.sh: $f:$no includes $r" >&2; fail=1; }
    done < <(grep -n -E '^[[:space:]]*#[[:space:]]*include' "$top/$f" || true)
done

[[ -f $top/tools/generic.cpp ]] || { echo "generic.sh: no tools/generic.cpp" >&2; exit 1; }
declare -A want got
for f in "${list[@]}"; do
    [[ $f == *.h ]] && want[$f]=1
done
want[gfx/present.h]=1
while IFS= read -r line || [[ -n $line ]]; do
    [[ $line =~ $re && ${BASH_REMATCH[1]} == '"' ]] || continue
    norm "$top/tools/${BASH_REMATCH[2]}"
    rel "$n" || r=${BASH_REMATCH[2]}
    got[$r]=1
    [[ -n ${want[$r]:-} ]] || { echo "generic.sh: tools/generic.cpp includes $r" >&2; fail=1; }
done < "$top/tools/generic.cpp"
for f in "${list[@]}" gfx/present.h; do
    [[ -z ${want[$f]:-} || -n ${got[$f]:-} ]] || { echo "generic.sh: tools/generic.cpp misses $f" >&2; fail=1; }
done
exit "$fail"
