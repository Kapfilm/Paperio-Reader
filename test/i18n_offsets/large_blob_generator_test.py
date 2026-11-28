#!/usr/bin/env python3
"""Compile and execute actual generated EN/RU tables larger than 64 KiB.

This catches both generator-width and runtime fallback-mask regressions.  The
fixture includes multi-byte Russian, a shared string after the first 32 KiB,
a second local string beyond 64 KiB, and English fallback beyond 64 KiB.
No project-generated files are modified: all output lives in TemporaryDirectory.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    spec = importlib.util.spec_from_file_location("paperio_i18n_generator", root / "scripts/gen_i18n.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    keys = ["STR_ZERO", "STR_BIG_A", "STR_SHARED_MID", "STR_BIG_B", "STR_LOCAL_TAIL", "STR_SHARED_TAIL"]
    translations = {
        "STR_ZERO": ["zero", "ноль"],
        "STR_BIG_A": ["A" * 40000, "Я" * 21000],
        "STR_SHARED_MID": ["shared-after-first", "shared-after-first"],
        "STR_BIG_B": ["B" * 40000, "Б" * 21000],
        "STR_LOCAL_TAIL": ["english-tail", "русский-хвост"],
        "STR_SHARED_TAIL": ["shared-after-second", "shared-after-second"],
    }
    # The fixture must continue to exercise both limits even if edited later.
    assert sum(len(translations[k][0].encode("utf-8")) + 1 for k in keys) > 65535
    assert sum(len(translations[k][1].encode("utf-8")) + 1 for k in keys
               if translations[k][0] != translations[k][1]) > 65535
    with tempfile.TemporaryDirectory(prefix="paperio-i18n-offsets-") as temp:
        out = Path(temp)
        generator.generate_keys_header(["EN", "RU"], ["English", "Русский"], keys, str(out / "I18nKeys.h"))
        generator.generate_strings_header(["EN", "RU"], ["English", "Русский"], str(out / "I18nStrings.h"))
        generator.generate_strings_cpp(["EN", "RU"], ["English", "Русский"], keys, translations,
                                       str(out / "I18nStrings.cpp"))
        source = [
            '#include "I18nStrings.h"', '#include "I18nOffset.h"',
            '#include <cstdint>', '#include <cstring>', '#include <iostream>',
            'int main() {',
            '  using namespace i18n_strings;',
            '  static_assert(sizeof(OFFSETS_EN[0]) == 4);',
            '  static_assert(sizeof(OFFSETS_RU[0]) == 4);',
            '  if (OFFSETS_RU[0] != 0) return 10;',
            '  if (OFFSETS_EN[2] <= 32767 || OFFSETS_EN[5] <= 65535) return 11;',
            '  if (OFFSETS_RU[4] <= 65535 || OFFSETS_RU[4] & 0x80000000u) return 12;',
            '  if (!(OFFSETS_RU[2] & 0x80000000u) || !(OFFSETS_RU[5] & 0x80000000u)) return 13;',
        ]
        for i, key in enumerate(keys):
            for lang_idx, lang in enumerate(("EN", "RU")):
                # ensure_ascii=False lets the compiler verify exact UTF-8 bytes,
                # including Cyrillic offsets; JSON quoting is a C++ string literal.
                expected = json.dumps(translations[key][lang_idx], ensure_ascii=False)
                source += [
                    f'  if (std::strcmp(resolveI18nOffset(STRINGS_{lang}_DATA, STRINGS_EN_DATA, OFFSETS_{lang}[{i}]),',
                    f'                  {expected}) != 0) {{ std::cerr << "{lang} {key}"; return 20; }}',
                ]
        source += ['  std::cout << "PASS: generated EN/RU blobs >64 KiB, UTF-8 and fallback resolution\\n";',
                   '  return 0;', '}']
        (out / "check.cpp").write_text("\n".join(source) + "\n", encoding="utf-8")
        subprocess.run([args.cxx, "-std=c++20", "-O1", "-I", str(out), "-I", str(root / "lib/I18n"),
                        str(out / "check.cpp"), str(out / "I18nStrings.cpp"), "-o", str(out / "check")], check=True)
        subprocess.run([str(out / "check")], check=True)


if __name__ == "__main__":
    main()
