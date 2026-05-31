import struct, os

FACE_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data\gui\resident\face"
OUT_DIR  = r"C:\Program Files (x86)\Steam\steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data\hd_textures\faces"

os.makedirs(OUT_DIR, exist_ok=True)

def dds_header(width, height, fourcc, data_size):
    flags = 0x00000001|0x00000002|0x00000004|0x00001000|0x00080000
    return struct.pack(
        "<4sIIIIIII44sII4sIIIIIIIIII",
        b"DDS ", 124,
        flags, height, width, data_size, 0, 1,
        b"\x00"*44,
        32, 4, fourcc, 0, 0, 0, 0, 0,
        0x1000, 0, 0, 0, 0)

for fname in sorted(f for f in os.listdir(FACE_DIR) if f.endswith(".imgb")):
    raw = open(os.path.join(FACE_DIR, fname), "rb").read()
    if len(raw) <= 8:
        continue
    data = (raw + b"\x00" * 16384)[:16384]
    stem = fname.split(".")[0]
    out_path = os.path.join(OUT_DIR, stem + ".dds")
    with open(out_path, "wb") as f:
        f.write(dds_header(256, 64, b"DXT5", 16384))
        f.write(data)
    print(f"  {stem}.dds")

print("Done.")
