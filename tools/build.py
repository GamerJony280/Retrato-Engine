#!/usr/bin/env python3
"""Developer convenience only. No downloads, shell assembly, or runtime dependency."""
import argparse, json, os, platform, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def call(args):
    print('+',subprocess.list2cmdline([str(a) for a in args]),flush=True)
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)
def binary(build,name):
    suffix='.exe' if os.name=='nt' else ''
    for directory in [build/'nova/Release',build/'nova',build/'templates/game/Release',build/'templates/game',build/'Release',build]:
        p=directory/(name+suffix)
        if p.is_file():return p
    raise RuntimeError('Built executable not found: '+name)
def host_nova(cmake,jobs):
    build=ROOT/'build/host-tools'
    call([cmake,'--preset','cpu','-B',build,'-DEMBER_TOOLS=OFF','-DEMBER_EXAMPLES=OFF','-DEMBER_TESTS=OFF','-DEMBER_VFX=OFF','-DEMBER_NOVA=ON'])
    call([cmake,'--build',build,'--config','Release','--target','nova','--parallel',jobs])
    return binary(build,'nova')
def main():
    machine=platform.machine().lower()
    default=(('windows-arm64' if machine in ['arm64','aarch64'] else 'windows-x86' if machine in ['x86','i386','i686'] else 'windows-x64') if os.name=='nt' else 'linux-arm64-native' if machine in ['aarch64','arm64'] else 'linux-armv7-native' if machine.startswith('armv7') else 'linux-x86' if machine in ['i386','i686'] else 'linux-x64')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('target',nargs='?',default=default,choices=['host-tools','cpu','runtime','linux-x64','linux-x64-compat','linux-arm64-native','linux-arm64','linux-armv7','linux-armv7-native','linux-x86','windows-x64','windows-x64-compat','windows-x86','windows-arm64','windows-x64-mingw','android-arm64','android-x64','android-armv7','android-x86'])
    p.add_argument('--cmake',default=os.environ.get('CMAKE','cmake'));p.add_argument('--jobs',type=int,default=2)
    p.add_argument('--tests',action='store_true');p.add_argument('--starter',action='store_true');p.add_argument('--run',action='store_true');p.add_argument('--sdk',type=Path)
    p.add_argument('--define',action='append',default=[],metavar='KEY=VALUE',help='CMake option, repeatable; each value stays one argument')
    a=p.parse_args()
    if a.jobs<1:p.error('--jobs must be positive')
    if a.target=='host-tools':print(host_nova(a.cmake,a.jobs));return
    build=ROOT/'build'/a.target
    cross=a.target in ['linux-arm64','linux-armv7','linux-x86','windows-x64-mingw'] or a.target.startswith('android-') or (a.target=='windows-arm64' and platform.machine().lower() not in ['arm64','aarch64'])
    args=[a.cmake,'--preset',a.target,'-B',build,'-DEMBER_TESTS='+('ON' if a.tests else 'OFF')]
    if a.starter or a.run:
        if a.target.startswith('android-'):p.error('Use platform/android to package an APK, not --starter/--run')
        args+=['-DEMBER_APP=ON','-DEMBER_STARTER=ON']
    if cross and not a.target.startswith('android-'):args+=['-DNOVA_HOST_COMPILER='+str(host_nova(a.cmake,a.jobs))]
    args+=['-D'+d for d in a.define]
    call(args);call([a.cmake,'--build',build,'--config','Release','--parallel',a.jobs])
    if a.tests:
        if cross:print('Cross build complete. Tests were compiled; execute them on the target or configure an emulator.')
        else:
            ctest=Path(a.cmake).with_name('ctest'+('.exe' if os.name=='nt' else '')) if Path(a.cmake).parent!=Path('.') else 'ctest'
            call([ctest,'--test-dir',build,'-C','Release','--output-on-failure'])
    if a.sdk:
        prefix=a.sdk.resolve();manifest=prefix/'share/retrato/retrato-target.json';new=json.loads((build/'retrato-target.json').read_text())
        if manifest.exists():
            old=json.loads(manifest.read_text())
            if (old['os'],old['arch'])!=(new['os'],new['arch']):raise RuntimeError('SDK destination belongs to another OS/architecture; choose a separate directory')
        elif prefix.exists() and any(prefix.iterdir()):raise RuntimeError('SDK destination is nonempty and has no Retrato manifest; choose an empty directory')
        call([a.cmake,'--install',build,'--config','Release','--prefix',prefix])
    if a.run:
        if cross:raise RuntimeError('Cannot run a target binary on this computer')
        call([binary(build,'retrato_game')])
if __name__=='__main__':
    try:main()
    except (subprocess.CalledProcessError,RuntimeError,OSError) as error:print(error,file=sys.stderr);sys.exit(1)
