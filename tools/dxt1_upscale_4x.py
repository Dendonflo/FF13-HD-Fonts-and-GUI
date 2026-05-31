#!/usr/bin/env python3
"""
4x integer upscale of DXT1 DDS textures.
Each pixel in the source becomes a 4x4 uniform-color DXT1 block in the output.
No blending — pure pixel replication. Keeps DXT1 compression throughout.

Usage:
    python dxt1_upscale_4x.py <src_dir> <dst_dir>
"""

import struct
import os
import sys


# ---------------------------------------------------------------------------
# DXT1 block decoder
# ---------------------------------------------------------------------------

def _unpack_rgb565(c):
    r = ((c >> 11) & 0x1F)
    g = ((c >> 5)  & 0x3F)
    b = ( c        & 0x1F)
    # Expand to 8-bit (replicate high bits into low bits for accurate range)
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def decode_dxt1_block(data, offset):
    """Return list of 16 (r8,g8,b8) tuples, row-major top-left first."""
    c0, c1 = struct.unpack_from('<HH', data, offset)
    idx    = struct.unpack_from('<I',  data, offset + 4)[0]

    col0 = _unpack_rgb565(c0)
    col1 = _unpack_rgb565(c1)

    if c0 > c1:
        # 4-colour mode
        palette = [
            col0,
            col1,
            tuple((2 * col0[i] + col1[i] + 1) // 3 for i in range(3)),
            tuple((col0[i] + 2 * col1[i] + 1) // 3 for i in range(3)),
        ]
    else:
        # 3-colour + punch-through alpha mode
        palette = [
            col0,
            col1,
            tuple((col0[i] + col1[i]) // 2 for i in range(3)),
            (0, 0, 0),
        ]

    return [palette[(idx >> (2 * i)) & 3] for i in range(16)]


# ---------------------------------------------------------------------------
# DXT1 uniform-block encoder
# ---------------------------------------------------------------------------

def encode_uniform_dxt1_block(r, g, b):
    """Encode a 4x4 block where every pixel has the same colour.
    Uses 4-colour mode: color0 = colour, color1 = 0, all indices = 0."""
    r5 = r >> 3
    g6 = g >> 2
    b5 = b >> 3
    c = (r5 << 11) | (g6 << 5) | b5
    # 8 bytes: color0 (u16), color1 (u16), indices (u32) -- all indices 0
    return struct.pack('<HHI', c, 0, 0)


# ---------------------------------------------------------------------------
# Core upscale
# ---------------------------------------------------------------------------

def upscale_dxt1_4x(dxt1_data, src_w, src_h):
    """Return (dxt1_bytes, dst_w, dst_h) — 4x integer upscale."""
    assert src_w % 4 == 0 and src_h % 4 == 0, "Dimensions must be multiples of 4"

    src_bx = src_w // 4
    src_by = src_h // 4
    dst_w  = src_w * 4
    dst_h  = src_h * 4
    dst_bx = dst_w // 4   # == src_w
    dst_by = dst_h // 4   # == src_h

    out = bytearray(dst_bx * dst_by * 8)

    for iby in range(src_by):
        for ibx in range(src_bx):
            src_off = (iby * src_bx + ibx) * 8
            pixels  = decode_dxt1_block(dxt1_data, src_off)

            # Each of the 16 pixels → one output DXT1 block
            for py in range(4):
                for px in range(4):
                    r, g, b = pixels[py * 4 + px]
                    obx = ibx * 4 + px
                    oby = iby * 4 + py
                    dst_off = (oby * dst_bx + obx) * 8
                    out[dst_off:dst_off + 8] = encode_uniform_dxt1_block(r, g, b)

    return bytes(out), dst_w, dst_h


# ---------------------------------------------------------------------------
# DDS header read / write
# ---------------------------------------------------------------------------

DDS_MAGIC = b'DDS '

def read_dds(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:4] != DDS_MAGIC:
        raise ValueError(f"Not a DDS file: {path}")
    # dwHeight at offset 12, dwWidth at offset 16
    height = struct.unpack_from('<I', raw, 12)[0]
    width  = struct.unpack_from('<I', raw, 16)[0]
    fourcc = raw[84:88]
    if fourcc != b'DXT1':
        raise ValueError(f"Expected DXT1, got {fourcc!r} in {path}")
    return raw[128:], width, height   # pixel data starts at byte 128


def write_dds_dxt1(path, dxt1_data, width, height):
    data_size = len(dxt1_data)

    hdr = bytearray()
    hdr += DDS_MAGIC
    hdr += struct.pack('<I', 124)                   # dwSize
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000    # CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
    hdr += struct.pack('<I', flags)
    hdr += struct.pack('<I', height)
    hdr += struct.pack('<I', width)
    hdr += struct.pack('<I', data_size)             # dwPitchOrLinearSize
    hdr += struct.pack('<I', 0)                     # dwDepth
    hdr += struct.pack('<I', 1)                     # dwMipMapCount
    hdr += b'\x00' * 44                             # dwReserved1 (11 dwords)

    # DDS_PIXELFORMAT (32 bytes)
    hdr += struct.pack('<I', 32)
    hdr += struct.pack('<I', 0x4)                   # DDPF_FOURCC
    hdr += b'DXT1'
    hdr += b'\x00' * 20                             # unused fields

    hdr += struct.pack('<I', 0x1000)                # dwCaps TEXTURE
    hdr += b'\x00' * 16                             # dwCaps2-4, reserved

    assert len(hdr) == 128
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(dxt1_data)


# ---------------------------------------------------------------------------
# Batch driver
# ---------------------------------------------------------------------------

def process_dir(src_dir, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)

    files = sorted(f for f in os.listdir(src_dir) if f.lower().endswith('.dds'))
    if not files:
        print(f"No DDS files found in {src_dir}")
        return

    print(f"Processing {len(files)} files: {src_dir}")
    print(f"Output:     {dst_dir}")
    print()

    for i, fname in enumerate(files, 1):
        src_path = os.path.join(src_dir, fname)
        dst_path = os.path.join(dst_dir, fname)
        try:
            dxt1_data, w, h = read_dds(src_path)
            out_data, ow, oh = upscale_dxt1_4x(dxt1_data, w, h)
            write_dds_dxt1(dst_path, out_data, ow, oh)
            print(f"[{i:3}/{len(files)}] {fname}  {w}x{h} -> {ow}x{oh}")
        except Exception as e:
            print(f"[{i:3}/{len(files)}] ERROR {fname}: {e}")

    print(f"\nDone — {len(files)} files written to {dst_dir}")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python dxt1_upscale_4x.py <src_dir> <dst_dir>")
        sys.exit(1)
    process_dir(sys.argv[1], sys.argv[2])
