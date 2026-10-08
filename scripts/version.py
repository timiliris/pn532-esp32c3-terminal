# Injects the firmware version from git (tag, or short commit) as FW_VERSION.
import subprocess

try:
    v = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"],
        stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    v = "dev"
print('-DFW_VERSION=\\"%s\\"' % (v.lstrip("v") or "dev"))
