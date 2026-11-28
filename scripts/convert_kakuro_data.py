#!/usr/bin/env python3
"""Pack MIT Kakuro banks by Omer Faruk Inel into flash; no runtime SD dependency."""
import hashlib
from pathlib import Path
import sys
banks = [('6x6-easy', 6), ('7x7-medium', 7), ('8x8-hard', 8)]
source = Path(sys.argv[1])
out = ['#pragma once', '#include <cstdint>', '// MIT © 2026 Omer Faruk Inel; see THIRD_PARTY_NOTICES.md.', 'namespace kakuro_data {']
for level, (name, n) in enumerate(banks):
    raw = (source / (name + '.txt')).read_bytes()
    lines = raw.decode('ascii').splitlines()
    assert len(lines) == 200 and len(set(lines)) == 200
    out += [f'// {name}.txt SHA256: {hashlib.sha256(raw).hexdigest()}', f'inline constexpr uint8_t bank{level}[200][{(n*n+1)//2}] = {{']
    for line in lines:
        assert len(line) == n*n and set(line) <= set('#123456789')
        digits = [0 if c == '#' else int(c) for c in line]
        if len(digits) % 2: digits += [0]
        packed = [(digits[i] << 4) | digits[i+1] for i in range(0, len(digits), 2)]
        out += ['  {' + ','.join(f'0x{x:02x}' for x in packed) + '},']
    out += ['};']
out += ['}  // namespace kakuro_data', '']
Path('src/activities/games/kakuro/KakuroData.h').write_text('\n'.join(out))
