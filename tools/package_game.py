#!/usr/bin/env python3
"""Bundle an existing desktop executable/content, without the engine/build tree."""
import argparse, shutil, tempfile, zipfile
from pathlib import Path
def main():
    p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('output',type=Path);p.add_argument('--content',type=Path,required=True);p.add_argument('--runtime-library',type=Path,action='append',default=[]);a=p.parse_args()
    if not a.binary.is_file() or not a.content.is_dir():p.error('Provide a built executable and content directory')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);shutil.copy2(a.binary,root/a.binary.name);shutil.copytree(a.content,root/'content')
        for lib in a.runtime_library:
            if not lib.is_file():p.error('Runtime library does not exist: '+str(lib))
            shutil.copy2(lib,root/lib.name)
        with zipfile.ZipFile(a.output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for f in sorted(root.rglob('*')):
                if f.is_file():z.write(f,f.relative_to(root))
    print(a.output)
if __name__=='__main__':main()
