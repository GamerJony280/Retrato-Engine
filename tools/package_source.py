#!/usr/bin/env python3
"""Source-only deterministic release archive. Standard library; no engine dependency."""
from pathlib import Path
import argparse,hashlib,zipfile
ROOT=Path(__file__).resolve().parents[1]
FOLDERS={'src','include','apps','nova','examples','tests','assets','config','cmake','shaders','third_party','tools','.github','platform','templates'}
FILES={'CMakeLists.txt','CMakePresets.json','README.md','README.es.md','CONTRIBUTING.md','SECURITY.md','LICENSE','THIRD_PARTY.md','CHANGELOG.md','BUILD.sh','BUILD.cmd','.gitignore','.gitattributes','.clang-format'}
SKIP={'build','bin','dist','__pycache__','CMakeFiles','Testing','.git','.gradle','.cxx','.externalNativeBuild','local.properties','CMakeCache.txt','compile_commands.json'}
# .obj is also a supported 3D asset: build directories are filtered separately.
SUFFIXES={'.pdf','.zip','.o','.a','.so','.dll','.exe','.ppm','.embsave','.pyc','.lib','.pdb','.ilk','.exp','.idb','.apk','.aab','.dex','.class'}
def package(root,output,label,engine=False):
    root=root.resolve();output=output.resolve(); entries=[]
    for p in sorted(root.rglob('*')):
        if not p.is_file() or p.is_symlink() or p.resolve()==output: continue
        rel=p.relative_to(root)
        if engine and rel.parts[0] not in FOLDERS|FILES|{'docs'}: continue
        if any(part in SKIP for part in rel.parts) or p.suffix.lower() in SUFFIXES: continue
        data=p.read_bytes(); entries.append((rel.as_posix(),data))
    output.parent.mkdir(parents=True,exist_ok=True)
    manifest=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in entries).encode()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in entries+[('SOURCE_SHA256.txt',manifest)]:
            info=zipfile.ZipInfo(label+'/'+name,date_time=(2026,10,5,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(0o100644<<16)
            archive.writestr(info,data,compresslevel=9)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip(): raise RuntimeError('Archive CRC failure')
    print(f'{output.name}: {len(entries)} source files, {output.stat().st_size} bytes compressed')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path)
    parser.add_argument('--root',type=Path,default=ROOT);parser.add_argument('--label',default='Retrato-Engine')
    parser.add_argument('--game',action='store_true');args=parser.parse_args()
    package(args.root,args.output,args.label,engine=not args.game)
