#!/bin/sh -e

# shellcheck disable=SC2086

set -e
alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

program=$(basename "$(readlink -f "$(dirname "$0")")")
script=$(basename "$0")

LANGS="pt_BR"

. ./targets
target="${1:-build}"

if ! grep -q "$target" ./targets; then
    echo "usage: $script <targets>"
    cat targets
    exit 1
fi

printf "\n${script} ${RED}${1} ${2}$RES\n"
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"

main="main.c"
exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CPPFLAGS="$CPPFLAGS -DGETTEXT_PACKAGE=\"$program\""
CPPFLAGS="$CPPFLAGS -DLOCALEDIR=\"$PREFIX/share/locale\""

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wno-format-pedantic"
CFLAGS="$CFLAGS -Wno-unknown-warning-option"
CFLAGS="$CFLAGS -Wno-gnu-union-cast"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-undefined-internal"
CFLAGS="$CFLAGS -Wno-discarded-qualifiers"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"

LDFLAGS="$LDFLAGS $(pkg-config --cflags --libs gtk+-3.0) -lpthread"
OS=$(uname -a)

CC="${CC:-cc}"
if echo "$OS" | grep -q "Linux"; then
    if echo "$OS" | grep -q "GNU"; then
        GNUSOURCE="-D_GNU_SOURCE"
    fi
fi

option_remove() {
    echo "$1" | sed "s/$2//g"
}

case "$target" in
"debug")
    CFLAGS="$CFLAGS -g -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
"benchmark")
    CFLAGS="$CFLAGS -O2 -flto -march=native -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DBRN2_BENCHMARK=1"
    exe="bin/${program}_benchmark"
    ;;
"perf")
    CFLAGS="$CFLAGS -g3 -Og -flto"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DBRN2_BENCHMARK=1"
    exe="bin/${program}_perf"
    ;;
"valgrind") 
    CFLAGS="$CFLAGS -g -O0 -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    ;;
"test")
    CFLAGS="$CFLAGS -g $GNUSOURCE -DDEBUGGING=1 -fsanitize=undefined"
    ;;
"check") 
    CC=gcc
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer"
    ;;
"build") 
    CFLAGS="$CFLAGS $GNUSOURCE -O2 -flto -march=native -ftree-vectorize"
    ;;
