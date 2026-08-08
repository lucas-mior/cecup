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
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Werror=all -Werror=extra"
CFLAGS="$CFLAGS -Werror"  # Only uncomment occasionally, keep this line
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-unused-macros"

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-cast-align"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-comma"
    CFLAGS="$CFLAGS -Wno-constant-logical-operand"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-float-equal"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-pedantic"
    CFLAGS="$CFLAGS -Wno-poison-system-directories"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"

    # only for the LSP and unity-test builds. They include helper functions
    # that are intentionally unused in some translation units.
    CFLAGS="$CFLAGS -Wno-unneeded-internal-declaration"
    CFLAGS="$CFLAGS -Wno-undefined-internal"
fi

if [ -z "$NOCOLORS" ]; then
    CFLAGS="$CFLAGS -fdiagnostics-color=always"
fi

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
    GTK_INCLUDES=$($PKG_CONFIG --cflags-only-I gtk4 | sed 's/-I/-isystem /g')
    GTK_OTHER_CFLAGS=$($PKG_CONFIG --cflags-only-other gtk4)
    GTK_CFLAGS="${GTK_CFLAGS:-$GTK_INCLUDES $GTK_OTHER_CFLAGS}"
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
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -ftree-vectorize"
    fi
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    ;;
test)
    CFLAGS="$CFLAGS -g3 $GNUSOURCE -DDEBUGGING=1 -fsanitize=undefined"
    ;;
check)
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer -fdiagnostics-color=never"
    ;;
build|run)
    CFLAGS="$CFLAGS $GNUSOURCE -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    ;;
release)
    CFLAGS="$CFLAGS $GNUSOURCE -O2"
    if [ "$target_os" = "Linux" ]; then
        CFLAGS="$CFLAGS -flto -march=native -ftree-vectorize"
    fi
    ;;
fast_feedback)
    CFLAGS="$CFLAGS $GNUSOURCE"
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

cross_syntax_src=""
if [ "$target" = "cross" ]; then
    CFLAGS=$(option_remove "$CFLAGS" "-D_GNU_SOURCE")

    case "$cross" in
    *macos*)
        CC="${HOST_CC:-${CC:-cc}}"
        # macOS cross builds use a syntax-only platform-detection probe.
mkdir -p .cache
cross_syntax_src=".cache/cecup-cross-syntax-$$.c"

# Keep this probe independent from Zig's Darwin SDK discovery. Native
# macOS CI is the real full compiler gate; this Linux-hosted probe only
# validates that the project platform feature gates select the expected
# Darwin branch when the target macros are simulated.
cat > "$cross_syntax_src" <<'EOF'
#if defined(__linux__)
#undef __linux__
#endif
#if defined(__FreeBSD__)
#undef __FreeBSD__
#endif
#if defined(__NetBSD__)
#undef __NetBSD__
#endif
#if defined(__OpenBSD__)
#undef __OpenBSD__
#endif
#if defined(_WIN32)
#undef _WIN32
#endif
#if defined(_WIN64)
#undef _WIN64
#endif
#if defined(__wasm__)
#undef __wasm__
#endif

#define __APPLE__ 1
#define __MACH__ 1
EOF
cat cbase/platform_detection.h >> "$cross_syntax_src"
cat >> "$cross_syntax_src" <<'EOF'

#if !OS_MAC
#error "Darwin cross syntax check must target macOS"
#endif
#if !OS_UNIX
#error "macOS must be detected as Unix"
#endif
#if CBASE_HAS_PROCFS
#error "macOS must not enable procfs helpers"
#endif
#if OS_WINDOWS || OS_LINUX || OS_BSD || OS_WASM
#error "macOS target must not enable another OS family"
#endif

int
main(void) {
    return 0;
}
EOF
        ;;
    *)
        if ! command_exists zig; then
            error "zig not found"
            exit 1
        fi
        CC="zig cc"
        CFLAGS="$CFLAGS -target $cross"
        ;;
    esac

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
    if [ "$CC" = "chibicc" ]; then
        compile_with_other chibicc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    elif [ "$CC" = "cproc" ]; then
        compile_with_other cproc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    elif [ "$cross_compile_only" = "1" ]; then
        if [ -z "$cross_syntax_src" ]; then
            error "internal error: cross syntax source was not generated"
            exit 1
        fi
        $CC $CPPFLAGS $CFLAGS -fsyntax-only "$cross_syntax_src"
        rm -f "$cross_syntax_src"
    else
        $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS
    fi

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
    find . -iname "*.c" | sort | while read -r src; do
        trace_off
        name=$(basename "$src")

        if [ -n "$2" ] && [ "$name" != "$2" ]; then
            continue
        fi
        if [ "$name" = "main.c" ]; then
            continue
        fi
        if echo "$src" | grep -q "stc/"; then
            continue
        fi

        name=$(echo "$name" | sed 's/\.c//')
        test_exe="$CECUP_TEST_TMPDIR/${name}_test"

        printf "\nTesting ${RED}${src}${RES} ...\n"

        flags="$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$src")"
        test_cc="$CC"
        if [ "$src" = "src/windows_functions.c" ]; then
            if ! zig version; then
                continue
            fi
            test_cc="zig cc"
            cmdline="zig cc $CPPFLAGS $CFLAGS"
            cmdline=$(option_remove "$cmdline" "-D_GNU_SOURCE")
            cmdline="$cmdline -target x86_64-windows-gnu"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1"
            cmdline="$cmdline $flags -o $test_exe $src"
        else
            cmdline="$test_cc $CPPFLAGS $CFLAGS"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1 $LDFLAGS"
            cmdline="$cmdline $flags -o $test_exe $src"
        fi

        if [ "$test_cc" = "chibicc" ] || [ "$test_cc" = "cproc" ]; then
            cmdline_no_cc=$(option_remove "$cmdline" "$test_cc")
            trace_on
            if compile_with_other "$test_cc" "$cmdline_no_cc"; then
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
                            --one-line "run" \
                            --one-line "bt" \
                            -- "$test_exe" 2>&1 || true
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
