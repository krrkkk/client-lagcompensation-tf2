#pragma once
#include "core.h"

namespace rp {
struct Explosion {
    Vec position{},normal{};
    int weapon=22,definition=18,sound=11,particle=65535,entity=0;
};
struct Effect {
    int command=0;
    double touched=0;
    bool pending=false,played=false,authoritative=false;
    Explosion explosion{};
};
struct Effects {
    Effect entries[256]{};
    int epoch=0;
    void reset(int value=0) {for(auto &e:entries)e={};epoch=value;}
    Effect *find(int cmd) {for(auto &e:entries)if(e.command==cmd&&cmd>0)return &e;return nullptr;}
    bool queue(int e,int cmd,const Explosion &data,double time,bool authoritative=false) {
        if(!epoch||e!=epoch||cmd<=0||!data.position.valid()||!data.normal.valid()||!isfinite(time))return false;
        Effect *entry=find(cmd);
        if(entry&&entry->played)return false;
        if(!entry)for(auto &candidate:entries)if(!candidate.command||time-candidate.touched>30){entry=&candidate;*entry={};entry->command=cmd;break;}
        if(!entry)return false;
        if(!entry->authoritative||authoritative)entry->explosion=data;
        entry->authoritative=entry->authoritative||authoritative;
        entry->touched=time;entry->pending=true;
        return true;
    }
    template<class Play> int flush(bool inPrediction,double time,Play play) {
        if(inPrediction)return 0;
        int count=0;
        for(auto &e:entries)if(e.pending&&!e.played){
            if(time-e.touched>2){e.pending=false;continue;}
            if(play(e.explosion)){e.played=true;e.pending=false;e.touched=time;count++;}
        }
        return count;
    }
};
}
