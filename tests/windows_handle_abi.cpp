#include "../client/abi.h"
#include <assert.h>
#include <stdio.h>
extern "C" unsigned long long receive_handle();
extern "C" int receive_index();
struct Handle {
    unsigned value;
    explicit Handle(unsigned v):value(v){}
    Handle(const Handle &other):value(other.value){}
};
int main() {
    void *vtable[5]={nullptr,nullptr,nullptr,reinterpret_cast<void*>(&receive_index),reinterpret_cast<void*>(&receive_handle)};
    struct Object{void **vtable;} object{vtable};
    unsigned bits=0x002a0034U;
    auto old=rp::call<unsigned long long,unsigned>(&object,4,bits);
    auto expected=rp::call<unsigned long long,Handle>(&object,4,Handle(bits));
    assert(old==bits&&expected==(0x100000000ULL|bits));
    assert((rp::call<int,int>(&object,3,rp::networkIndex(bits))==0x34));
    int lookups=0;
    auto entity=rp::resolveNetworkHandle(bits,[&](int index)->void*{
        lookups++;assert((rp::call<int,int>(&object,3,index)==0x34));return &object;
    },[&](void *found){assert(found==&object);return bits;});
    assert(entity==&object&&lookups==1);
    printf("Microsoft x64: old call passes handle bits as address 0x%llx; expected object address\n",old);
    puts("fixed lookup: integer entity index and full serial comparison passed");
}
