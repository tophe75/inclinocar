#!/usr/bin/env python3
"""
Injects the git tag as FW_VERSION into a PlatformIO platformio.ini file.
Usage: python3 set_version.py <tag> <path/to/platformio.ini>
"""
import sys
import re

tag  = sys.argv[1]
path = sys.argv[2]

txt = open(path).read()

# Remove any existing FW_VERSION line
txt = re.sub(r'[ \t]*-DFW_VERSION=[^\n]*\n', '', txt)

# Append fresh FW_VERSION at the end of build_flags
txt = txt.rstrip() + '\n    -DFW_VERSION=\\"' + tag + '\\"\n'

open(path, 'w').write(txt)
print(f'FW_VERSION set to {tag} in {path}')
