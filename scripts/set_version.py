#!/usr/bin/env python3
"""
Injects the git tag as FW_VERSION into the build_flags section of platformio.ini.
Handles both cases: build_flags with existing entries, and build_flags that is empty.
Usage: python3 scripts/set_version.py <tag> <path/to/platformio.ini>
"""
import sys, re

tag  = sys.argv[1]
path = sys.argv[2]

txt = open(path).read()

# Remove any existing FW_VERSION line wherever it appears
txt = re.sub(r'[ \t]*-DFW_VERSION=[^\n]*\n', '', txt)

new_flag = '    -DFW_VERSION=\\"' + tag + '\\"'

# Case 1: build_flags has other entries — append after the last one
if re.search(r'build_flags\s*=(?:\s*\n[ \t]+-[^\n]+)+', txt):
    txt = re.sub(
        r'(build_flags\s*=(?:\s*\n[ \t]+-[^\n]+)+)',
        lambda m: m.group(0) + '\n' + new_flag,
        txt
    )
# Case 2: build_flags is empty or only whitespace — add the flag on the next line
elif re.search(r'build_flags\s*=\s*\n', txt):
    txt = re.sub(
        r'(build_flags\s*=\s*\n)',
        r'\g<1>' + new_flag + '\n',
        txt
    )
# Case 3: no build_flags section at all — add one before lib_deps
else:
    txt = re.sub(
        r'(lib_deps\s*=)',
        'build_flags =\n' + new_flag + '\n\n\\1',
        txt
    )

open(path, 'w').write(txt)
print(f'FW_VERSION set to {tag}')
