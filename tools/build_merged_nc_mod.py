#!/usr/bin/env python3
"""
Build merged NC mod: DLC Restoration + Classic Serah (c107)

Sources:
  - Existing NC mod:  Nova Chrysalia BETA/Mods/XIII-2/DLC Restoration - Console Content/
  - Old v2.3 mod:     Downloads/FFXIII-2 - Console Content Patch (v2.3)/mod/
  - Game loose files: alba_data/chr/pc/c107/bin/
  - mod_dlc WDB:      x000.bin at offset 0x40D00, size 25392 bytes

Destination:
  Nova Chrysalia BETA/Mods/XIII-2/DLC Restoration + Classic Serah/
"""

import os
import shutil
import struct

# ── Paths ──────────────────────────────────────────────────────────────────

NC_MODS       = r"C:\Users\dendo\Documents\FF 13 mods\Nova Chrysalia BETA\Mods\XIII-2"
SRC_NC_MOD    = os.path.join(NC_MODS, "DLC Restoration - Console Content")
SRC_OLD_MOD   = r"C:\Users\dendo\Downloads\FFXIII-2 - Console Content Patch (v2.3)\mod"
SRC_GAME      = r"C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY XIII-2\alba_data\chr\pc\c107\bin"
X000_BIN      = os.path.join(SRC_OLD_MOD, r"Data\btscene\pack\wdb\x000.bin")

DEST          = r"C:\Users\dendo\Documents\console_content_merge"

# mod_dlc WDB location inside x000.bin
MOD_DLC_OFFSET = 0x40D00
MOD_DLC_SIZE   = 25392

# ── Step 1: Mirror the existing NC mod ─────────────────────────────────────

print("Step 1: Copying existing NC mod...")
if os.path.exists(DEST):
    print(f"  Destination already exists — removing old copy first")
    shutil.rmtree(DEST)

shutil.copytree(SRC_NC_MOD, DEST)
print(f"  Copied {SRC_NC_MOD}")
print(f"    -> {DEST}")

# ── Step 2: Add c107 chr files ─────────────────────────────────────────────

print("\nStep 2: Adding c107 chr files...")
dest_c107 = os.path.join(DEST, r"Data\chr\pc\c107\bin")
os.makedirs(dest_c107, exist_ok=True)

# trb: use the DLC version from the old mod (711,664 bytes — cleaner console version)
src_trb = os.path.join(SRC_OLD_MOD, r"Data\chr\pc\c107\bin\c107.win32.trb")
dst_trb = os.path.join(dest_c107, "c107.win32.trb")
shutil.copy2(src_trb, dst_trb)
print(f"  trb  ({os.path.getsize(dst_trb):,} bytes) <- old mod")

# imgb + mpk files: use the game's existing loose files
for fname in ["c107.win32.imgb", "c107_def.win32.mpk", "c107_rain.win32.mpk", "c107_snow.win32.mpk"]:
    src = os.path.join(SRC_GAME, fname)
    dst = os.path.join(dest_c107, fname)
    shutil.copy2(src, dst)
    print(f"  {fname} ({os.path.getsize(dst):,} bytes) <- game")

# ── Step 3: Extract mod_dlc WDB from x000.bin ─────────────────────────────

print("\nStep 3: Extracting mod_dlc WDB from x000.bin...")
with open(X000_BIN, 'rb') as f:
    f.seek(MOD_DLC_OFFSET)
    wdb_bytes = f.read(MOD_DLC_SIZE)

# Verify it's a WPD file
if wdb_bytes[:3] != b'WPD':
    raise ValueError(f"Expected 'WPD' magic at offset 0x{MOD_DLC_OFFSET:X}, got {wdb_bytes[:4]!r}")

# Count records
record_count = struct.unpack_from('>I', wdb_bytes, 4)[0]
print(f"  WPD verified: {record_count} records, {MOD_DLC_SIZE:,} bytes")

dest_wdbpack = os.path.join(DEST, r"Data\db\resident\_wdbpack.bin")
os.makedirs(dest_wdbpack, exist_ok=True)

wdb_out = os.path.join(dest_wdbpack, "r_dlc_classic_serah.wdb")
with open(wdb_out, 'wb') as f:
    f.write(wdb_bytes)
print(f"  Written -> {wdb_out}")

# ── Step 4: Write updated modconfig.ini ────────────────────────────────────

print("\nStep 4: Writing modconfig.ini...")
modconfig = """\
[ModPackConfig]
Name\t\t\t= DLC Restoration + Classic Serah
Version\t\t\t= 1.0.0
Author\t\t\t= Krisan Thyme, compiled by dendo
GameEntry\t\t= 2
Summary\t\t\t= DLC costumes and weaponry restoration, plus Serah's Classic (XIII-1) outfit.
Image\t\t\t= image.png
Preview\t\t\t= preview.png
Banner                  = banner.png
Readme\t\t\t= readme.txt

[NovaChysaliaConfig]
DataPatch\t\t= true
ENPatch                 = false
JPPatch                 = false
ExtPatch                = false
CodePatch\t\t= true
Installed\t\t= true
ENInstalled             = false
JPInstalled             = false
"""

with open(os.path.join(DEST, "modconfig.ini"), 'w', encoding='utf-8') as f:
    f.write(modconfig)
print("  Written modconfig.ini")

# ── Step 5: Summary ────────────────────────────────────────────────────────

print("\n-- Summary ---------------------------------------------------")
total = 0
for root, dirs, files in os.walk(DEST):
    dirs.sort()
    for fname in sorted(files):
        fpath = os.path.join(root, fname)
        size = os.path.getsize(fpath)
        total += size
        rel = os.path.relpath(fpath, DEST)
        print(f"  {rel}  ({size:,})")
print(f"\nTotal: {total:,} bytes")
print(f"\nDone — mod created at:\n  {DEST}")
