#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "../client/abi.h"
#include "../client/win_api.h"
int main(int argc,char **argv) {
    if(argc<2)return 2;
    HMODULE module=LoadLibraryA(argv[1]);if(!module){printf("load failed: %lu\n",GetLastError());return 3;}
    auto factory=rp::exportedFunction<rp::Factory>(module,"CreateInterface");
    if(!factory)return 4;
    int code=-1;if(factory("unsupported",&code)||code!=1)return 5;
    auto plugin=static_cast<rp::PluginCallbacks*>(factory("ISERVERPLUGINCALLBACKS003",&code));
    if(!plugin||code!=0)return 6;
    printf("%s\n",plugin->GetPluginDescription());
    if(plugin->Load(nullptr,nullptr)){puts("unexpected activation without TF2");return 7;}
    if(!FreeLibrary(module))return 8;
    puts("windows DLL: load, export, interface and fail-closed checks passed");return 0;
}
