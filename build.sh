#!/bin/sh

set -e
program="cecup"
DESTDIR="${DESTDIR:-}"
CC=clang

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -O2 -flto -g"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Wfatal-errors"
# CFLAGS="$CFLAGS -Werror"
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

CC="${CC:-cc}"

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
    # check later
    CFLAGS="$CFLAGS -Wno-reserved-macro-identifier"
    CFLAGS="$CFLAGS -Wno-reserved-identifier"
    CFLAGS="$CFLAGS -Wno-documentation-unknown-command"
    CFLAGS="$CFLAGS -Wno-documentation"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-incompatible-pointer-types-discards-qualifiers"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
fi

if [ "$1" = "install" ]; then
    trace_on
    $CC $CPPFLAGS $CFLAGS main.c -o "./$program" $LDFLAGS
    
    install -Dm755 "./$program" "$DESTDIR/usr/bin/$program"
    
    install -dm755 "$DESTDIR/etc/$program"
    if [ -d "config" ]; then
        cp -rp config/* "$DESTDIR/etc/$program/"
    fi
    
    if [ -f "$program.desktop" ]; then
        install -Dm644 "$program.desktop" "$DESTDIR/usr/share/applications/$program.desktop"
    fi
    trace_off
else
    trace_on
    # shellcheck disable=SC2086
    rm ./$program || true
    ctags --kinds-C=+l+d ./*.h ./*.c 2> /dev/null || true
    vtags.sed tags | sort | uniq > .tags.vim       2> /dev/null || true
    $CC $CPPFLAGS $CFLAGS main.c -o ./$program $LDFLAGS
    ./$program
    trace_off
fi
