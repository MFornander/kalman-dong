"""PlatformIO post-script: compiles VERSION and the git commit into the firmware.

Only the project's own sources get the two macros (KD_VERSION, KD_COMMIT),
so a new commit rebuilds src/ and not every library.
"""
import subprocess
from pathlib import Path

Import("env", "projenv")  # noqa: F821 (provided by PlatformIO)

root = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
version = (root / "VERSION").read_text().strip()
try:
    commit = subprocess.run(
        ["git", "describe", "--always", "--dirty", "--abbrev=7"],
        cwd=root, capture_output=True, text=True, check=True,
    ).stdout.strip()
except (OSError, subprocess.CalledProcessError):
    commit = "unknown"

projenv.Append(CPPDEFINES=[  # noqa: F821
    ("KD_VERSION", env.StringifyMacro(version)),  # noqa: F821
    ("KD_COMMIT", env.StringifyMacro(commit)),  # noqa: F821
])
print(f"kalman-dong {version} ({commit})")
