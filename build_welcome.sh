#!/bin/sh

if [ "$#" -lt 2 ]; then
    printf '%s\n' 'Usage: sh build_welcome.py <input.md> <output.h>' >&2
    exit 1
fi

input=$1
output=$2
tmp=${output}.$$

awk '
function ltrim(text) {
    sub(/^[[:space:]]+/, "", text)
    return text
}

function rtrim(text) {
    sub(/[[:space:]]+$/, "", text)
    return text
}

function trim(text) {
    return rtrim(ltrim(text))
}

function escape_pango(text) {
    gsub(/&/, "\\&amp;", text)
    gsub(/</, "\\&lt;", text)
    gsub(/>/, "\\&gt;", text)
    return text
}

function replace_pair(text, marker, open_tag, close_tag,    end, inner, out,
                      rest, start) {
    out = ""
    while ((start = index(text, marker)) > 0) {
        rest = substr(text, start + length(marker))
        end = index(rest, marker)
        if (end == 0) {
            return out text
        }

        inner = substr(rest, 1, end - 1)
        out = out substr(text, 1, start - 1) open_tag inner close_tag
        text = substr(rest, end + length(marker))
    }

    return out text
}

function replace_list_marker(text,    bullet, indent) {
    if (match(text, /^[[:space:]]*[-*][[:space:]]+/)) {
        bullet = "• "
    } else if (match(text, /^[[:space:]]*[+][[:space:]]+/)) {
        bullet = "◦ "
    } else {
        return text
    }

    indent = substr(text, 1, RLENGTH)
    sub(/[-*+][[:space:]]+$/, "", indent)
    return indent bullet substr(text, RLENGTH + 1)
}

function c_string(text) {
    gsub(/"/, "\\\"", text)
    return "\"" text "\\n\""
}

function print_processed(text) {
    text = escape_pango(text)

    if (text ~ /^## /) {
        sub(/^## /, "", text)
        text = "<b><big>" text "</big></b>"
    } else if (text ~ /^# /) {
        sub(/^# /, "", text)
        text = "<b><big><big>" text "</big></big></b>"
    }

    text = replace_list_marker(text)

    text = replace_pair(text, "**", "<b>", "</b>")
    text = replace_pair(text, "*", "<i>", "</i>")
    text = replace_pair(text, "_", "<i>", "</i>")
    text = replace_pair(text, "`", "<tt>", "</tt>")

    print c_string(text)
}

function flush_block(    i, text) {
    if (block_len == 0) {
        return
    }

    text = block[1]
    if (block_len > 1) {
        text = rtrim(text)
        for (i = 2; i <= block_len; i += 1) {
            text = text " " trim(block[i])
        }
    }

    print_processed(text)
    block_len = 0
}

BEGIN {
    print "#include \"cbase.h\""
    print ""
    print "static char *cecup_welcome_text = N_("
}

/^# cecup/ || /^!\[/ {
    next
}

/^[[:space:]]*$/ {
    flush_block()
    print "\"\\n\""
    next
}

/^[[:space:]]*[-*+][[:space:]]+/ {
    flush_block()
    block_len = 1
    block[1] = $0
    next
}

/^#/ {
    flush_block()
    print_processed($0)
    next
}

{
    block_len += 1
    block[block_len] = $0
}

END {
    flush_block()
    print ");"
}
' "$input" > "$tmp" || {
    status=$?
    rm -f "$tmp"
    exit "$status"
}

mv "$tmp" "$output"
