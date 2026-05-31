"""
Extract LR:FFXIII face IMGB files as PNG candidates for all plausible DXT formats.
Creates subfolders under an output dir, one per format+dimension combination.
"""
import struct
import os
import io
from PIL import Image

FACE_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data\gui\resident\face"
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Other", "LR", "face_candidates")

FILE_SIZE = 16384  # expected data bytes

# DDS header builder
DDSD_CAPS        = 0x00000001
DDSD_HEIGHT      = 0x00000002
DDSD_WIDTH       = 0x00000004
DDSD_LINEARSIZE  = 0x00080000
DDSD_PIXELFORMAT = 0x00001000
DDSCAPS_TEXTURE  = 0x00001000
DDPF_FOURCC      = 0x00000004
DDPF_RGB         = 0x00000040
DDPF_ALPHAPIXELS = 0x00000001

def dxt_header(width, height, fourcc: bytes, data_size: int) -> bytes:
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE
    pf_flags = DDPF_FOURCC
    return struct.pack(
        "<4sI"          # magic, dwSize=124
        "IIIII"         # dwFlags, dwHeight, dwWidth, dwPitchOrLinearSize, dwDepth
        "I"             # dwMipMapCount
        "44s"           # dwReserved1[11]
        "II4sIIIII"     # DDPIXELFORMAT: size,flags,fourCC,bitcount,RGBA masks
        "IIIII",        # DDSCAPS: caps,caps2,caps3,caps4, reserved2
        b"DDS ", 124,
        flags, height, width, data_size, 0,
        1,
        b"\x00" * 44,
        32, pf_flags, fourcc, 0, 0, 0, 0, 0,
        DDSCAPS_TEXTURE, 0, 0, 0, 0,
    )

def uncompressed_header(width, height, bpp, r_mask, g_mask, b_mask, a_mask) -> bytes:
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    pitch = width * (bpp // 8)
    pf_flags = DDPF_RGB | DDPF_ALPHAPIXELS
    return struct.pack(
        "<4sI"
        "IIIII"
        "I"
        "44s"
        "IIIIIIII"
        "IIIII",
        b"DDS ", 124,
        flags, height, width, pitch, 0,
        1,
        b"\x00" * 44,
        32, pf_flags, 0, bpp, r_mask, g_mask, b_mask, a_mask,
        DDSCAPS_TEXTURE, 0, 0, 0, 0,
    )

CANDIDATES = [
    # (label, width, height, header_fn, block_bytes_per_4x4)
    ("DXT5_128x128",  128, 128, lambda w,h,n: dxt_header(w, h, b"DXT5", n), 16),
    ("DXT5_256x64",   256,  64, lambda w,h,n: dxt_header(w, h, b"DXT5", n), 16),
    ("DXT3_128x128",  128, 128, lambda w,h,n: dxt_header(w, h, b"DXT3", n), 16),
    ("DXT1_128x256",  128, 256, lambda w,h,n: dxt_header(w, h, b"DXT1", n),  8),
    ("DXT1_256x128",  256, 128, lambda w,h,n: dxt_header(w, h, b"DXT1", n),  8),
    ("DXT1_128x128",  128, 128, lambda w,h,n: dxt_header(w, h, b"DXT1", n),  8),  # 8192 bytes -- will zero-pad
    # Uncompressed
    ("BGRA8888_64x64", 64,  64,
     lambda w,h,n: uncompressed_header(w, h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
     None),
    ("BGRA8888_128x32", 128, 32,
     lambda w,h,n: uncompressed_header(w, h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
     None),
]

def expected_data_size(w, h, block_bytes):
    if block_bytes is None:
        return w * h * 4  # BGRA8888
    blocks_x = max(1, (w + 3) // 4)
    blocks_y = max(1, (h + 3) // 4)
    return blocks_x * blocks_y * block_bytes

imgb_files = sorted(
    f for f in os.listdir(FACE_DIR)
    if f.endswith(".imgb")
)

for label, w, h, hdr_fn, block_bytes in CANDIDATES:
    out_sub = os.path.join(OUT_DIR, label)
    os.makedirs(out_sub, exist_ok=True)
    needed = expected_data_size(w, h, block_bytes)

    for fname in imgb_files:
        fpath = os.path.join(FACE_DIR, fname)
        raw = open(fpath, "rb").read()
        if len(raw) <= 8:
            # placeholder file (face000 = FF FF FF FF 00 00 00 00)
            continue

        # Truncate or zero-pad to needed size
        if len(raw) > needed:
            data = raw[:needed]
        else:
            data = raw + b"\x00" * (needed - len(raw))

        header = hdr_fn(w, h, needed)
        dds_bytes = header + data
        stem = fname.split(".")[0]
        png_path = os.path.join(out_sub, stem + ".png")

        try:
            img = Image.open(io.BytesIO(dds_bytes))
            img = img.convert("RGBA")
            img.save(png_path)
        except Exception as e:
            print(f"  FAILED {label} {fname}: {e}")
            continue

    print(f"[{label}] done -> {out_sub}")

print("\nAll candidates written.")
