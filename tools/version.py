# PlatformIO pre-build script: defines FW_VERSION for the firmware.
#
# The release workflow sets the FW_VERSION environment variable to the tag
# being built. Local builds use `git describe`, or "dev" outside a checkout.
import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

version = os.environ.get("FW_VERSION", "").strip()
if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],  # noqa: F821
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except Exception:
        version = ""
if not version:
    version = "dev"

env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
