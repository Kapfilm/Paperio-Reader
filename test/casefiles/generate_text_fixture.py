from pathlib import Path
import json,sys
source=Path(sys.argv[1]);out=Path(sys.argv[2]);data=json.loads(source.read_text())
keys=list(data);out.parent.mkdir(parents=True,exist_ok=True)
s='#pragma once\n#include <cstddef>\nenum class StrId {'+','.join(keys)+'};\ninline bool caseRussian=true;\nstruct CaseI18n {const char* get(StrId id)const {\n'
for lang in ['ru','en']:s+='static const char* '+lang+'[] = {'+','.join(json.dumps(data[k][lang],ensure_ascii=False) for k in keys)+'};\n'
s+='return (caseRussian?ru:en)[static_cast<size_t>(id)];}};\ninline CaseI18n I18N;\n';out.write_text(s)
