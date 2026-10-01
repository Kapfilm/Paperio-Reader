from pathlib import Path
import sys
import zipfile
root = Path(__file__).resolve().parent
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as out:
    for p in sorted(root.rglob('*')):
        if not p.is_file() or '__pycache__' in p.parts or p.suffix == '.zip':
            continue
        name = p.relative_to(root).as_posix()
        if p.suffix == '.py' or p.name in ('LICENSE', 'LICENSE-pyserial.txt', 'UPSTREAM.md'):
            info = zipfile.ZipInfo(name, (2026, 10, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            out.writestr(info, p.read_bytes())
