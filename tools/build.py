#!/usr/bin/env python3
import hashlib
import json
import argparse
from pathlib import Path
import subprocess
from msvc import MicrosoftBuild

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parents[1] if ROOT.parent.name=='tf2server' else ROOT
OUT = PROJECT/'.dserv/tmp/projectile-prediction/build'
SDK = Path('/srv/tf2-duel/server/tf/addons/sourcemod/scripting')


def run(args):
    subprocess.run([str(x) for x in args], check=True, cwd=PROJECT)


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,default=SDK)
    parser.add_argument('--client-only',action='store_true')
    parser.add_argument('--windows-sdk',type=Path,default=Path.home()/'.cache/cargo-xwin/xwin')
    parser.add_argument('--source-sdk',type=Path,default=PROJECT/'.dserv/tmp/projectile-prediction/sdk')
    parser.add_argument('--compiler',default='clang-19')
    args=parser.parse_args()
    args.source_sdk=args.source_sdk.resolve()
    SDK=args.sdk
    OUT.mkdir(parents=True, exist_ok=True)
    msvc=MicrosoftBuild(args.windows_sdk,args.source_sdk,OUT,args.compiler)
    msvc.binary('projectile_prediction',[ROOT/'client/client.cpp'],dll=True)
    msvc.binary('projectile_prediction_test',[ROOT/'client/client.cpp'],defines=['RP_RENDER_TEST'],dll=True)
    msvc.binary('windows_loader',[ROOT/'tests/windows_loader.cpp'])
    msvc.binary('windows_handle_abi',[ROOT/'tests/windows_handle_abi.cpp',ROOT/'tests/msvc_handle_receiver.cpp'])
    msvc.binary('windows_diagnostics',[ROOT/'tests/windows_diagnostics.cpp'])
    msvc.binary('windows_render_rtti',[ROOT/'tests/windows_render_rtti.cpp'])
    msvc.binary('windows_model_pose',[ROOT/'tests/windows_model_pose.cpp'])
    run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', ROOT/'tests/core_test.cpp', '-o', OUT/'core_test'])
    run([OUT/'core_test'])
    run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', ROOT/'tests/native_physics.cpp', '-o', OUT/'native_physics'])
    binaries=[OUT/'projectile_prediction.dll']
    if not args.client_only:
        run([SDK/'spcomp64', '-i'+str(SDK/'include'), ROOT/'server/projectile_lab.sp', '-o'+str(OUT/'projectile_lab.smx')])
        run([SDK/'spcomp64', '-i'+str(SDK/'include'), 'RP_TEST=1', ROOT/'server/projectile_lab.sp', '-o'+str(OUT/'projectile_lab_test.smx')])
        binaries.extend([OUT/'projectile_lab.smx',OUT/'projectile_lab_test.smx'])
    hashes = lambda files: {str(p.relative_to(PROJECT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    manifest = {
        'version': '0.4.0', 'server_protocol': 4, 'compatible_server': '0.4.0; RP2 stock-only fallback', 'status': 'server timeline, command replay and rocket weapon profiles; full TF2 client validation pending',
        'sdk_reference': 'ValveSoftware/source-sdk-2013@be57e3d01ac7d7aa6a2cacbfef187c0b46616db0',
        'sources': hashes(sorted(p for folder in ['client','server','tools','tests','config'] for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts)+sorted(ROOT.glob('README*.md'))),
        'binaries': hashes(binaries),
        'compiler': msvc.version, 'client_abi': 'x86_64-pc-windows-msvc', 'rtti': True, 'crt': 'static release /MT',
        'source_sdk_headers': {str(p.relative_to(args.source_sdk)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.source_sdk/'src/public/iclientrenderable.h',args.source_sdk/'src/public/iclientunknown.h',args.source_sdk/'src/public/basehandle.h']},
    }
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(OUT/'manifest.json')
