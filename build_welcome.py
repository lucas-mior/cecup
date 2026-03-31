#!/usr/bin/env python3

import sys
import re

def escape_pango(text):
    text = text.replace('&', '&amp;')
    text = text.replace('<', '&lt;')
    text = text.replace('>', '&gt;')
    return text

def process_markdown(lines):
    out = []
    for line in lines:
        line = line.rstrip('\n')
        
        # Skip the main title and the GIF
        if line.startswith('# cecup') or line.startswith('!['):
            continue
            
        line = escape_pango(line)
        
        # Headings (Fixed: removed literal \n from the f-strings)
        if line.startswith('## '):
            line = line.replace('## ', '', 1)
            line = f'<b><big>{line}</big></b>'
        elif line.startswith('# '):
            line = line.replace('# ', '', 1)
            line = f'<b><big><big>{line}</big></big></b>'
            
        # Lists
        line = re.sub(r'^(\s*)[-*]\s+', r'\1• ', line)
        line = re.sub(r'^(\s*)\+\s+', r'\1◦ ', line)
        
        # Bold and Italic
        line = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', line)
        line = re.sub(r'\*(.+?)\*', r'<i>\1</i>', line)
        line = re.sub(r'_(.+?)_', r'<i>\1</i>', line)
        
        # Inline code
        line = re.sub(r'`(.+?)`', r'<tt>\1</tt>', line)
        
        # Escape quotes for C string
        line = line.replace('"', '\\"')
        
        out.append(f'"{line}\\n"')
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
