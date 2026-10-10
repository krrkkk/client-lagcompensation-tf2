#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parents[1] if ROOT.parent.name == 'tf2server' else ROOT


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_zip(path, files):
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            if Path(name).is_absolute() or '..' in Path(name).parts:
                raise ValueError('unsafe archive path')
            archive.writestr(name, data)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('archive integrity check failed')
        if set(archive.namelist()) != set(files):
            raise RuntimeError('archive file list changed')
        for name, data in files.items():
            if digest(archive.read(name)) != digest(data):
                raise RuntimeError('archive data changed: '+name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=PROJECT/'.dserv/tmp/projectile-prediction/build')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    manifest = json.loads((build/'manifest.json').read_text())
    version = manifest['version']
    binary = {}
    for name in ['projectile_prediction.dll', 'projectile_lab.smx']:
        data = (build/name).read_bytes()
        match = [expected for path, expected in manifest['binaries'].items() if Path(path).name == name]
        if match != [digest(data)]:
            raise RuntimeError('binary does not match build manifest: '+name)
        binary[name] = data
    for name, expected in manifest['sources'].items():
        path = PROJECT/name
        if not path.is_file() or digest(path.read_bytes()) != expected:
            raise RuntimeError('source changed after build: '+name)
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError('release output already exists; do not overwrite published releases')
    output.mkdir(parents=True)
    docs = {p.name: p.read_bytes() for pattern in ['README*.md', 'CHANGELOG*.md', 'THIRD_PARTY.md'] for p in ROOT.glob(pattern)}
    client = dict(docs)
    client['tf/addons/projectile_prediction.dll'] = binary['projectile_prediction.dll']
    client['tf/cfg/projectile_lab.cfg'] = (ROOT/'config/projectile_lab.cfg').read_bytes()
    server = dict(docs)
    server['tf/addons/sourcemod/plugins/projectile_lab.smx'] = binary['projectile_lab.smx']
    server['tf/cfg/sourcemod/projectile_lab.cfg'] = b'sm_rp_max_advance 0.100\nsm_rp_advance 1\n'
    public = dict(docs)
    public['.gitignore'] = (ROOT/'.gitignore').read_bytes()
    for folder in ['client', 'server', 'tests', 'config']:
        for p in sorted((ROOT/folder).rglob('*')):
            if p.is_file() and p.suffix in {'.h', '.cpp', '.sp', '.inc', '.cfg'}:
                public[p.relative_to(ROOT).as_posix()] = p.read_bytes()
    for name in ['build.py', 'msvc.py', 'weapons.py', 'schema.py', 'package.py']:
        public['tools/'+name] = (ROOT/'tools'/name).read_bytes()
    portable = {key: manifest[key] for key in ['version', 'server_protocol', 'sdk_reference', 'compiler', 'client_abi', 'rtti', 'crt']}
    portable['sources'] = {name: digest(data) for name, data in sorted(public.items())}
    portable['binaries'] = {name: digest(data) for name, data in binary.items()}
    portable['source_sdk_headers'] = manifest['source_sdk_headers']
    public['build-manifest.json'] = (json.dumps(portable, indent=2)+'\n').encode()
    client_name = f'projectile_lab_{version}_Windows_x64.zip'
    server_name = f'projectile_lab_{version}_Server.zip'
    source_name = f'projectile_lab_{version}_source.zip'
    write_zip(output/client_name, client)
    write_zip(output/server_name, server)
    write_zip(output/source_name, public)
    hashes = ''.join(f'{digest((output/name).read_bytes())}  {name}\n' for name in [client_name, server_name, source_name])
    (output/'SHA256SUMS').write_text(hashes)
    bundle = {'repository/'+name: data for name, data in public.items()}
    for name in [client_name, server_name, 'SHA256SUMS']:
        bundle['release-assets/'+name] = (output/name).read_bytes()
    bundle['release-assets/'+source_name] = (output/source_name).read_bytes()
    bundle['publish.txt'] = (
        'github upload\n\n'
        '1. upload the contents of repository/ to your repository.\n'
        '2. create release v'+version+' and mark it as a pre-release: this is an experimental TF2 lab.\n'
        '3. attach the files from release-assets/. use CHANGELOG.md for the release notes.\n\n'
        '1. содержимое repository/ залей в репозиторий.\n'
        '2. создай релиз v'+version+' с пометкой pre-release: это тестовая сборка.\n'
        '3. прикрепи файлы из release-assets/. текст обновления — CHANGELOG.ru.md.\n'
    ).encode()
    bundle_name = f'projectile_lab_{version}_GitHub.zip'
    write_zip(output/bundle_name, bundle)
    (output/'SHA256SUMS').write_text(hashes+f'{digest((output/bundle_name).read_bytes())}  {bundle_name}\n')
    (output/'build-manifest.json').write_bytes(public['build-manifest.json'])
    print(json.dumps({'version': version, 'output': str(output), 'public_source_files': len(public), 'client_sha256': digest(binary['projectile_prediction.dll']), 'server_sha256': digest(binary['projectile_lab.smx'])}, indent=2))


if __name__ == '__main__':
    main()
