#!/usr/bin/env python3
"""
uint8_to_int8.py

Reads a sequence of uint8 values from a binary input file, subtracts 128
from each value, and writes the result as int8 values to a binary output
file.

Since uint8 ranges 0..255 and subtracting 128 maps that exactly onto
-128..127, the output always fits cleanly in int8 with no clipping or
wraparound needed.

Usage:
    python3 uint8_to_int8.py input.bin output.bin
"""

import argparse
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="Path to input file of raw uint8 values")
    ap.add_argument("output", help="Path to write raw int8 values")
    args = ap.parse_args()

    data = np.fromfile(args.input, dtype=np.uint8)
    if data.size == 0:
        print(f"Warning: {args.input} is empty or unreadable as uint8", file=sys.stderr)

    # Subtract 128 in a wider type first, then cast down to int8 (safe:
    # result range is exactly -128..127).
    shifted = data.astype(np.int16) - 128
    result = shifted.astype(np.int8)

    result.tofile(args.output)
    print(f"Wrote {result.size} int8 values to {args.output}")


if __name__ == "__main__":
    main()
