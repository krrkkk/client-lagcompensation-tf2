#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

namespace rp {
enum class Stage:LONG {Idle,Prediction,WeaponLookup,NativeCommand,ShotPosition,AimTrace,MuzzleTrace,StoreShot,MoveSetup,MoveTrace,ModelLoad,ModelInstance,ModelRegister,ModelUpdate,ModelDraw,Explosion};
struct Diagnostics {
    HANDLE file=INVALID_HANDLE_VALUE;
    void *handler=nullptr;
    volatile LONG stage=LONG(Stage::Idle),command=0,thread=0,writing=0,records=0;
    static const char *label(LONG stage) {
        static const char *names[]={"idle","prediction","weapon-lookup","native-command","shot-position","aim-trace","muzzle-trace","store-shot","move-setup","move-trace","model-load","model-instance","model-register","model-update","model-draw","explosion"};
        return stage>=0&&stage<LONG(sizeof(names)/sizeof(*names))?names[stage]:"unknown";
    }
    void set(Stage value) {InterlockedExchange(&thread,LONG(GetCurrentThreadId()));InterlockedExchange(&stage,LONG(value));}
    void close() {
        if(handler){RemoveVectoredExceptionHandler(handler);handler=nullptr;}
        if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
    }
    void open(const char *directory,PVECTORED_EXCEPTION_HANDLER callback) {
        InterlockedExchange(&records,0);
        char path[MAX_PATH];snprintf(path,sizeof(path),"%s/logs/projectile_prediction_exceptions.log",directory);
        file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE)handler=AddVectoredExceptionHandler(0,callback);
    }
    LONG exception(EXCEPTION_POINTERS *info) {
        if(!info||!info->ExceptionRecord||file==INVALID_HANDLE_VALUE)return EXCEPTION_CONTINUE_SEARCH;
        auto record=info->ExceptionRecord;
        if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION&&record->ExceptionCode!=EXCEPTION_ILLEGAL_INSTRUCTION&&record->ExceptionCode!=EXCEPTION_INT_DIVIDE_BY_ZERO)return EXCEPTION_CONTINUE_SEARCH;
        if(InterlockedIncrement(&records)>16)return EXCEPTION_CONTINUE_SEARCH;
        if(InterlockedCompareExchange(&writing,1,0)!=0)return EXCEPTION_CONTINUE_SEARCH;
        HMODULE module=nullptr;char path[MAX_PATH]="unknown";
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<const char*>(record->ExceptionAddress),&module))GetModuleFileNameA(module,path,sizeof(path));
        const char *name=path;for(const char *p=path;*p;p++)if(*p=='\\'||*p=='/')name=p+1;
        auto address=reinterpret_cast<uintptr_t>(record->ExceptionAddress);
        uintptr_t target=record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&record->NumberParameters>=2?record->ExceptionInformation[1]:0;
        char output[768];int size=snprintf(output,sizeof(output),"exception=0x%08lx module=%s offset=0x%llx access=0x%llx stage=%s command=%ld thread=%lu stage_thread=%ld (first chance; no recovery attempted)\r\n",record->ExceptionCode,name,static_cast<unsigned long long>(address-reinterpret_cast<uintptr_t>(module)),static_cast<unsigned long long>(target),label(stage),command,GetCurrentThreadId(),thread);
        if(size>0){DWORD written;WriteFile(file,output,DWORD(size<int(sizeof(output))?size:int(sizeof(output))-1),&written,nullptr);FlushFileBuffers(file);}
        InterlockedExchange(&writing,0);return EXCEPTION_CONTINUE_SEARCH;
    }
};
inline Diagnostics diagnostics;
inline LONG WINAPI recordException(EXCEPTION_POINTERS *info) {return diagnostics.exception(info);}
struct StageScope {
    LONG previous;
    explicit StageScope(Stage value):previous(diagnostics.stage) {diagnostics.set(value);}
    ~StageScope() {diagnostics.set(Stage(previous));}
};
}
