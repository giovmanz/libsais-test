#!/usr/bin/env python3

"""
Convert UTF-8 text to a clean uint16 array for SA construction.
Filters surrogate pairs (U+D800–U+DFFF) which are invalid in UCS-2.

Todo: work in chunks for large files  
    
"""




import sys
import numpy as np


def utf8_to_uint16_array(input_path, output_path, strip_surrogates=True):
    """
    Convert UTF-8 text to a clean uint16 array for SA construction.
    Filters surrogate pairs (U+D800–U+DFFF) which are invalid in UCS-2.
    """

    with open(input_path, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()

    # Filter surrogates and remap to clean uint16 range
    codepoints = [ord(c) for c in text
                  if not (0xD800 <= ord(c) <= 0xDFFF)]

    arr = np.array(codepoints, dtype=np.uint16)
    arr.tofile(output_path)

    unique = len(np.unique(arr))
    print(f"Length: {len(arr):,}  |  Distinct symbols: {unique:,}")
    return arr

# Example
# arr = utf8_to_uint16_array('ja.txt', 'ja_sa_input.bin')

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_utf8_file> <output_uint16_file>", file=sys.stderr)
        sys.exit(1)
    utf8_to_uint16_array(sys.argv[1], sys.argv[2])
