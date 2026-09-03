#!/bin/sh -e

# shellcheck disable=SC2086,SC2089,SC2090

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. "./cbase/common.sh"

# gtk might not work correctly if you have stuff here
export XDG_DATA_DIRS=""

# export LC_ALL=C

cd "$dir" || exit
program=$(common_get_program "$0")
script=$(basename "$0")

LANGS="pt_BR"

common_build_parse_args "$@"

case "$mode" in
build|cachegrind|callgrind|check|cross|debug|debug-fast|fast_feedback|install|po|profile|run|test|test_all|uninstall|valgrind)
    ;;
*)
    common_build_unknown_mode
    ;;
esac
cross="$target"

common_build_print_invocation "$script"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
BINDIR="${BINDIR:-$PREFIX/bin}"
MANDIR="${MANDIR:-$PREFIX/man}"
DATADIR="${DATADIR:-$PREFIX/share}"
SYSCONFDIR="${SYSCONFDIR:-}"
APPDIR="${APPDIR:-}"

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CC=$(common_get_compiler "$mode")

host_os=$(uname -s 2> /dev/null || printf unknown)
target_os=$host_os
if [ "$mode" = "cross" ]; then
    case "$cross" in
    *macos*)
        target_os=Darwin
        ;;
    *windows*)
        target_os=Windows
        ;;
    *freebsd*)
        target_os=FreeBSD
        ;;
    *netbsd*)
        target_os=NetBSD
        ;;
    *linux*)
        target_os=Linux
        ;;
    esac
fi

CPPFLAGS="$CPPFLAGS -I$dir/cbase"
CPPFLAGS="$CPPFLAGS -I."

if [ -z "$SYSCONFDIR" ]; then
    case "$target_os" in
    Darwin|FreeBSD|NetBSD|OpenBSD|DragonFly)
        SYSCONFDIR="$PREFIX/etc"
        ;;
    *)
        SYSCONFDIR="/etc"
        ;;
    esac
fi
if [ -z "$APPDIR" ]; then
    APPDIR="$DATADIR/applications"
fi

CPPFLAGS="$CPPFLAGS -DGETTEXT_PACKAGE=$program"
CPPFLAGS="$CPPFLAGS -DLOCALEDIR=\"$DATADIR/locale\""
CPPFLAGS="$CPPFLAGS -DCECUP_SYSTEM_CONFIG_DIR=\"$SYSCONFDIR/$program\""

CFLAGS="$CFLAGS -std=c11"
if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -ferror-limit=1"
else
    CFLAGS="$CFLAGS -fmax-errors=1"
fi

PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
if ! common_command_exists "$PKG_CONFIG"; then
    error "$PKG_CONFIG not found"
    exit 1
fi
GTK_INCLUDES=$($PKG_CONFIG --cflags-only-I gtk4 | sed 's/-I/-isystem /g')
GTK_OTHER_CFLAGS=$($PKG_CONFIG --cflags-only-other gtk4)
GTK_CFLAGS="${GTK_CFLAGS:-$GTK_INCLUDES $GTK_OTHER_CFLAGS}"
GTK_LIBS="${GTK_LIBS:-$($PKG_CONFIG --libs gtk4)}"
CFLAGS="$CFLAGS $GTK_CFLAGS"
LDFLAGS="$LDFLAGS $GTK_LIBS"

case "$target_os" in
Darwin)
    ;;
Windows)
    LDFLAGS="$LDFLAGS -lpthread"
    ;;
*)
    CFLAGS="$CFLAGS -pthread"
    LDFLAGS="$LDFLAGS -pthread"
    ;;
esac
LDFLAGS="$LDFLAGS -lm"

generate_welcome_h() {
    if [ -f "README.md" ]; then
        trace_on
        sh build_welcome.sh README.md src/.welcome.h
        trace_off
    fi
}

case "$mode" in
debug)
    CFLAGS="$CFLAGS -g3"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/$program"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -g2 -O2"
    CFLAGS="$CFLAGS -fsanitize=undefined"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    ;;
valgrind)
    CFLAGS="$CFLAGS -g3 -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
test)
    CFLAGS="$CFLAGS -g3 -DDEBUGGING=1"
    ;;
check)
    CFLAGS="$CFLAGS -DDEBUGGING=1 -fanalyzer -fdiagnostics-color=never"
    ;;
build|run)
    CFLAGS="$CFLAGS -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    ;;
fast_feedback)
    ;;
