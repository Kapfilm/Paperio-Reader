"""
PlatformIO pre-build hook: ensure the local uzlib fixes are present in the
vendored copy under lib/uzlib/.

The fixes are committed directly into lib/uzlib/ (so a normal checkout builds
correctly and the working tree never flaps), and the corresponding .patch files
in scripts/uzlib_patches/ document exactly how our copy diverges from upstream.
This hook re-applies a patch only when it is *missing* -- e.g. right after
someone re-vendors a clean upstream tarball over lib/uzlib/. When the fix is
already present (the normal case) it is a silent no-op, so builds never modify
the tracked source. Mirrors scripts/patch_jpegdec.py, except the target is a
file tracked in *this* repo rather than a download under .pio/libdeps.

Idempotency: a patch that already applies in reverse is treated as present and
skipped. A patch that applies neither way means the vendored source drifted from
what the patch expects -- the build is failed rather than half-patched.

Runs both as a PlatformIO pre: script and standalone (`python scripts/patch_uzlib.py`).
"""

import os
import subprocess
import sys

try:
    Import("env")  # noqa: F821 -- provided by PlatformIO when run as a build hook
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except (NameError, Exception):  # not under PlatformIO -> standalone invocation
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PATCH_DIR = os.path.join(PROJECT_DIR, "scripts", "uzlib_patches")


def _git_apply_succeeds(patch_path, *, reverse):
    cmd = ["git", "apply", "--check"]
    if reverse:
        cmd.append("--reverse")
    cmd.append(patch_path)
    return subprocess.run(
        cmd, cwd=PROJECT_DIR, capture_output=True, text=True
    ).returncode == 0


def _apply_one(patch_path):
    name = os.path.basename(patch_path)
    # Already applied (patch reverses cleanly) -> nothing to do.
    if _git_apply_succeeds(patch_path, reverse=True):
        return
    # Not applied and does not apply forward -> the vendored source drifted from
    # what the patch expects; fail loudly rather than ship a half-patched uzlib.
    if not _git_apply_succeeds(patch_path, reverse=False):
        result = subprocess.run(
            ["git", "apply", "--check", patch_path],
            cwd=PROJECT_DIR, capture_output=True, text=True,
        )
        sys.stderr.write(
            "ERROR: uzlib patch %s does not apply cleanly:\n%s%s\n"
            % (name, result.stdout, result.stderr)
        )
        raise SystemExit(1)
    subprocess.run(["git", "apply", patch_path], cwd=PROJECT_DIR, check=True)
    print("Applied uzlib patch: %s" % name)


def patch_uzlib():
    if not os.path.isdir(PATCH_DIR):
        raise RuntimeError(
            "uzlib patches missing -- aborting build (expected directory %s)" % PATCH_DIR
        )
    patches = sorted(
        os.path.join(PATCH_DIR, n) for n in os.listdir(PATCH_DIR) if n.endswith(".patch")
    )
    if not patches:
        raise RuntimeError(
            "uzlib patches missing -- aborting build (no .patch files in %s)" % PATCH_DIR
        )
    for patch in patches:
        _apply_one(patch)


patch_uzlib()
