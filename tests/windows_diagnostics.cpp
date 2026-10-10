#include "../client/diagnostics.h"
#include <assert.h>
#include <string.h>
static int reached=0;
static LONG WINAPI downstream(EXCEPTION_POINTERS *) {reached++;return EXCEPTION_CONTINUE_EXECUTION;}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    char path[MAX_PATH];snprintf(path,sizeof(path),"%s/logs",argv[1]);CreateDirectoryA(path,nullptr);
    rp::diagnostics.open(argv[1],rp::recordException);
    assert(rp::diagnostics.handler&&rp::diagnostics.file!=INVALID_HANDLE_VALUE);
    void *next=AddVectoredExceptionHandler(0,downstream);assert(next);
    rp::diagnostics.command=77;
    {
        rp::StageScope phase(rp::Stage::WeaponLookup);
        ULONG_PTR parameters[2]={0,0x2a0034};
        RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,parameters);
    }
    assert(reached==1&&rp::diagnostics.stage==LONG(rp::Stage::Idle));
    rp::diagnostics.close();RemoveVectoredExceptionHandler(next);
    snprintf(path,sizeof(path),"%s/logs/projectile_prediction_exceptions.log",argv[1]);
    FILE *input=fopen(path,"rb");assert(input);char data[1024]{};auto count=fread(data,1,sizeof(data)-1,input);fclose(input);
    assert(count>0&&strstr(data,"stage=weapon-lookup")&&strstr(data,"command=77")&&strstr(data,"access=0x2a0034"));
    puts("Windows exception log: command, stage, module and access address recorded; exception chain preserved");
}
