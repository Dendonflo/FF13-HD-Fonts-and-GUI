#!/usr/bin/env python3
"""
xBRZ-style 4x pixel-art upscaler (numpy implementation).

This implements the core xBRZ idea: detect diagonal edges by comparing
pixel neighborhoods with a fuzzy colour threshold, then fill the scaled
output block with the detected edge colour rather than the centre pixel.
Applied twice (2x * 2x) to reach 4x.

Closest to xBRZ 2x applied twice.  For true xBRZ (with its rotational
kernel tables and partial-triangle blending), the C++ library would be
needed — but for a visual test this shows the pixel-art-aware difference
vs Gigapixel clearly.

Usage:
    python xbrz_upscale.py <src_dir> <dst_dir>

    src_dir must contain map_tiles_N subfolders with BMP files.
    dst_dir receives the same subfolder structure with PNG output.
"""

import sys, os
import numpy as np
from PIL import Image


# ── xBRZ-style scale-2x kernel ─────────────────────────────────────────────

def _scale2x(img: np.ndarray, thresh: int) -> np.ndarray:
    """
    One pass of xBRZ-style 2x upscale.

    For each pixel E, looks at its N/W/E/S neighbours.
    When two adjacent neighbours are 'close' in colour and the opposite
    pair are not, the output corner takes the neighbour colour instead of E.
    This smooths diagonal edges without blurring straight ones.

    thresh: max per-channel absolute difference to treat colours as equal.
    Use thresh=0 for exact Scale2x, ~30 for the fuzzy xBRZ behaviour.
    """
    h, w, c = img.shape

    # int16 so subtraction can't overflow
    p = np.pad(img.astype(np.int16), ((1, 1), (1, 1), (0, 0)), mode='edge')
    N_ = p[0:h,     1:w+1]   # north
    W  = p[1:h+1,   0:w]     # west
    E_ = p[1:h+1,   1:w+1]   # centre (E)
    S_ = p[1:h+1,   2:w+2]   # east  (naming: E means pixel, not direction here)
    Sv = p[2:h+2,   1:w+1]   # south

    # Rename to directions to avoid confusion with E (pixel name)
    Bn, Dn, En, Fn, Hn = N_, W, E_, S_, Sv

    def close(a: np.ndarray, b: np.ndarray) -> np.ndarray:
        """True where every channel of |a-b| <= thresh. Shape: H×W×1."""
        return (np.abs(a - b).max(axis=-1, keepdims=True) <= thresh)

    # Scale2x rules:
    #   top-left  output = W   if (W≈N and W≉S and N≉E)
    #   top-right output = E   if (N≈E and N≉W and E≉S)
    #   bot-left  output = W   if (W≈S and W≉N and S≉E)
    #   bot-right output = E   if (S≈E and S≉W and E≉N)
    cNW = close(Bn, Dn)
    cNE = close(Bn, Fn)
    cWS = close(Dn, Hn)
    cES = close(Fn, Hn)

    o_tl = np.where(cNW & ~cWS & ~cNE, Dn, En)
    o_tr = np.where(cNE & ~cNW & ~cES, Fn, En)
    o_bl = np.where(cWS & ~cNW & ~cES, Dn, En)
    o_br = np.where(cES & ~cWS & ~cNE, Fn, En)

    dst = np.empty((h * 2, w * 2, c), dtype=np.int16)
    dst[0::2, 0::2] = o_tl
    dst[0::2, 1::2] = o_tr
    dst[1::2, 0::2] = o_bl
    dst[1::2, 1::2] = o_br

    return np.clip(dst, 0, 255).astype(np.uint8)


def xbrz_4x(img: np.ndarray, thresh: int = 30) -> np.ndarray:
    """4x upscale by applying the xBRZ-style 2x kernel twice."""
    return _scale2x(_scale2x(img, thresh), thresh)


# ── DDS DXT1 output (optional) ─────────────────────────────────────────────
# Uncomment the write_dds block below if you want DXT1 output instead of PNG.
# Note: DXT1 is lossy — PNG is better for comparing visual quality.


# ── batch driver ───────────────────────────────────────────────────────────

def process_dir(src_root: str, dst_root: str, thresh: int = 30):
    os.makedirs(dst_root, exist_ok=True)

    # Find all map_tiles_N subfolders
    subdirs = sorted(
        [d for d in os.listdir(src_root)
         if os.path.isdir(os.path.join(src_root, d)) and d.startswith('map_tiles_')],
        key=lambda d: int(d.split('_')[-1])
    )

    if not subdirs:
        print(f"No map_tiles_N subfolders found in {src_root}")
        return

    total_files = sum(
        len([f for f in os.listdir(os.path.join(src_root, d))
             if f.lower().endswith(('.bmp', '.png', '.dds'))])
        for d in subdirs
    )
    print(f"Found {len(subdirs)} namespace folders, {total_files} images total")
    print(f"Output: {dst_root}")
    print(f"Colour threshold: {thresh}")
    print()

    done = 0
    for subfolder in subdirs:
        src_sub = os.path.join(src_root, subfolder)
        dst_sub = os.path.join(dst_root, subfolder)
        os.makedirs(dst_sub, exist_ok=True)

        files = sorted(f for f in os.listdir(src_sub)
                       if f.lower().endswith(('.bmp', '.png', '.dds')))

        for fname in files:
            src_path = os.path.join(src_sub, fname)
            stem = os.path.splitext(fname)[0]
            dst_path = os.path.join(dst_sub, stem + '.png')

            img = Image.open(src_path).convert('RGB')
            arr = np.array(img, dtype=np.uint8)

            out_arr = xbrz_4x(arr, thresh)

            Image.fromarray(out_arr, 'RGB').save(dst_path, optimize=False)
            done += 1
            print(f"[{done:3}/{total_files}] {subfolder}/{fname}  "
                  f"{arr.shape[1]}x{arr.shape[0]} -> "
                  f"{out_arr.shape[1]}x{out_arr.shape[0]}")

    print(f"\nDone — {done} files written to {dst_root}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python xbrz_upscale.py <src_dir> <dst_dir> [thresh=30]")
        print()
        print("  src_dir  root with map_tiles_N subfolders (BMP/PNG files)")
        print("  dst_dir  output root (PNG files, same subfolder layout)")
        print("  thresh   colour equality threshold, 0=exact Scale2x, 30=xBRZ-style (default)")
        sys.exit(1)

    src  = sys.argv[1]
    dst  = sys.argv[2]
    thr  = int(sys.argv[3]) if len(sys.argv) > 3 else 30

    process_dir(src, dst, thr)
