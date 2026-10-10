#pragma once
#include "core.h"

namespace rp {
struct ServerClock {
    struct Sample {double received=0,offset=0,rtt=0;bool valid=false;};
    Sample samples[16]{};
    int pending=0,sequence=0,write=0;
    double sent=0,offset=0,last=0,bestRtt=0;
    bool ready=false;
    void reset() {for(auto &s:samples)s=Sample{};pending=sequence=write=0;sent=offset=last=bestRtt=0;ready=false;}
    int request(double time) {
        if(!isfinite(time))return 0;
        sequence=sequence==1000000?1:sequence+1;
        pending=sequence;sent=time;return pending;
    }
    bool receive(int nonce,double server,double time) {
        if(!pending||nonce!=pending||!isfinite(server)||!isfinite(time)||server<0||time<sent||time-sent>1)return false;
        pending=0;
        samples[write++%16]={time,server-(sent+time)*.5,time-sent,true};
        const Sample *best=nullptr;
        for(const auto &s:samples)if(s.valid&&time-s.received<=15&&(!best||s.rtt<best->rtt))best=&s;
        if(!best)return false;
        offset=best->offset;bestRtt=best->rtt;last=time;ready=true;return true;
    }
    bool fresh(double time) const {return ready&&time>=last&&time-last<12;}
    double serverNow(double time) const {return time+offset;}
    bool sampleAge(double server,double time,float &age) const {
        if(!fresh(time)||!isfinite(server)||server<0)return false;
        double delta=serverNow(time)-server;
        if(delta<-.05||delta>.5)return false;
        age=clamp(float(delta),0,.5f);return true;
    }
};
}
