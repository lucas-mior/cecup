#!/bin/sh -e

# shellcheck disable=SC2086

if [ "${1:-}" != "--parsed" ]; then
    # Filter out lines starting with '+' followed by '[' or '[['
    pattern="^\+ \[.+\]"

    # 1. Open FD 3 and point it to the current stdout (1).
    # 2. Redirect the command's stderr (2) to stdout (1) so it enters the pipe.
    # 3. Redirect the command's stdout (1) to FD 3 so it bypasses the pipe.
    # 4. The pipe now only contains the original stderr.
    # 5. grep filters the stream and sends it back to stderr (2).
    { "$0" --parsed "$@" 2>&1 1>&3 | grep -Ev "$pattern" >&2; } 3>&1

    # Note: In POSIX, $? here will be the exit code of grep, not $0.
    exit $?
fi
shift

set -e

error () {
    >&2 printf "$@"
    return
}

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

if command -v measure; then
    measure=measure
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

dir=$(dirname "$(readlink -f "$0")")
cbase="cbase"
CPPFLAGS="$CPPFLAGS -I$dir/$cbase"
CPPFLAGS="$CPPFLAGS -I."

cd "$dir" || exit
program=$(basename "$(readlink -f "$dir")")
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
CPPFLAGS="$CPPFLAGS -DGETTEXT_PACKAGE=$program"
CPPFLAGS="$CPPFLAGS -DLOCALEDIR=$PREFIX/share/locale"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wall -Wextra"
if [ -z "$NOCOLORS" ]; then
    CFLAGS="$CFLAGS -fdiagnostics-color=always"
fi
# CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wno-format-pedantic"
CFLAGS="$CFLAGS -Wno-unknown-warning-option"
CFLAGS="$CFLAGS -Wno-gnu-union-cast"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-unknown-pragmas"
CFLAGS="$CFLAGS -Wno-format-security"
CFLAGS="$CFLAGS -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-comma"
CFLAGS="$CFLAGS -Wno-undef"

LDFLAGS="$LDFLAGS $(pkg-config --cflags --libs gtk4) -lpthread"
LDFLAGS="$LDFLAGS -lm"
OS=$(uname -a)

CC="${CC:-cc}"
if echo "$OS" | grep -q "Linux"; then
    if echo "$OS" | grep -q "GNU"; then
        GNUSOURCE="-D_GNU_SOURCE"
    fi
fi

option_remove() {
    echo "$1" | sed -E "s| *$2 +| |g"
}

generate_welcome_h() {
    if [ -f "README.md" ]; then
        trace_on
        python3 build_welcome.py README.md src/.welcome.h
        trace_off
    fi
}

with_chibicc () {
    args="$*"
    while ! problem=$(chibicc $args 2>&1); do
        trace_off
        sleep 0.4
        if echo "$problem" | grep -q "unknown argument:"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        elif echo "$problem" | grep -q "unknown file extension:"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        else
            printf "\n\nError compiling with chibicc:\n\n${problem}\n\n"
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
    CFLAGS="$CFLAGS -g -O2 -flto"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    exe="bin/${program}_perf"
    ;;
"valgrind")
    CFLAGS="$CFLAGS -g3 -O0 -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    ;;
"callgrind")
    CFLAGS="$CFLAGS -g3 -O2 -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    ;;
"test")
    CFLAGS="$CFLAGS -g3 $GNUSOURCE -DDEBUGGING=1 -fsanitize=undefined -Wno-address"
    ;;
"check")
    CC=gcc
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer"
    ;;
"build"|"run")
    CFLAGS="$CFLAGS $GNUSOURCE -O2 -flto -march=native -ftree-vectorize"
    ;;
"release")
    CFLAGS="$CFLAGS $GNUSOURCE -DRELEASING=1 -O2 -flto -march=native -ftree-vectorize"
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

if [ "$target" = "cross" ]; then
    cross="$2"
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

    if [ ! -d "po" ]; then
        return
    fi
    for lang in $LANGS; do
        if [ -f "po/$lang.po" ]; then
            mkdir -p "po/$lang/LC_MESSAGES"
            msgfmt "po/$lang.po" -o "po/$lang/LC_MESSAGES/$program.mo"
        fi
    done

    ctags --kinds-C=+l+d cbase/*.c src/*.h src/*.c  2> /dev/null || true
    vtags.sed tags | sort | uniq > .tags.vim 2> /dev/null || true
    if [ "$CC" = "chibicc" ]; then
        $measure with_chibicc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    else
        $measure $CC          $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS
    fi

    if [ $target = "debug" ]; then
        # reactivate fatal_warnings after solving:
        # (cecup_debug:9237):
        # Gtk-WARNING **:
        # Trying to snapshot GtkGizmo 0x555555c69fd0 without a current allocation

        # G_DEBUG=fatal_warnings \
            gdb $exe -ex run 2>&1 | tee "gdb_output_$(date +%s).txt"
    fi
    if [ $target = "run" ]; then
        $exe
    fi

    trace_off
    ;;
"install")
    trace_on
    $0 release
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
        install -Dm755 \
            "$program.desktop" \
            "$DESTDIR/usr/share/applications/$program.desktop"
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
        test_exe="/tmp/${name}_test"

        printf "\nTesting ${RED}${src}${RES} ...\n"

        flags="$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$src")"
        if [ $src = "src/windows_functions.c" ]; then
            if ! zig version; then
                continue
            fi
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

        if [ "$CC" = "chibicc" ]; then
            cmdline_no_cc=$(option_remove "$cmdline" "$CC")
            trace_on
            if with_chibicc "$cmdline_no_cc"; then
                /tmp/${name}_test
            else
                exit 1
            fi
        else
            trace_on
            if $cmdline; then
                if ! $test_exe; then
                    gdb --quiet \
                        -ex run -ex backtrace -ex quit \
                        $test_exe 2>&1 | xsel -b
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
    rm -vf  "${DESTDIR}${PREFIX}/bin/${program:?}"
    rm -vf  "${DESTDIR}${PREFIX}/man/man1/${program:?}.1"
    for lang in $LANGS; do
        rm -vf "${DESTDIR}${PREFIX}/share/locale/$lang/LC_MESSAGES/$program.mo"
    done
    rm -rvf "$DESTDIR/etc/${program:?}/"
    rm -vf  "$DESTDIR/usr/share/applications/${program:?}.desktop"
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
    CC=gcc CFLAGS="-fanalyzer" ./build.sh
    scan-build --view -analyze-headers --status-bugs ./build.sh
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
