#!/usr/bin/env python3
"""
Injects the git tag as FW_VERSION into the build_flags section of platformio.ini.
Usage: python3 scripts/set_version.py <tag> <path/to/platformio.ini>
"""
import sys, re

tag  = sys.argv[1]
path = sys.argv[2]

txt = open(path).read()

# Remove any existing FW_VERSION line wherever it is
txt = re.sub(r'[ \t]*-DFW_VERSION=[^\n]*\n', '', txt)

# Insert the new FW_VERSION line right after the last existing build_flag line
# i.e. before the blank line that follows build_flags
txt = re.sub(
    r'(build_flags\s*=(?:\s*\n[ \t]+-[^\n]+)+)',
    lambda m: m.group(0) + '\n    -DFW_VERSION=\\"' + tag + '\\"',
    txt
)

open(path, 'w').write(txt)
print(f'FW_VERSION set to {tag}')
