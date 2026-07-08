"""
Generate lib/SecureNet/include/CrossPointRoots.h from CrossPointRoots.pem.

The .pem is the single source of truth for the curated TLS trust anchors (it is
also what scripts/validate_roots.py checks hosts against). This script emits a
C++ header holding the same bytes as one concatenated PEM string, so the
firmware loads exactly what the validator verified.

Comment lines (starting with '#') in the .pem are dropped from the embedded
string but the certificate labels are preserved as C comments for readability.

Runs both as a PlatformIO pre: hook and standalone (`python scripts/gen_roots.py`).
Idempotent: only rewrites the header when its content would change, so it never
churns the working tree or triggers needless rebuilds.
"""

import os

try:
    Import("env")  # noqa: F821 -- provided by PlatformIO when run as a build hook
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except (NameError, Exception):  # not under PlatformIO -> standalone invocation
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PEM_PATH = os.path.join(PROJECT_DIR, "lib", "SecureNet", "include", "CrossPointRoots.pem")
HDR_PATH = os.path.join(PROJECT_DIR, "lib", "SecureNet", "include", "CrossPointRoots.h")


def build_header(pem_text):
    lines = pem_text.splitlines()
    out = []
    out.append("// AUTO-GENERATED from CrossPointRoots.pem by scripts/gen_roots.py -- do not edit by hand.")
    out.append("// Curated TLS root set for the wolfSSL trust store (verified-first connect).")
    out.append("// Regenerate after editing the .pem: python scripts/gen_roots.py")
    out.append("#pragma once")
    out.append("")
    out.append("// clang-format off")
    out.append("// Concatenated PEM roots, loaded once via wolfSSL_CTX_load_verify_buffer().")
    out.append("inline constexpr char CROSSPOINT_ROOTS_PEM[] =")
    for line in lines:
        if line.startswith("#"):
            # Surface the human labels as C comments; skip them from the string.
            out.append("    // {}".format(line.lstrip("#").strip()))
            continue
        out.append('    "{}\\n"'.format(line))
    out.append("    ;")
    out.append("// clang-format on")
    out.append("")
    return "\n".join(out)


def main():
    with open(PEM_PATH, "r", encoding="utf-8") as f:
        pem_text = f.read()
    header = build_header(pem_text)
    existing = None
    if os.path.exists(HDR_PATH):
        with open(HDR_PATH, "r", encoding="utf-8") as f:
            existing = f.read()
    if existing == header:
        return  # up to date; no write, no rebuild churn
    with open(HDR_PATH, "w", encoding="utf-8") as f:
        f.write(header)
    print("Generated {}".format(os.path.relpath(HDR_PATH, PROJECT_DIR)))


main()
