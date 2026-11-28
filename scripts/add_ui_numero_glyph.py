#!/usr/bin/env python3
"""Append U+2116 to built-in UI fonts without re-rasterising existing glyphs.
Requires freetype-py. Existing bitmap, glyph and kerning data remain unchanged.
"""
from pathlib import Path
import re
import freetype

ROOT=Path(__file__).resolve().parents[1]
FONTS=ROOT/'lib/EpdFont/builtinFonts'
CP=0x2116

def add(name,size,family,weight):
    path=FONTS/(name+'.h');text=path.read_text()
    interval=re.search(r'(static const EpdUnicodeInterval '+name+r'Intervals\[\] = \{)(.*?)(\n\};)',text,re.S)
    rows=re.findall(r'\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}',interval[2])
    if any(int(a,0)<=CP<=int(b,0) for a,b,_ in rows):
        print(name,'already contains U+2116');return
    face=None
    for source in [FONTS/'source'/family/(family+'-'+weight+'.ttf'),FONTS/'source/NotoSans'/('NotoSans-'+weight+'.ttf')]:
        candidate=freetype.Face(str(source))
        if candidate.get_char_index(CP):face=candidate;break
    if face is None:raise RuntimeError('No source glyph for '+name)
    face.set_char_size(size<<6,size<<6,150,150)
    face.load_char(CP,freetype.FT_LOAD_RENDER|freetype.FT_LOAD_FORCE_AUTOHINT)
    slot=face.glyph;bm=slot.bitmap;packed=bytearray((bm.width*bm.rows+7)//8)
    for y in range(bm.rows):
        for x in range(bm.width):
            if bm.buffer[y*abs(bm.pitch)+x]>=102:
                bit=y*bm.width+x;packed[bit//8]|=0x80>>(bit%8)
    bitmap=re.search(r'(static const uint8_t '+name+r'Bitmaps\[)(\d+)(\] = \{)(.*?)(\n\};)',text,re.S)
    old_size=int(bitmap[2]);assert len(re.findall(r'0x[0-9A-Fa-f]{2}',bitmap[4]))==old_size
    suffix='\n    '+', '.join('0x%02x'%v for v in packed)+', // U+2116\n'
    text=text[:bitmap.start()]+bitmap[1]+str(old_size+len(packed))+bitmap[3]+bitmap[4]+suffix+bitmap[5]+text[bitmap.end():]
    glyph=re.search(r'(static const EpdGlyph '+name+r'Glyphs\[\] = \{)(.*?)(\n\};)',text,re.S)
    index=len(re.findall(r'\{[^}]+\}',glyph[2]));advance=(slot.linearHoriAdvance+2048)>>12
    row=f'\n    {{ {bm.width}, {bm.rows}, {advance}, {slot.bitmap_left}, {slot.bitmap_top}, {len(packed)}, {old_size} }}, // U+2116\n'
    text=text[:glyph.start()]+glyph[1]+glyph[2]+row+glyph[3]+text[glyph.end():]
    interval=re.search(r'(static const EpdUnicodeInterval '+name+r'Intervals\[\] = \{)(.*?)(\n\};)',text,re.S)
    lines=interval[2].splitlines();at=len(lines)
    for n,line in enumerate(lines):
        m=re.search(r'\{\s*(0x[0-9A-Fa-f]+|\d+)',line)
        if m and int(m[1],0)>CP:at=n;break
    lines.insert(at,f'    {{ 0x2116, 0x2116, 0x{index:X} }},')
    text=text[:interval.start()]+interval[1]+'\n'.join(lines)+interval[3]+text[interval.end():]
    pattern=r'(static const EpdFontData '+name+r' = \{\s*'+name+r'Bitmaps,\s*'+name+r'Glyphs,\s*'+name+r'Intervals,\s*)(\d+)(,)'
    text,n=re.subn(pattern,lambda m:m[1]+str(int(m[2])+1)+m[3],text);assert n==1
    text=text.replace(' * mode: 1-bit',' * U+2116 added by scripts/add_ui_numero_glyph.py; existing glyphs preserved.\n * mode: 1-bit',1)
    path.write_text(text)
    print(name,'added',len(packed),'bitmap bytes')

for size in (9,10,11,12,14):
    for weight in ('Regular','Bold'):add(f'inter_ui_{size}_{weight.lower()}',size,'Inter',weight)
for weight in ('Regular','Bold'):add(f'notosans_8_{weight.lower()}',8,'NotoSans',weight)
