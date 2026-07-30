#!/bin/sh -e

# shellcheck disable=SC2086

if [ "${1:-}" != "--parsed" ]; then
    # Filter out lines starting with '+' followed by '[' or '[['.
    pattern="^\+ \[.+\]"
    status_file="${TMPDIR:-/tmp}/cecup-build-status.$$"

    trap 'rm -f "$status_file"' EXIT HUP INT TERM
    set +e
    {
        "$0" --parsed "$@" 2>&1 1>&3
        command_status=$?
        printf '%s\n' "$command_status" > "$status_file"
    } 3>&1 | grep -Ev "$pattern" >&2
    grep_status=$?
    set -e

    if [ ! -f "$status_file" ]; then
        exit "$grep_status"
    fi

    exit "$(cat "$status_file")"
fi
shift

set -e

error () {
    >&2 printf '%s\n' "$*"
    return
}

command_exists () {
    command -v "$1" > /dev/null 2>&1
}

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

if command_exists measure; then
    measure=$(command -v measure)
else
    measure=""
fi

if [ -n "$BASH_VERSION" ]; then
    # shellcheck disable=SC3044
    shopt -s expand_aliases
fi

# gtk might not work correctly if you have stuff here
export XDG_DATA_DIRS=""

# export LC_ALL=C

