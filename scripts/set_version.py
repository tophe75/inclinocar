#!/usr/bin/env python3
"""
Injects the git tag as FW_VERSION build flag into platformio.ini.
Usage: python3 scripts/set_version.py <tag> <path/to/platformio.ini>
"""
import sys, re

tag  = sys.argv[1]
path = sys.argv[2]

txt = open(path).read()
# Remove any existing FW_VERSION line
txt = re.sub(r'[ \t]*-DFW_VERSION=[^\n]*\n', '', txt)
# Produce:  -DFW_VERSION=\"v0.x.x\"  (backslash-quote wrapping the version)
line = '    -DFW_VERSION=\\"' + tag + '\\"\n'
txt = txt.rstrip() + '\n' + line
open(path, 'w').write(txt)
print(f'FW_VERSION set to {tag}')
