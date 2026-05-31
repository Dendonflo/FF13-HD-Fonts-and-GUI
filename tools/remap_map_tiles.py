import re, os, shutil

DB_PATH = (r"C:\Program Files (x86)\Steam\steamapps\common"
           r"\LIGHTNING RETURNS FINAL FANTASY XIII\weiss_data"
           r"\hd_textures\hash_database.txt")

TILES_BASE = r"C:\Users\dendo\Pictures\FF 13 textures files\LR\map tiles"
TILE_DIRS  = ["og", "integer scaled"]

# ── helpers ────────────────────────────────────────────────────────────────

def hex_to_dec_ns(hex_str):
    """'0a' -> 'map_tiles_10',  '1d' -> 'map_tiles_29'"""
    return f"map_tiles_{int(hex_str, 16)}"


# ── 1. Update hash_database.txt ────────────────────────────────────────────
with open(DB_PATH, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Match lines like:  <hash> map_tiles_NN/scene..._XX_split_...
PATTERN = re.compile(
    r'^([0-9a-f]{16} )map_tiles_([0-9a-f]+)/(scene\d+_[0-9a-f]+_split_[^\s]+)$'
)

def remap_line(line):
    m = PATTERN.match(line.rstrip('\n'))
    if m:
        prefix, hex_ns, filename = m.group(1), m.group(2), m.group(3)
        return f"{prefix}{hex_to_dec_ns(hex_ns)}/{filename}\n"
    return line

new_lines = [remap_line(l) for l in lines]
changed = sum(1 for a, b in zip(lines, new_lines) if a != b)

with open(DB_PATH, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print(f"hash_database.txt: {changed} lines updated")

# ── 2. Rename subfolders hex -> decimal ────────────────────────────────────
for d in TILE_DIRS:
    src_dir = os.path.join(TILES_BASE, d)
    # Build rename list, then sort by decreasing hex value to avoid
    # cascade conflicts (e.g. rename 10->16 before 0a->10).
    renames = []
    for entry in os.listdir(src_dir):
        full = os.path.join(src_dir, entry)
        if not os.path.isdir(full):
            continue
        try:
            dec_val  = int(entry, 16)
            dec_name = str(dec_val)
        except ValueError:
            continue  # not a hex folder, skip
        if entry != dec_name:
            renames.append((dec_val, full, os.path.join(src_dir, dec_name)))

    renames.sort(key=lambda t: t[0], reverse=True)   # highest hex first
    renamed = 0
    for _, old, new in renames:
        os.rename(old, new)
        renamed += 1
    print(f"{d}/: {renamed} folders renamed")

print("Done.")
