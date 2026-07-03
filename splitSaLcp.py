#!/usr/bin/env python3

# split the sa+lcp file produced by CaPS-SA discarding the first 8 bytes (input size?)
# only useful for CaPS-SA output, which is a single file containing both the suffix array and the LCP array.

import sys
from pathlib import Path

def copy_chunk(fin, fout, nbytes, chunk_size=1024 * 1024):
    while nbytes > 0:
        to_read = min(chunk_size, nbytes)
        buf = fin.read(to_read)
        if not buf:
            raise EOFError("Unexpected EOF while copying.")
        fout.write(buf)
        nbytes -= len(buf)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input_binary_file>", file=sys.stderr)
        sys.exit(1)

    inp = Path(sys.argv[1])
    size = inp.stat().st_size

    if size < 8:
        raise ValueError(f"File too small: must be at least 8 bytes. Got {size}.")
    size2 = size - 8
    if size2 % 2 != 0:
        raise ValueError(f"After discarding first 8 bytes, size must be 2N. Got {size2} bytes.")

    half = size2 // 2

    # If input has extension, replace it; otherwise append .sa/.lcp
    out_sa = inp.with_suffix(".sa")
    out_lcp = inp.with_suffix(".lcp")

    chunk_size = 1024 * 1024  # 1 MiB

    with inp.open("rb") as fin, out_sa.open("wb") as fsa, out_lcp.open("wb") as flcp:
        fin.seek(8)  # discard first 8 bytes
        copy_chunk(fin, fsa, half, chunk_size=chunk_size)
        copy_chunk(fin, flcp, half, chunk_size=chunk_size)

if __name__ == "__main__":
    main()
