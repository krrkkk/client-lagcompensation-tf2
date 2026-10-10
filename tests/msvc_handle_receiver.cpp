struct Handle {
    unsigned value;
    explicit Handle(unsigned v):value(v){}
    Handle(const Handle &other):value(other.value){}
};
extern "C" __declspec(noinline) unsigned long long receive_handle(void*,Handle handle) {
    auto address=reinterpret_cast<unsigned long long>(&handle);
    if(address<0x100000000ULL)return address;
    return 0x100000000ULL|handle.value;
}
extern "C" __declspec(noinline) int receive_index(void*,int index) {return index;}
