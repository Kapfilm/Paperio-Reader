#!/usr/bin/env python3
"""Compare real cold FB2 pipelines and exact anchor maps with fresh caches.
Usage: indexing_regression.py BASELINE_PIPELINE CANDIDATE_PIPELINE OUTPUT_DIR
The output directory must not exist. No device timing claims are made.
"""
import pathlib
import re
import struct
import subprocess
import sys
import zipfile

baseline, candidate = [str(pathlib.Path(arg).resolve()) for arg in sys.argv[1:3]]
out = pathlib.Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=False)


def anchors(cache):
    data = (cache / '.fb2_anchors.bin').read_bytes()
    assert data[:8] == b'FB2ANC01'
    version, source, count, size, buckets, start = struct.unpack_from('<6I', data, 8)
    assert size == len(data)
    result, position = {}, start
    for _ in range(count):
        _, chapter, _, length = struct.unpack_from('<QIIH', data, position)
        position += 18
        key = data[position:position + length]
        position += length
        result[key] = min(chapter, result.get(key, chapter))
    assert position == size
    return result


for name, chapters, paragraphs, inline in [('ordinary', 40, 30, False),
                                          ('inline-long', 1, 1200, True),
                                          ('short-chapters', 300, 2, True)]:
    text = '<FictionBook xmlns:l="http://www.w3.org/1999/xlink"><description><title-info><book-title>Index regression</book-title><lang>ru</lang></title-info></description><body>'
    for i in range(chapters):
        text += f'<section id="ch{i}"><title><p>Chapter {i}</p></title>'
        for j in range(paragraphs):
            attr = f' id="p{i}_{j}"' if inline else ''
            text += f'<p{attr}>' + 'Readable text. ' * 22 + '</p>'
        text += '<p><a l:href="#note">Note</a></p></section>'
    text += '</body><body name="notes"><section id="note"><title><p>Note</p></title><p>Note text.</p></section></body></FictionBook>'
    book = out / (name + '.fb2')
    book.write_text(text)
    for zipped in (False, True):
        path = book
        case = name + ('-zip' if zipped else '')
        if zipped:
            path = book.with_suffix('.fb2.zip')
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
                z.write(book, book.name)
        maps = []
        for label, binary in [('before', baseline), ('after', candidate)]:
            cache = out / (case + '-' + label)
            run = subprocess.run([binary, str(path), str(cache)], capture_output=True, text=True)
            (out / (case + '-' + label + '.log')).write_text(run.stdout + run.stderr)
            assert run.returncode == 0, (case, label, run.returncode)
            print(case, label, re.search(r'^FB2_LOAD.*$', run.stdout, re.M).group(), flush=True)
            package = pathlib.Path(re.search(r'^PACKAGE chapters=\d+ path=(.*)$', run.stdout, re.M).group(1))
            maps.append(anchors(package.parent))
        assert maps[0] == maps[1], case
        expected = {i.encode() for i in re.findall(r'\bid="([^"]+)"', text)}
        assert set(maps[1]) == expected, case
        print('PASS exact anchor mapping and full pipeline:', case, flush=True)
