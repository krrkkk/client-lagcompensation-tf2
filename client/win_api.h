#pragma once
#include <windows.h>
#include <string.h>
namespace rp {
template<class Function> Function exportedFunction(HMODULE module,const char *name) {
    FARPROC symbol=module?GetProcAddress(module,name):nullptr;
    Function result=nullptr;
    static_assert(sizeof(result)==sizeof(symbol));
    memcpy(&result,&symbol,sizeof(result));
    return result;
}
}
