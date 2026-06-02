"""
make_transparent_gui.py
Copies every DDS from gui_resident into gui_resident_nohud,
replacing pixel data with a fully-transparent DXT5 texture
of the same width/height.
"""

import os
import struct

SRC = r"C:\Program Files (x86)\Steam\steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data\hd_textures\gui_resident"
DST = r"C:\Program Files (x86)\Steam\steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data\hd_textures\gui_resident_nohud"


def make_transparent_dxt5(width: int, height: int) -> bytes:
    """Build a minimal single-mip DXT5 DDS that is fully transparent."""
    blocks_w = max(1, (width  + 3) // 4)
    blocks_h = max(1, (height + 3) // 4)
    linear_size = blocks_w * blocks_h * 16   # 16 bytes per DXT5 block

    # DDS_PIXELFORMAT  (32 bytes)
    pf  = struct.pack('<I', 32)          # dwSize
    pf += struct.pack('<I', 0x4)         # dwFlags = DDPF_FOURCC
    pf += b'DXT5'                        # dwFourCC
    pf += b'\x00' * 20                   # rgbBitCount + masks (unused)

    # DDS_HEADER (124 bytes, starts after magic)
    hdr  = struct.pack('<I', 124)                    # dwSize
    hdr += struct.pack('<I', 0x81007)                # dwFlags: CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
    hdr += struct.pack('<I', height)
    hdr += struct.pack('<I', width)
    hdr += struct.pack('<I', linear_size)            # dwPitchOrLinearSize
    hdr += struct.pack('<I', 0)                      # dwDepth
    hdr += struct.pack('<I', 0)                      # dwMipMapCount
    hdr += b'\x00' * 44                              # dwReserved1[11]
    hdr += pf
    hdr += struct.pack('<I', 0x1000)                 # dwCaps = DDSCAPS_TEXTURE
    hdr += struct.pack('<I', 0)                      # dwCaps2
    hdr += struct.pack('<I', 0)                      # dwCaps3
    hdr += struct.pack('<I', 0)                      # dwCaps4
    hdr += struct.pack('<I', 0)                      # dwReserved2

    # Each DXT5 block that is all-zero = alpha0=0, alpha1=0, all indices->0 = fully transparent
    pixel_data = b'\x00' * linear_size

    return b'DDS ' + hdr + pixel_data


def read_dds_dimensions(path: str):
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != b'DDS ':
            return None, None
        f.read(4)   # dwSize
        f.read(4)   # dwFlags
        height = struct.unpack('<I', f.read(4))[0]
        width  = struct.unpack('<I', f.read(4))[0]
    return width, height


os.makedirs(DST, exist_ok=True)

dds_files = [f for f in os.listdir(SRC) if f.lower().endswith('.dds')]
print(f"Processing {len(dds_files)} files...")

for fname in sorted(dds_files):
    src_path = os.path.join(SRC, fname)
    dst_path = os.path.join(DST, fname)

    w, h = read_dds_dimensions(src_path)
    if w is None:
        print(f"  SKIP (bad magic): {fname}")
        continue

    data = make_transparent_dxt5(w, h)
    with open(dst_path, 'wb') as f:
        f.write(data)
    print(f"  {fname}  ({w}x{h})")

print(f"\nDone. Output: {DST}")
