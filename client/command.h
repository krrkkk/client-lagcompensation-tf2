#pragma once
#include "abi.h"

namespace rp {
struct CommandArgs {
    int count,firstSize;
    char command[512],arguments[512];
    const char *argv[64];
};
struct ConsoleCommand {
    void *next=nullptr;
    bool registered=false;
    const char *name=nullptr,*help=nullptr;
    int flags=8,dll=-1;
    void (*callback)(const CommandArgs&)=nullptr;
    void *completion=nullptr;
    uint8_t callbackFlags=2;
    virtual void destroy() {}
    virtual bool isCommand() const {return true;}
    virtual bool isFlagSet(int f) const {return (flags&f)!=0;}
    virtual void addFlags(int f) {flags|=f;}
    virtual const char *getName() const {return name;}
    virtual const char *getHelp() const {return help;}
    virtual bool isRegistered() const {return registered;}
    virtual int getDll() const {return dll;}
    virtual void create(const char *n,const char *h,int f) {name=n;help=h;flags=f;}
    virtual void init() {}
    virtual int suggest(const char*,void*) {return 0;}
    virtual bool canComplete() {return false;}
    virtual void dispatch(const CommandArgs &args) {if(callback)callback(args);}
};
static_assert(sizeof(ConsoleCommand)==72);
static_assert(sizeof(CommandArgs)==1544);
}
