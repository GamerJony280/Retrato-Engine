#!/usr/bin/env python3
"""Compile/run an external consumer against a RELOCATED installed SDK."""
import argparse, shutil, subprocess, tempfile, os, json
from pathlib import Path
def main():
    p=argparse.ArgumentParser();p.add_argument('prefix',type=Path);p.add_argument('--cmake',default='cmake');p.add_argument('--nova',action='store_true');p.add_argument('--lua',action='store_true');p.add_argument('--define',action='append',default=[]);a=p.parse_args()
    template=Path(__file__).resolve().parents[1]/'templates/game'
    with tempfile.TemporaryDirectory(prefix='Retrato SDK ñ spaces ') as tmp:
        root=Path(tmp);sdk=root/'relocated SDK';shutil.copytree(a.prefix,sdk)
        configs=list(sdk.glob('lib*/cmake/Retrato/RetratoConfig.cmake'))
        if len(configs)!=1:raise RuntimeError('Expected exactly one installed RetratoConfig.cmake')
        source=root/'independent game';shutil.copytree(template,source);build=root/'build'
        args=[a.cmake,'-S',str(source),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DRetrato_DIR='+str(configs[0].parent),'-DGAME_USE_NOVA='+('ON' if a.nova else 'OFF'),'-DGAME_USE_LUA='+('ON' if a.lua else 'OFF')]+['-D'+d for d in a.define]
        subprocess.run(args,check=True)
        subprocess.run([a.cmake,'--build',str(build),'--config','Release','--parallel','2'],check=True)
        name='retrato_game'+('.exe' if os.name=='nt' else '')
        exe=next((x for x in [build/name,build/'Release'/name] if x.is_file()),None)
        if not exe:raise RuntimeError('Independent game executable not found')
        probe=source/'content/resource-copy-probe.txt';probe.write_text('asset-only change')
        subprocess.run([a.cmake,'--build',str(build),'--config','Release','--target','retrato_game','--parallel','2'],check=True)
        if (exe.parent/'content'/probe.name).read_text()!='asset-only change':raise RuntimeError('Asset-only changes were not copied')
        # Run from a different cwd: assets must resolve beside the executable.
        subprocess.run([str(exe),'--headless','--frames','3'],cwd=root,check=True)
        rejected=subprocess.run(args+['-DRETRATO_EXPECT_ARCH=definitely-wrong'],capture_output=True,text=True)
        if rejected.returncode==0 or 'SDK is' not in rejected.stdout+rejected.stderr:raise RuntimeError('SDK failed to reject an architecture mismatch')
        # Foreign SDK metadata must be rejected without RETRATO_EXPECT_ARCH.
        metadata=json.loads((sdk/'share/retrato/retrato-target.json').read_text(encoding='utf-8'))
        original=configs[0].read_text(encoding='utf-8')
        try:
            for key,actual,wrong in [('OS',metadata['os'],'linux' if metadata['os']=='windows' else 'windows'),('ARCH',metadata['arch'],'x86_64' if metadata['arch']=='arm64' else 'arm64')]:
                needle=f'set(Retrato_TARGET_{key} "{actual}")'
                if needle not in original:raise RuntimeError('SDK metadata declaration not found')
                configs[0].write_text(original.replace(needle,f'set(Retrato_TARGET_{key} "{wrong}")'),encoding='utf-8')
                foreign=args.copy();foreign[foreign.index('-B')+1]=str(root/('foreign-'+key))
                rejected=subprocess.run(foreign,capture_output=True,text=True)
                diagnostic=' '.join((rejected.stdout+rejected.stderr).split())
                if rejected.returncode==0 or 'compiler target is' not in diagnostic:raise RuntimeError('SDK accepted foreign '+key+' without an explicit architecture hint')
        finally:configs[0].write_text(original,encoding='utf-8')
        print('PASS: relocated SDK, independent project, Unicode/spaces, content-only refresh, explicit and automatic OS/architecture rejection'+(', Nova module' if a.nova else '')+(', Lua script' if a.lua else ''))
if __name__=='__main__':main()
