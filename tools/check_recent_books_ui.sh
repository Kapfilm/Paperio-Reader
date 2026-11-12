#!/usr/bin/env bash
# Run from any directory; no network, Python packages, PlatformIO or hardware required.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/recent_books_ui
python3 - <<'PY'
import json
import pathlib
import re

root = pathlib.Path.cwd()
header = (root / "src/activities/home/RecentBooksActivity.h").read_text()
source = (root / "src/activities/home/RecentBooksActivity.cpp").read_text()
compact = source.split("if (usesCompactCoverGrid()) {", 1)[1].split("const int slotWidth", 1)[0]
values = {}
for language, suffix in [("russian", "Ru"), ("english", "En")]:
    text = (root / f"lib/I18n/translations/{language}.yaml").read_text()
    captions = [
        ("STR_RECENT_VIEW_HINT", "hint"),
        ("STR_RECENT_HOME_FULL", "homeFull"),
        ("STR_HOME", "home"),
        ("STR_BACK", "back"),
        ("STR_OPEN", "open"),
        ("STR_REMOVE", "remove"),
        ("STR_INFO", "info"),
        ("STR_SELECT", "select"),
        ("STR_DELETE", "delete"),
        ("STR_BOOKMARK_RENAME_SHORT", "renameShort"),
    ]
    for key, name in captions:
        value = json.loads(re.search(rf"^{key}:\s*(.+)$", text, re.M).group(1))
        values[name + suffix] = value
numbers = {}
for constant, name in [("GRID_CELL_HEIGHT", "cellHeight"), ("GRID_LABEL_HEIGHT", "labelHeight"), ("GRID_THUMB_MARGIN", "gridMargin")]:
    numbers[name] = int(re.search(rf"{constant}\s*=\s*(\d+)", header).group(1))
for name in ["rows", "outerMargin", "cellGap"]:
    numbers[name] = int(re.search(rf"constexpr int {name}\s*=\s*(\d+)", compact).group(1))
lines = ["#pragma once", "namespace recent_probe {"]
lines.extend(f"inline constexpr const char* {key} = {json.dumps(value, ensure_ascii=False)};" for key, value in values.items())
lines.extend(f"inline constexpr int {key} = {value};" for key, value in numbers.items())
lines.append("}")
(root / "build/recent_books_ui/RecentProbeInputs.h").write_text("\n".join(lines) + "\n")
PY
"${CXX:-c++}" -std=c++20 -O2 \
  -I test/ui_text_size/shims -I build/recent_books_ui -I src -I lib/EpdFont -I lib/Utf8 \
  tools/recent_books_ui_probe.cpp src/UiFonts.cpp lib/EpdFont/EpdFont.cpp \
  lib/EpdFont/EpdFontFamily.cpp lib/Utf8/Utf8.cpp \
  -o build/recent_books_ui/probe
build/recent_books_ui/probe
