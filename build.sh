#!/bin/sh -e

# shellcheck disable=SC2086,SC2089,SC2090

dir=$(dirname "$(readlink -f "$0")")
# shellcheck source=/dev/null
. "$dir/cbase/common.sh"

error () {
    >&2 printf '%s\n' "$*"
    return
}

# gtk might not work correctly if you have stuff here
export XDG_DATA_DIRS=""

# export LC_ALL=C

cd "$dir" || exit
program=$(get_program "$0")
script=$(basename "$0")

LANGS="pt_BR"

if [ -f ./targets ]; then
    . ./targets
else
    targets=$(cat <<'EOF_TARGETS'
build
debug
fast_feedback
install
uninstall
test
check
release
run
po
profile
callgrind
cachegrind
test_all
cross x86_64-linux
cross aarch64-linux
cross x86_64-macos
cross aarch64-macos
cross x86_64-windows-gnu
EOF_TARGETS
)
fi
target="${1:-debug}"
cross="${2:-}"

target_line=$target
if [ "$target" = "cross" ]; then
    target_line="$target $cross"
fi
if ! printf '%s\n' "$targets" | awk -v wanted="$target_line" '
    {
        line = $0
        sub(/^# /, "", line)
    }
    line == wanted { found = 1 }
    END { exit !found }
'; then
    echo "usage: $script <targets>"
    cat targets
    exit 1
fi

printf "\n${script} ${RED}${1:-} ${2:-}$RES\n"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
BINDIR="${BINDIR:-$PREFIX/bin}"
MANDIR="${MANDIR:-$PREFIX/man}"
DATADIR="${DATADIR:-$PREFIX/share}"
SYSCONFDIR="${SYSCONFDIR:-}"
APPDIR="${APPDIR:-}"

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CC=$(get_compiler "$target")

host_os=$(uname -s 2> /dev/null || printf unknown)
target_os=$host_os
if [ "$target" = "cross" ]; then
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
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Werror=all -Werror=extra"
# CFLAGS="$CFLAGS -Werror"  # Only uncomment occasionally, keep this line
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-unused-function"

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-cast-align"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-cast-qual"
    CFLAGS="$CFLAGS -Wno-comma"
    CFLAGS="$CFLAGS -Wno-constant-logical-operand"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-float-equal"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-pedantic"  # because of gpointer casting
    CFLAGS="$CFLAGS -Wno-poison-system-directories"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-unused-macros"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"

    # only for the LSP and unity-test builds. They include helper functions
    # that are intentionally unused in some translation units.
    CFLAGS="$CFLAGS -Wno-unneeded-internal-declaration"
    CFLAGS="$CFLAGS -Wno-undefined-internal"
fi

if [ -z "$NOCOLORS" ]; then
    CFLAGS="$CFLAGS -fdiagnostics-color=always"
fi

PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
if ! command_exists "$PKG_CONFIG"; then
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
        python3 build_welcome.py README.md src/.welcome.h
        trace_off
    fi
}

case "$target" in
debug)
    CFLAGS="$CFLAGS -g3 -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    ;;
test)
    CFLAGS="$CFLAGS -g3 -DDEBUGGING=1 -fsanitize=undefined"
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
release)
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
*)
    CFLAGS="$CFLAGS -O2"
    ;;
esac

if [ "$target" = "cross" ]; then
    if ! command_exists zig; then
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

case "$target" in
fast_feedback)
    generate_welcome_h
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS && "$exe"
    trace_off
    ;;
build|debug|run|release|callgrind|profile|cross)
    generate_welcome_h
    trace_on

    if [ -d "po" ]; then
        if command_exists msgfmt; then
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

    build_tags
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS

    if [ "$target" = "run" ]; then
        "$exe"
    fi

    trace_off
    ;;
install)
    trace_on
    $0 release
    install_file 755 "bin/${program}" "${DESTDIR}${BINDIR}/${program}"
    install_file 644 "${program}.1" "${DESTDIR}${MANDIR}/man1/${program}.1"

    for lang in $LANGS; do
        if [ -f "po/$lang/LC_MESSAGES/$program.mo" ]; then
            install_file \
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
                install_file \
                    644 \
                    "$file" \
                    "$DESTDIR$SYSCONFDIR/$program/$(basename "$file")"
            fi
        done
    fi
    if [ "$target_os" != "Darwin" ] && [ -f "$program.desktop" ]; then
        install_file \
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

    TEST_TMPDIR="$CECUP_TEST_TMPDIR" test "$2"
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

case "$target" in
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
check)
    set +e

    NOCOLORS=1 CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" ./build.sh

    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    NOCOLORS=1 CC=clang CFLAGS="$CFLAGS" ./build.sh

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
