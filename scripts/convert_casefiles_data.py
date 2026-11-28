#!/usr/bin/env python3
"""Embed the unchanged MIT Case Files banks. Usage: script [directory with casefiles-*.txt]."""
from pathlib import Path
import sys,hashlib
src=Path(sys.argv[1] if len(sys.argv)>1 else '/private/tmp')
root=Path(__file__).resolve().parents[1]/'src/activities/games/casefiles'
out=['#pragma once','#include <cstddef>','namespace casefiles {']
for name in ['easy','medium','hard']:
 p=src/f'casefiles-{name}.txt';data=p.read_text();rows=data.splitlines();assert len(rows)==200
 out += [f'// Original {name}.txt SHA256: {hashlib.sha256(p.read_bytes()).hexdigest()}',f'inline constexpr const char* {name}Bank[200] = {{']
 out += ['  "'+r+'",' for r in rows];out += ['};']
out += ['inline const char* bankLine(int level, int index) {','  if(level<0||level>2||index<0||index>=200)return nullptr;','  return level==0?easyBank[index]:level==1?mediumBank[index]:hardBank[index];','}','}']
(root/'CaseFilesData.h').write_text('\n'.join(out)+'\n')
