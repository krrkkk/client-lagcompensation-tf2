#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <iclientrenderable.h>
#include "../client/win_api.h"

class CEconItemView:public IClientRenderable {};
static DWORD exceptionCode=0;
static uintptr_t accessAddress=0;
static int capture(EXCEPTION_POINTERS *info) {
    exceptionCode=info->ExceptionRecord->ExceptionCode;
    if(exceptionCode==EXCEPTION_ACCESS_VIOLATION&&info->ExceptionRecord->NumberParameters>=2)accessAddress=info->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool materialCast(IClientRenderable *renderable) {
    __try {
        if(dynamic_cast<CEconItemView*>(renderable)!=nullptr)return false;
        return dynamic_cast<void*>(renderable)!=nullptr;
    } __except(capture(GetExceptionInformation())) {return false;}
}
int main(int argc,char **argv) {
    if(argc!=3&&argc!=4)return 2;
    HMODULE module=LoadLibraryA(argv[1]);if(!module){printf("load failed %lu\n",GetLastError());return 3;}
    IClientRenderable *renderable=nullptr;
    bool direct=strcmp(argv[2],"current")!=0;
    bool legacy=direct&&(argc!=4||strcmp(argv[3],"fixed")!=0);
    if(direct)renderable=reinterpret_cast<IClientRenderable*>(reinterpret_cast<uintptr_t>(module)+strtoull(argv[2],nullptr,16));
    else {
        auto make=rp::exportedFunction<IClientRenderable*(*)()>(module,"rp_test_renderable");
        if(!make)return 4;
        renderable=make();
    }
    bool metadata=(*reinterpret_cast<void***>(renderable))[-1]!=nullptr;
    bool safe=materialCast(renderable);
    printf("%s material cast: metadata=%d safe=%d exception=0x%08lx access=0x%llx\n",legacy?"legacy":"current",metadata,safe,exceptionCode,static_cast<unsigned long long>(accessAddress));
    if(legacy)return !metadata&&!safe&&exceptionCode!=0?0:5;
    if(!safe||exceptionCode)return 6;
    auto unknown=renderable->GetIClientUnknown();
    if(!unknown||unknown->GetClientRenderable()!=renderable||unknown->GetBaseEntity()!=nullptr)return 7;
    Vector mins,maxs;renderable->GetRenderBounds(mins,maxs);
    if(mins.x!=-16||maxs.x!=16)return 8;
    matrix3x4_t bones[3];if(renderable->SetupBones(bones,3,0,0))return 9;
    puts("Source interface: RTTI, unknown/renderable link, bounds and unloaded-model guard passed");
    FreeLibrary(module);return 0;
}