po)
    generate_welcome_h
    mkdir -p po

    xgettext \
        --keyword=_ \
        --keyword=N_ \
        --language=C \
        --from-code=UTF-8 \
        --output po/${program}.pot \
        src/*.c src/*.h

    for lang in $LANGS; do
        if [ -f "po/$lang.po" ]; then
            msgmerge --update "po/$lang.po" po/${program}.pot
        else
            msginit \
                --locale "$lang" \
                --input po/${program}.pot \
                --output "po/$lang.po" \
                 --no-translator
        fi
    done
    trace_off
    exit
    ;;
cross)
    common_build_cross_all
    CFLAGS="$CFLAGS -O2"
    ;;
profile)
    CFLAGS="$CFLAGS -O2"
    ;;
build|cachegrind|callgrind|check|cross|debug|debug-fast|fast_feedback|install|po|profile|run|test|test_all|uninstall|valgrind)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

if [ "$mode" = "cross" ]; then
    if ! common_command_exists zig; then
        error "zig not found"
        exit 1
    fi
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"

    case "$cross" in
    *windows*)
        exe="bin/$program.exe"
        ;;
    esac
fi

case "$mode" in
fast_feedback)
    generate_welcome_h
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS && "$exe"
    trace_off
    ;;
build|debug|debug-fast|run|callgrind|profile|cross|valgrind)
    generate_welcome_h

    if [ -d "po" ]; then
        if common_command_exists msgfmt; then
            for lang in $LANGS; do
                if [ -f "po/$lang.po" ]; then
                    mkdir -p "po/$lang/LC_MESSAGES"
                    msgfmt "po/$lang.po" -o "po/$lang/LC_MESSAGES/$program.mo"
                fi
            done
        else
            printf '%s\n' "msgfmt not found; skipping message catalogs"
        fi
    fi

    common_build_tags

    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS

    if [ "$mode" = "run" ]; then
        "$exe"
    fi

    trace_off
    ;;
install)
    trace_on
    $0 build
    common_install_file 755 "bin/${program}" "${DESTDIR}${BINDIR}/${program}"
    common_install_file 644 "${program}.1" "${DESTDIR}${MANDIR}/man1/${program}.1"

    for lang in $LANGS; do
        if [ -f "po/$lang/LC_MESSAGES/$program.mo" ]; then
            common_install_file \
                644 \
                "po/$lang/LC_MESSAGES/$program.mo" \
                "${DESTDIR}${DATADIR}/locale/$lang/LC_MESSAGES/$program.mo"
        fi
    done

    if [ -d "etc" ]; then
        mkdir -p "$DESTDIR$SYSCONFDIR/$program"
        chmod 755 "$DESTDIR$SYSCONFDIR/$program"
        for file in etc/*; do
            if [ -f "$file" ]; then
                common_install_file \
                    644 \
                    "$file" \
                    "$DESTDIR$SYSCONFDIR/$program/$(basename "$file")"
            fi
        done
    fi
    if [ "$target_os" != "Darwin" ] && [ -f "$program.desktop" ]; then
        common_install_file \
            644 \
            "$program.desktop" \
            "$DESTDIR$APPDIR/$program.desktop"
    fi
    trace_off
    exit
    ;;
test)
    generate_welcome_h
    CECUP_TEST_TMPDIR="${CECUP_TEST_TMPDIR:-${TMPDIR:-/tmp}}"
    mkdir -p "$CECUP_TEST_TMPDIR"
    export CECUP_TEST_TMPDIR

    TEST_TMPDIR="$CECUP_TEST_TMPDIR" common_test "$target"
    exit
    ;;
uninstall)
    rm -vf  "${DESTDIR}${BINDIR}/${program:?}"
    rm -vf  "${DESTDIR}${MANDIR}/man1/${program:?}.1"
    for lang in $LANGS; do
        rm -vf "${DESTDIR}${DATADIR}/locale/$lang/LC_MESSAGES/$program.mo"
    done
    rm -rvf "$DESTDIR$SYSCONFDIR/${program:?}/"
    rm -vf  "$DESTDIR$APPDIR/${program:?}.desktop"
    exit
    ;;
esac

case "$mode" in
callgrind)
    out="callgrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=callgrind --callgrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
cachegrind)
    out="cachegrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=cachegrind --cachegrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
valgrind)
    trace_on
    valgrind --tool=memcheck bin/$program
    trace_off
    exit
    ;;
check)
    set +e

    CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" ./build.sh

    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    CC=clang CFLAGS="$CFLAGS" ./build.sh

    echo "static analysis finished."
    exit
    ;;
esac

trace_off
if [ "$mode" = "test_all" ]; then
    common_build_test_all "debug build test" gcc tcc clang "zig cc"
fi