script_path=$0
case "$script_path" in
*/*)
    script_dir=${script_path%/*}
    ;;
*)
    script_dir=.
    ;;
esac

dir=$(CDPATH= cd "$script_dir" && pwd -P)
cbase="cbase"
CPPFLAGS="$CPPFLAGS -I$dir/$cbase"
CPPFLAGS="$CPPFLAGS -I."

cd "$dir" || exit
program=$(basename "$dir")
script=$(basename "$0")

LANGS="pt_BR"

. ./targets
target="${1:-build}"
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

main="main.c"
exe="bin/$program"
mkdir -p "$(dirname "$exe")"

if [ "$target" = "test" ] && [ -z "$CC" ] && command_exists tcc; then
    CC=tcc
else
    CC="${CC:-cc}"
fi

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

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
if [ "$target_os" != "Windows" ]; then
    CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=700"
fi
if [ "$target_os" = "Darwin" ]; then
    CPPFLAGS="$CPPFLAGS -D_DARWIN_C_SOURCE"
fi

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
CFLAGS="$CFLAGS -Wall -Wextra"
if [ -z "$NOCOLORS" ]; then
    CFLAGS="$CFLAGS -fdiagnostics-color=always"
fi
# CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-unknown-pragmas"
CFLAGS="$CFLAGS -Wno-format-security"
CFLAGS="$CFLAGS -Wno-undef"
CFLAGS="$CFLAGS -Wno-bad-function-cast"

cross_compile_only=0
if [ "$target" = "cross" ]; then
    case "$cross" in
    *macos*)
        cross_compile_only="${CROSS_COMPILE_ONLY:-1}"
        ;;
    esac
fi

PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
if [ "$cross_compile_only" = "1" ]; then
    GTK_CFLAGS="${GTK_CFLAGS:-}"
    GTK_LIBS="${GTK_LIBS:-}"
else
    if ! command_exists "$PKG_CONFIG"; then
        error "$PKG_CONFIG not found"
        exit 1
    fi
    GTK_CFLAGS="${GTK_CFLAGS:-$($PKG_CONFIG --cflags gtk4)}"
    GTK_LIBS="${GTK_LIBS:-$($PKG_CONFIG --libs gtk4)}"
fi
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

GNUSOURCE=""
if [ "$target_os" = "Linux" ]; then
    if uname -a 2> /dev/null | grep -q GNU; then
        GNUSOURCE="-D_GNU_SOURCE"
    fi
fi

option_remove() {
    remove=$2
    result=""

    for option in $1; do
        if [ "$option" = "$remove" ]; then
            continue
        fi

        if [ -z "$result" ]; then
            result=$option
        else
            result="$result $option"
        fi
    done

    printf '%s\n' "$result"
}

install_file() {
    mode=$1
    src=$2
    dst=$3
    dst_dir=$(dirname "$dst")

    mkdir -p "$dst_dir"
    install -m "$mode" "$src" "$dst"
}

install_dir() {
    mode=$1
    dst=$2

    mkdir -p "$dst"
    chmod "$mode" "$dst"
}

generate_welcome_h() {
    if [ -f "README.md" ]; then
        trace_on
        python3 build_welcome.py README.md src/.welcome.h
        trace_off
    fi
}

with_other () {
    compiler="$1"
    compiler_macro=$(echo "$compiler" | tr '[:lower:]' '[:upper:]')
    compiler_macro="__${compiler_macro}__"
    shift
    args="$*"
    trace_on
    while ! problem=$($compiler "-D${compiler_macro}" $args 2>&1); do
        trace_off
        problem=$(echo "$problem" | head -n 1 | tr -d "'")

        sleep 0.4
        if echo "$problem" | grep -Eq "unknown (argument|option)"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        elif echo "$problem" | grep -q "unknown file extension:"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        else
            printf "\n\nError compiling with $compiler:\n\n%s" "${problem}\n\n"
            return 1
        fi
        printf "\n"
        trace_on
    done
    return 0
}

case "$target" in
"debug")
    CFLAGS="$CFLAGS -g3 -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
"perf")
    CFLAGS="$CFLAGS -g -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto"
    fi
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    exe="bin/${program}_perf"
    ;;
"valgrind")
    CFLAGS="$CFLAGS -g3 -O0"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    ;;
"callgrind")
    CFLAGS="$CFLAGS -g3 -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    ;;
"test")
    CFLAGS="$CFLAGS -g3 $GNUSOURCE -DDEBUGGING=1 -fsanitize=undefined -Wno-address"
    ;;
"check")
    CC=gcc
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer -fdiagnostics-color=never"
    ;;
"build"|"run")
    CFLAGS="$CFLAGS $GNUSOURCE -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    ;;
"release")
    CFLAGS="$CFLAGS $GNUSOURCE -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    ;;
"fast_feedback")
    CC=clang
    CFLAGS="$CFLAGS $GNUSOURCE -Werror"
    ;;
"po")
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

cross_syntax_src=src/cross_syntax_check.c
if [ "$target" = "cross" ]; then
    if ! command_exists zig; then
        error "zig not found"
        exit 1
    fi
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"
    CFLAGS=$(option_remove "$CFLAGS" "-D_GNU_SOURCE")

    case "$cross" in
    *macos*)
        CFLAGS="$CFLAGS -fno-lto"
        ;;
    *windows*)
        exe="bin/$program.exe"
        ;;
    esac
fi

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-pedantic"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-reserved-identifier"  # because of __GTK_H_INSIDE__
    CFLAGS="$CFLAGS -Wno-documentation"
    CFLAGS="$CFLAGS -Wno-documentation-unknown-command"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
    CFLAGS="$CFLAGS -Wno-double-promotion"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-unknown-warning-option"
    CFLAGS="$CFLAGS -Wno-gnu-union-cast"
    CFLAGS="$CFLAGS -Wno-comma"
    CFLAGS="$CFLAGS -Wno-constant-logical-operand"
    CFLAGS="$CFLAGS -Wno-format-pedantic"
    CFLAGS="$CFLAGS -Wno-poison-system-directories"
    CFLAGS="$CFLAGS -Wno-allocator-wrappers"

    # to avoid using -Wno-unused-function
    CFLAGS="$CFLAGS -Wno-unneeded-internal-declaration"

    # only for the LSP. It does not understand unity builds
    CFLAGS="$CFLAGS -Wno-undefined-internal"
fi

case "$target" in
"fast_feedback")
    generate_welcome_h
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS && "$exe"
    trace_off
    ;;
"build"|"debug"|"run"|"release"|"valgrind"|"callgrind"|"perf"|"profile"|"cross")
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

    if command_exists ctags; then
        ctags --kinds-C=+l+d cbase/*.c src/*.h src/*.c 2> /dev/null || true
    fi
    if command_exists vtags.sed && [ -f tags ]; then
        vtags.sed tags | sort | uniq > .tags.vim 2> /dev/null || true
    fi
    if [ "$CC" = "chibicc" ]; then
        with_other chibicc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    elif [ "$CC" = "cproc" ]; then
        with_other cproc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    elif [ "$cross_compile_only" = "1" ]; then
        $measure $CC $CPPFLAGS $CFLAGS -fsyntax-only "$cross_syntax_src"
    else
        $measure $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS
    fi

    if [ "$target" = "debug" ]; then
        # reactivate fatal_warnings after solving:
        # (cecup_debug:9237):
        # Gtk-WARNING **:
        # Trying to snapshot GtkGizmo 0x555555c69fd0 without a current allocation

        # G_DEBUG=fatal_warnings \
            if command_exists gdb; then
                gdb "$exe" -ex run 2>&1 | tee "gdb_output_$(date +%s).txt"
            elif command_exists lldb; then
                lldb --one-line run "$exe" 2>&1 | tee "lldb_output_$(date +%s).txt"
            else
                "$exe"
            fi
    fi
    if [ "$target" = "run" ]; then
        "$exe"
    fi

    trace_off
    ;;
"install")
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
        install_dir 755 "$DESTDIR$SYSCONFDIR/$program"
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
"assembly")
    generate_welcome_h
    trace_on
    $CC $CPPFLAGS $CFLAGS -S $LDFLAGS -o ${program}_$CC.S "src/$main"
    trace_off
    exit
    ;;
"test")
    generate_welcome_h
    CECUP_TEST_TMPDIR="${CECUP_TEST_TMPDIR:-${TMPDIR:-/tmp}}"
    mkdir -p "$CECUP_TEST_TMPDIR"
    export CECUP_TEST_TMPDIR
    find . -iname "*.c" | sort | while read -r src; do
        trace_off
        name=$(basename "$src")

        if [ -n "$2" ] && [ "$name" != "$2" ]; then
            continue
        fi
        if [ "$name" = "$main" ]; then
            continue
        fi
        if echo "$src" | grep -q "stc/"; then
            continue
        fi

        name=$(echo "$name" | sed 's/\.c//')
        test_exe="$CECUP_TEST_TMPDIR/${name}_test"

        printf "\nTesting ${RED}${src}${RES} ...\n"

        flags="$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$src")"
        if [ "$src" = "src/windows_functions.c" ]; then
            if ! zig version; then
                continue
            fi
            CC="zig cc"
            cmdline="zig cc $CPPFLAGS $CFLAGS"
            cmdline=$(option_remove "$cmdline" "-D_GNU_SOURCE")
            cmdline="$cmdline -target x86_64-windows-gnu"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1"
            cmdline="$cmdline $flags -o $test_exe $src"
        else
            cmdline="$CC $CPPFLAGS $CFLAGS"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1 $LDFLAGS"
            cmdline="$cmdline $flags -o $test_exe $src"
        fi

        if [ "$CC" = "chibicc" ] || [ "$CC" = "cproc" ]; then
            cmdline_no_cc=$(option_remove "$cmdline" "$CC")
            trace_on
            if with_other "$CC" "$cmdline_no_cc"; then
                "$test_exe"
            else
                exit 1
            fi
        else
            trace_on
            if $cmdline; then
                if ! "$test_exe"; then
                    if command_exists gdb; then
                        gdb --quiet \
                            -ex run -ex backtrace -ex quit \
                            "$test_exe" 2>&1 || true
                    elif command_exists lldb; then
                        lldb \
                            --batch \
                            --one-line run \
                            --one-line bt \
                            "$test_exe" 2>&1 || true
                    fi
                    exit 1
                fi
            else
                exit 1
            fi
        fi
        trace_off
    done
    exit
    ;;
"uninstall")
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
"valgrind")
    vg_flags="$vg_flags --error-exitcode=1"
    vg_flags="$vg_flags --leak-check=no"
    # vg_flags="$vg_flags --show-leak-kinds=definite"
    # vg_flags="$vg_flags --errors-for-leak-kinds=definite"
    vg_flags="$vg_flags --track-origins=yes"
    # vg_flags="$vg_flags --suppressions=valgrind.supress"
    # vg_flags="$vg_flags --gen-suppressions=yes"
    vg_flags="$vg_flags --main-stacksize=18388608"

    trace_on
    G_DEBUG=gc-friendly G_SLICE=always-malloc \
        valgrind $vg_flags -s --tool=memcheck bin/$program 2>&1 \
        | tee "valgrind_output_$(date +%s).txt"
    trace_off
    exit
    ;;
"callgrind")
    out="callgrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=callgrind --callgrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
"cachegrind")
    out="cachegrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=cachegrind --cachegrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
"check")
    NOCOLORS=1 CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" ./build.sh
    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    NOCOLORS=1 CC=clang CFLAGS="$CFLAGS" ./build.sh
    exit
    ;;
"perf")
    trace_on
    perf record -F 999 -g --call-graph dwarf -o bin/perf.data "$exe"
    perf report -n -g --input bin/perf.data
    trace_off
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
