#!/usr/bin/env python3

"""
Convert UTF-8 text to a clean uint16 array for SA construction.
Filters surrogate pairs (U+D800–U+DFFF) which are invalid in UCS-2.

Processes large files in chunks to minimize memory usage.
Supports output size limits via command-line multiplier suffixes (K, M, G, T).
    
"""

import sys
import os
import lzma
import gzip
import bz2
import numpy as np


def open_text(input_path):
    """
    Open a text file for reading, transparently streaming through xz/gz/bz2
    decompression based on file extension (without ever materializing the
    fully decompressed data on disk or in memory).
    """
    if input_path.endswith('.xz'):
        return lzma.open(input_path, 'rt', encoding='utf-8', errors='replace')
    if input_path.endswith('.gz'):
        return gzip.open(input_path, 'rt', encoding='utf-8', errors='replace')
    if input_path.endswith('.bz2'):
        return bz2.open(input_path, 'rt', encoding='utf-8', errors='replace')
    return open(input_path, 'r', encoding='utf-8', errors='replace')


def parse_size(size_str):
    """
    Parse a size string with optional suffix (K, M, G, T).
    Examples: "100M", "1G", "500K", "1024" (bytes)
    """
    if not size_str:
        return None
    
    size_str = size_str.strip().upper()
    multipliers = {
        'K': 1024,
        'M': 1024 ** 2,
        'G': 1024 ** 3,
        'T': 1024 ** 4,
    }
    
    for suffix, mult in multipliers.items():
        if size_str.endswith(suffix):
            try:
                return int(float(size_str[:-1]) * mult)
            except ValueError:
                raise ValueError(f"Invalid size format: {size_str}")
    
    # No suffix, treat as bytes
    try:
        return int(size_str)
    except ValueError:
        raise ValueError(f"Invalid size format: {size_str}")


def utf8_to_uint16_array_chunked(input_path, output_path, max_output_size=None, chunk_size=65536):
    """
    Convert UTF-8 text to a clean uint16 array for SA construction.
    Filters surrogate pairs (U+D800–U+DFFF) which are invalid in UCS-2.
    
    Processes input in chunks to handle large files efficiently.
    
    Args:
        input_path: Path to input UTF-8 text file
        output_path: Path to output binary file
        max_output_size: Maximum output file size in bytes (None for unlimited)
        chunk_size: Number of characters to read per chunk
    """
    
    output_count = 0
    total_unique = set()
    
    with open(output_path, 'wb') as out_f:
        with open_text(input_path) as in_f:
            while True:
                # Read chunk
                chunk = in_f.read(chunk_size)
                if not chunk:
                    break
                
                # Filter surrogates and codepoints outside uint16 range
                codepoints = [ord(c) for c in chunk
                              if not (0xD800 <= ord(c) <= 0xDFFF) and ord(c) <= 0xFFFF]
                
                if not codepoints:
                    continue
                
                # Convert to uint16 array and write to file
                arr = np.array(codepoints, dtype=np.uint16)
                arr.tofile(out_f)
                
                output_count += len(arr)
                total_unique.update(codepoints)
                
                # Check if we've reached the output size limit
                if max_output_size is not None:
                    current_size = out_f.tell()
                    if current_size >= max_output_size:
                        print(f"Output size limit reached ({current_size:,} bytes >= {max_output_size:,} bytes)")
                        break
    
    unique = len(total_unique)
    output_size = os.path.getsize(output_path)
    print(f"Length: {output_count:,}  |  Distinct symbols: {unique:,}  |  Output size: {output_size:,} bytes")
    
    return output_count, unique


def main():
    if len(sys.argv) < 3:
        print("Usage: python 2utf16.py <input_file> <output_file> [max_output_size]")
        print("  max_output_size: optional size limit (e.g., '100M', '1G', '500K')")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    max_output_size = None
    
    if len(sys.argv) > 3:
        try:
            max_output_size = parse_size(sys.argv[3])
            print(f"Max output size: {max_output_size:,} bytes")
        except ValueError as e:
            print(f"Error: {e}")
            sys.exit(1)
    
    utf8_to_uint16_array_chunked(input_path, output_path, max_output_size)


if __name__ == '__main__':
    main()

