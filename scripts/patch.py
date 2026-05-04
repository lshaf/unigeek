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

