from os import remove, rename
from os.path import isfile, join
import sys

Import("env")  # type: ignore

# Verify platform version matches expected
EXPECTED_PLATFORM_VERSION = "6.13.0"
installed_version = env.PioPlatform().version
if installed_version != EXPECTED_PLATFORM_VERSION:
    sys.stderr.write(
        "\n[ERROR] espressif32 platform version mismatch!\n"
        "  Expected: %s\n"
        "  Installed: %s\n"
        "  Run: pio pkg update -p espressif32@%s\n\n"
        % (EXPECTED_PLATFORM_VERSION, installed_version, EXPECTED_PLATFORM_VERSION)
    )
    env.Exit(1)

# Expose the PlatformIO env name (== board id, e.g. "m5_cardputer") to the firmware
# as FIRMWARE_BOARD so the UART/BLE INFO response can report which board is running.
env.Append(CPPDEFINES=[("FIRMWARE_BOARD", env.StringifyMacro(env["PIOENV"]))])

FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
board_mcu = env.BoardConfig()
mcu = board_mcu.get("build.mcu", "esp32s3")
patchflag_path = join(FRAMEWORK_DIR, "tools", "sdk", mcu, "lib", ".patched")
print("[pre-build] Applying libnet80211.a patch for %s..." % mcu)

# patch file only if we didn't do it befored
if not isfile(patchflag_path):
    print("[pre-build] Patching libnet80211.a to weaken symbol 's'...")
    original_file = join(FRAMEWORK_DIR, "tools", "sdk", mcu, "lib", "libnet80211.a")
    patched_file = join(FRAMEWORK_DIR, "tools", "sdk", mcu, "lib", "libnet80211.a.patched")

    env.Execute(
        "pio pkg exec -p toolchain-xtensa-%s -- xtensa-%s-elf-objcopy  --weaken-symbol=s %s %s"
        % (mcu, mcu, original_file, patched_file)
    )
    if isfile("%s.old" % original_file):
        remove("%s.old" % original_file)
    rename(original_file, "%s.old" % original_file)
    env.Execute(
        "pio pkg exec -p toolchain-xtensa-%s -- xtensa-%s-elf-objcopy  --weaken-symbol=ieee80211_raw_frame_sanity_check %s %s"
        % (mcu, mcu, patched_file, original_file)
    )

    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))
    print("[pre-build] Patch applied.")
else:
    print("[pre-build] Patch already applied, skipping.")



# ── RMT driver: move its ISR out of IRAM ────────────────────────────────────
#
# The IDF 4.4 RMT driver marks rmt_driver_isr_default() and rmt_fill_memory()
# IRAM_ATTR, so linking rmt_driver_install() drags ~1.5 KB into iram0_0_seg.
# On the classic-ESP32 boards (CYD, StickC Plus, T-Display) that is the
# difference between linking and "iram0_0_seg overflowed by 660 bytes" — the
# BT controller, FreeRTOS and libc already account for ~66 KB of the segment
# and none of it is ours to give back.
#
# An ISR only *has* to live in IRAM when its interrupt is allocated with
# ESP_INTR_FLAG_IRAM; without that flag the IDF disables the interrupt while
# the flash cache is down, so a flash-resident handler is safe. Every RMT user
# here qualifies:
#   - RmtRf (SubGHz RX/TX) calls rmt_driver_install(ch, n, 0)
#   - FastLED either calls rmt_driver_install(ch, 0, 0) or, on its custom
#     path, registers its own IRAM handler and never installs the driver
# so rmt_driver_isr_default is never the IRAM-flagged handler.
#
# Renaming the object's .iram1.* sections to .text.*/.literal.* relocates them
# to flash, which keeps SubGHz on every board instead of gating it off the
# memory-tight ones.
rmt_flag_path = join(FRAMEWORK_DIR, "tools", "sdk", mcu, "lib", ".patched_rmt")

# Opt-in: this rewrites libdriver.a inside the SHARED framework package, which
# affects every PlatformIO project on the machine. Enable deliberately with
#   set UNIGEEK_PATCH_RMT=1   (or export UNIGEEK_PATCH_RMT=1)
import os as _os
_rmt_opt_in = _os.environ.get("UNIGEEK_PATCH_RMT") == "1"

if _rmt_opt_in and not isfile(rmt_flag_path):
    import re
    import subprocess
    import tempfile
    import shutil

    lib_dir = join(FRAMEWORK_DIR, "tools", "sdk", mcu, "lib")
    libdriver = join(lib_dir, "libdriver.a")
    toolchain = env.PioPlatform().get_package_dir("toolchain-xtensa-%s" % mcu)
    prefix = join(toolchain, "bin", "xtensa-%s-elf-" % mcu)

    print("[pre-build] Relocating RMT driver ISR out of IRAM (%s)..." % mcu)
    workdir = tempfile.mkdtemp(prefix="rmtpatch_")
    try:
        shutil.copy(libdriver, join(lib_dir, "libdriver.a.old"))
        subprocess.check_call([prefix + "ar", "x", libdriver, "rmt.c.obj"], cwd=workdir)
        obj = join(workdir, "rmt.c.obj")

        headers = subprocess.check_output([prefix + "objdump", "-h", obj]).decode("utf-8", "replace")
        sections = sorted(set(re.findall(r"\s(\.iram1\.[\w.]+)", headers)))
        if not sections:
            raise RuntimeError("no .iram1.* sections found in rmt.c.obj")

        args = [prefix + "objcopy"]
        for s in sections:
            if s.endswith(".literal"):
                dest = ".literal" + s[len(".iram1"):-len(".literal")]
            else:
                dest = ".text" + s[len(".iram1"):]
            args += ["--rename-section", "%s=%s" % (s, dest)]
        args += [obj, obj + ".patched"]
        subprocess.check_call(args)
        shutil.move(obj + ".patched", obj)
        subprocess.check_call([prefix + "ar", "r", libdriver, "rmt.c.obj"], cwd=workdir)

        with open(rmt_flag_path, "w") as fp:
            fp.write("")
        print("[pre-build] RMT ISR relocated to flash (%d sections)." % len(sections))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
else:
    if not _rmt_opt_in:
        print("[pre-build] RMT ISR patch disabled (set UNIGEEK_PATCH_RMT=1 to enable).")
    else:
        print("[pre-build] RMT ISR patch already applied, skipping.")