"po")
    mkdir -p po

    xgettext \
        --keyword=_ \
        --keyword=N_ \
        --language=C \
        --add-comments \
        --sort-output \
        --from-code=UTF-8 \
        -o po/${program}.pot \
        ./*.c ./*.h

    for lang in $LANGS; do
        if [ -f "po/$lang.po" ]; then
            msgmerge -U "po/$lang.po" po/${program}.pot
        else
            msginit -l "$lang" -i po/${program}.pot -o "po/$lang.po" --no-translator
        fi
    done
    trace_off
    exit
    ;;
*)
    CFLAGS="$CFLAGS -O2"
    ;;
esac

if [ "$target" = "cross" ]; then
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"
    CFLAGS=$(option_remove "$CFLAGS" "-D_GNU_SOURCE")

    case $cross in
    "x86_64-macos"|"aarch64-macos")
        CFLAGS="$CFLAGS -fno-lto"
        LDFLAGS="$LDFLAGS -lpthread"
        ;;
    *windows*)
        exe="bin/$program.exe"
        ;;
    *)
        LDFLAGS="$LDFLAGS -lpthread"
        ;;
    esac
else
    LDFLAGS="$LDFLAGS -lpthread"
fi

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-ignored-attributes"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-reserved-identifier"
    CFLAGS="$CFLAGS -Wno-documentation-unknown-command"
    CFLAGS="$CFLAGS -Wno-documentation"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-pedantic"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
fi

case "$target" in
"build"|"debug"|"valgrind")
    trace_on

    if [ ! -d "po" ]; then
        return
    fi
    for lang in $LANGS; do
        if [ -f "po/$lang.po" ]; then
            mkdir -p "po/$lang/LC_MESSAGES"
            msgfmt "po/$lang.po" -o "po/$lang/LC_MESSAGES/$program.mo"
        fi
    done

    ctags --kinds-C=+l+d ./*.h ./*.c         2> /dev/null || true
    vtags.sed tags | sort | uniq > .tags.vim 2> /dev/null || true
    $CC $CPPFLAGS $CFLAGS main.c -o "$exe" $LDFLAGS

    if [ $target = "debug" ]; then
        gdb $exe -ex run 2>&1 | tee "gdb_output_$(date +%s).txt"
    fi

    trace_off
    ;;
"install")
    trace_on
    if [ ! -f "bin/$program" ]; then
        $0 build
    fi
    install -Dm755 bin/${program}   ${DESTDIR}${PREFIX}/bin/${program}
    install -Dm644 ${program}.1     ${DESTDIR}${PREFIX}/man/man1/${program}.1
    
    for lang in $LANGS; do
        if [ -f "po/$lang/LC_MESSAGES/$program.mo" ]; then
            install -Dm644 "po/$lang/LC_MESSAGES/$program.mo" \
                "${DESTDIR}${PREFIX}/share/locale/$lang/LC_MESSAGES/$program.mo"
        fi
    done

    if [ -d "etc" ]; then
        install -dm755 "$DESTDIR/etc/$program"
        cp -rp etc/* "$DESTDIR/etc/$program/"
    fi
    if [ -f "$program.desktop" ]; then
        install -Dm644 \
            "$program.desktop" \
            "$DESTDIR/usr/share/applications/$program.desktop"
    fi
    trace_off
    exit
    ;;
"uninstall")
    rm -vf  "${DESTDIR}${PREFIX}/bin/${program}"
    rm -vf  "${DESTDIR}${PREFIX}/man/man1/${program}.1"
    for lang in $LANGS; do
        rm -vf "${DESTDIR}${PREFIX}/share/locale/$lang/LC_MESSAGES/$program.mo"
    done
    rm -rvf "$DESTDIR/etc/$program/"
    rm -vf  "$DESTDIR/usr/share/applications/$program.desktop"
    exit
    ;;
"assembly")
    trace_on
    $CC $CPPFLAGS $CFLAGS -S $LDFLAGS -o ${program}_$CC.S "$main"
    trace_off
    exit
    ;;
"test")
    for src in *.c; do
        if [ -n "$2" ] && [ $src != "$2" ]; then
            continue
        fi
        if [ "$src" = "$main" ]; then
            continue
        fi
        printf "\nTesting ${RED}${src}${RES} ...\n"
        name="$(echo "$src" | sed 's/\.c//g')"

        flags="$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$src")"
        if [ $src = "windows_functions.c" ]; then
            if ! zig version; then
                continue
            fi
            cmdline="zig cc $CPPFLAGS $CFLAGS"
            cmdline=$(option_remove "$cmdline" "-D_GNU_SOURCE")
            cmdline="$cmdline -target x86_64-windows-gnu"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1"
            cmdline="$cmdline $flags -o /tmp/$src.exe $src"
        else
            cmdline="$CC $CPPFLAGS $CFLAGS -Wno-unused-variable -DTESTING_$name=1 $LDFLAGS"
            cmdline="$cmdline $flags -o /tmp/$src.exe $src"
        fi

        trace_on
        if $cmdline; then
            /tmp/$src.exe || gdb /tmp/$src.exe -ex run
        else
            exit 1
        fi
        trace_off
    done
    exit
    ;;
esac

case "$target" in
"valgrind")
    vg_flags="$vg_flags --leak-check=full"
    # vg_flags="--error-exitcode=1 --errors-for-leak-kinds=all"
    # vg_flags="$vg_flags --leak-check=full --show-leak-kinds=all"
    # vg_flags="$vg_flags --track-origins=yes"
    trace_on
    valgrind $vg_flags -s --tool=memcheck bin/$program 2>&1 \
        | tee "valgrind_output_$(date +%s).txt"
    trace_off
    exit
    ;;
"check")
    CC=gcc CFLAGS="-fanalyzer" ./build.sh
    scan-build --view -analyze-headers --status-bugs ./build.sh
    exit
    ;;
esac

trace_off
if [ "$target" = "test_all" ]; then
    printf '%s\n' "$targets" | while IFS= read -r target; do
        echo "$target" | grep -Eq "^(# |$)" && continue
        if echo "$target" | grep "cross"; then
            $0 $target
            continue
        fi
        for compiler in gcc tcc clang "zig cc" ; do
            CC=$compiler $0 $target || exit
        done
    done
fi
