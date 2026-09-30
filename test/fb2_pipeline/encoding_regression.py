#!/usr/bin/env python3
"""Exercise cold/warm FB2 preparation with real converter, in plain and ZIP form.
Usage: python3 encoding_regression.py /path/to/fb2_pipeline /tmp/new-output-dir
"""
import pathlib
import subprocess
import sys
import zipfile

runner = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=False)
body = "Привет мир. Проверка русского текста и сохранения кодировки. " * 12
for name, declared, encoding in [
    ("utf8", "UTF-8", "utf-8"),
    ("cp1251", "windows-1251", "cp1251"),
    ("koi8r", "KOI8-R", "koi8-r"),
    ("mislabeled", "windows-1251", "utf-8"),
    ("no-declaration", None, "utf-8"),
]:
    declaration = f'<?xml version="1.0" encoding="{declared}"?>' if declared else ""
    xml = declaration + '<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">'
    xml += '<description><title-info><book-title>Проверка кодировки</book-title><lang>ru</lang></title-info></description>'
    xml += '<body><section id="chapter"><title><p>Русская глава</p></title>'
    xml += ''.join(f'<p id="p{i}">{body}</p>' for i in range(100))
    xml += '</section></body></FictionBook>'
    book = root / (name + '.fb2')
    book.write_bytes(xml.encode(encoding))
    archive = root / (name + '.fb2.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(book, 'book.fb2')
    for source in [book, archive]:
        label = source.name
        result = subprocess.run([str(runner), str(source), str(root / (label + '-cache'))],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / (label + '.log')).write_bytes(result.stdout)
        text = result.stdout.decode('utf-8', errors='replace')
        assert result.returncode == 0, f'{label}: pipeline failed ({result.returncode})'
        assert 'Русская глава' in text, f'{label}: corrupted Cyrillic TOC'
        if name in ('cp1251', 'koi8r'):
            outputs = list((root / (label + '-cache')).rglob('.source.utf8.fb2'))
            assert outputs, f'{label}: no transcoded source'
            assert body in outputs[0].read_text(encoding='utf-8'), f'{label}: corrupted body text'
        print('PASS', label)
