#!/usr/bin/env python3
import sys
import re


def escape_pango(text):
    text = text.replace('&', '&amp;')
    text = text.replace('<', '&lt;')
    text = text.replace('>', '&gt;')
    return text


def process_markdown(lines):
    blocks = []
    current_block = []

    for line in lines:
        line = line.rstrip('\n')

        # Skip the main title and the GIF
        if line.startswith('# cecup') or line.startswith('!['):
            continue

        if line.strip() == '':
            # Empty line means the end of a block
            if current_block:
                blocks.append(current_block)
                current_block = []
            blocks.append(['']) # Represent an empty line block to preserve \n
        elif re.match(r'^\s*[-*+]\s+', line):
            # A list marker starts a new block
            if current_block:
                blocks.append(current_block)
            current_block = [line]
        elif line.startswith('#'):
            # A heading is its own isolated block
            if current_block:
                blocks.append(current_block)
                current_block = []
            blocks.append([line])
        else:
            # Standard continuation line
            current_block.append(line)

    # Don't forget the last block
    if current_block:
        blocks.append(current_block)

    # Step 2: Format and merge blocks
    out = []
    for block in blocks:
        if not block:
            continue

        if block == ['']:
            out.append('"\\n"')
            continue

        # Merge block lines into a single line.
        # Keeps the first line's indentation, but strips trailing/leading spaces
        # from the wrapped lines so words don't have multiple spaces between them.
        if len(block) > 1:
            merged = block[0].rstrip() + " " + " ".join(l.strip() for l in block[1:])
        else:
            merged = block[0]

        merged = escape_pango(merged)

        # Headings
        if merged.startswith('## '):
            merged = merged.replace('## ', '', 1)
            merged = f'<b><big>{merged}</big></b>'
        elif merged.startswith('# '):
            merged = merged.replace('# ', '', 1)
            merged = f'<b><big><big>{merged}</big></big></b>'

        # Lists
        merged = re.sub(r'^(\s*)[-*]\s+', r'\1• ', merged)
        merged = re.sub(r'^(\s*)\+\s+', r'\1◦ ', merged)

        # Bold and Italic
        merged = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', merged)
        merged = re.sub(r'\*(.+?)\*', r'<i>\1</i>', merged)
        merged = re.sub(r'_(.+?)_', r'<i>\1</i>', merged)

        # Inline code
        merged = re.sub(r'`(.+?)`', r'<tt>\1</tt>', merged)

        # Escape quotes for C string
        merged = merged.replace('"', '\\"')

        out.append(f'"{merged}\\n"')

    return out

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 build_welcome.py <input.md> <output.h>")
        sys.exit(1)

    in_file = sys.argv[1]
    out_file = sys.argv[2]

    try:
        with open(in_file, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        processed = process_markdown(lines)

        with open(out_file, 'w', encoding='utf-8') as f:
            f.write('static char *cecup_welcome_text = N_(\n')
            f.write('\n'.join(processed))
            f.write('\n);\n')
    except Exception as e:
        print(f"Error processing markdown: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
