# python
Import("env")
import os
import sys
import traceback

pioenv    = env.subst("${PIOENV}")
BUILD_DIR = env.subst("$BUILD_DIR")
PROJECT_DIR = env.get("PROJECT_DIR")
OUTPUT_PATH = os.path.join(PROJECT_DIR, "unigeek-latest.bin")

# Determine boot offset based on the MCU.
# ESP32 uses 0x1000, while others (like ESP32-S2/S3) use 0.
board = env.BoardConfig()
mcu = board.get("build.mcu", "")
boot_offset = 0x1000 if mcu == "esp32" else 0

DEFAULT_OFFSETS = {
    "boot": boot_offset,
    "part": 0x8000,
    "app": 0x10000,
}

FILES = {
    "boot": os.path.join(BUILD_DIR, "bootloader.bin"),
    "part": os.path.join(BUILD_DIR, "partitions.bin"),
    "app": os.path.join(BUILD_DIR, "firmware.bin"),
}

def parse_offsets():
    out = DEFAULT_OFFSETS.copy()
    boot = os.environ.get("BOOT_OFFSET")
    part = os.environ.get("PART_OFFSET")
    app = os.environ.get("APP_OFFSET")
    if boot:
        out["boot"] = int(boot, 0)
    if part:
        out["part"] = int(part, 0)
    if app:
        out["app"] = int(app, 0)
    return out

def read_file(path):
    with open(path, "rb") as f:
        return f.read()

def _merge_action(source, target, env):
    try:
        offsets = parse_offsets()
        items = []
        for name, path in FILES.items():
            if os.path.isfile(path):
                data = read_file(path)
                items.append((name, offsets[name], len(data), data, path))
                print("[post-build] found", name, "->", path, "size", hex(len(data)))
            else:
                print("[post-build] missing", name, "->", path)

        if not any(i[0] == "app" for i in items):
            print("[post-build] ERROR: firmware.bin not found; aborting merge")
            return

        # check overlaps
        for i in range(len(items)):
            n1,o1,s1,_,_ = items[i]
            for j in range(i+1, len(items)):
                n2,o2,s2,_,_ = items[j]
                if o1 < o2 + s2 and o2 < o1 + s1:
                    print("[post-build] ERROR: overlap between", n1, "and", n2)
                    return

        final_size = max(o + s for _,o,s,_,_ in items)
        buf = bytearray(b'\x00') * final_size
        for name, offset, size, data, path in items:
            buf[offset:offset+size] = data
            print("[post-build] placed", name, "at", hex(offset), "size", hex(size))

        with open(OUTPUT_PATH, "wb") as f:
            f.write(buf)
        print("[post-build] merged image written to", OUTPUT_PATH, "size", hex(len(buf)))
    except Exception:
        print("[post-build] Exception during merge:")
        traceback.print_exc()

# register to run after ELF is produced
env.AddPostAction(FILES["app"], _merge_action)
print(f"[post-build] Registered merge action for {FILES['app']}")