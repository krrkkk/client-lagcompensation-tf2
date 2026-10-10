from pathlib import Path
import re
import subprocess


class MicrosoftBuild:
    def __init__(self, windows_sdk, source_sdk, output, compiler='clang-19'):
        self.output=Path(output).resolve()
        self.output.mkdir(parents=True,exist_ok=True)
        self.windows=Path(windows_sdk).resolve()
        self.source=Path(source_sdk).resolve()/'src'
        self.compiler=compiler
        version=subprocess.check_output([compiler,'--version'],text=True)
        match=re.search(r'clang version (\d+)',version)
        if not match or int(match[1])<19:raise RuntimeError('clang 19 or newer is required')
        self.version=version.splitlines()[0]
        original=(self.source/'public/mathlib/ssemath.h').read_text()
        adjusted,count=re.subn(r'#ifndef POSIX(?=\n\s*return a\.m128_(?:f32|u32)\[)', '#if !defined(POSIX) && !defined(__clang__)',original)
        if count not in (0,4):raise RuntimeError('unexpected Source SDK SIMD header; use the pinned SDK')
        overlay=self.output/'sdk-compat'
        (overlay/'mathlib').mkdir(parents=True,exist_ok=True)
        (overlay/'mathlib/ssemath.h').write_text(adjusted)
        bitbuf=(self.source/'public/tier1/bitbuf.h').read_text()
        signature='void bf_write::WriteUBitLong( unsigned int curData, int numbits, bool bCheckRange ) RESTRICT'
        if signature not in bitbuf:raise RuntimeError('unexpected Source SDK bitbuf header; use the pinned SDK')
        (overlay/'tier1').mkdir(exist_ok=True)
        (overlay/'tier1/bitbuf.h').write_text(bitbuf.replace(signature,signature.removesuffix(' RESTRICT')))
        includes=[overlay,self.windows/'crt/include']+[self.windows/'sdk/include'/p for p in ['ucrt','shared','um','winrt']]+[self.source/'public',self.source/'public/tier0',self.source/'public/tier1']
        libraries=[self.windows/'crt/lib/x86_64',self.windows/'sdk/lib/um/x86_64',self.windows/'sdk/lib/ucrt/x86_64']
        for path in includes+libraries:
            if not path.is_dir():raise RuntimeError(f'missing SDK directory: {path}')
        self.flags=[compiler,'--driver-mode=cl','--target=x86_64-pc-windows-msvc','-fuse-ld=lld','/std:c++17','/O2','/W4','/WX','/Z7','/MT','/GR','/EHsc','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/DCOMPILER_MSVC','/DCOMPILER_MSVC64','/DWIN32']+['/imsvc'+str(p) for p in includes]
        self.link=['/link','/Brepro','/debug:full','/ignore:4099','kernel32.lib','shell32.lib']+['/libpath:'+str(p) for p in libraries]

    def binary(self,name,sources,defines=(),dll=False):
        target=self.output/(name+('.dll' if dll else '.exe'))
        obj=self.output/(name+'-objects');obj.mkdir(exist_ok=True)
        command=self.flags+(['/LD'] if dll else [])+['/D'+d for d in defines]
        command += [str(Path(p).resolve()) for p in sources]+['/Fo'+str(obj)+'/', '/Fe'+str(target)]
        command += self.link+['/pdb:'+str(self.output/(name+'.pdb')),'/pdbaltpath:'+name+'.pdb']
        subprocess.run(command,check=True)
        return target
