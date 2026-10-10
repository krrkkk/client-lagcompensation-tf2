#pragma once
#include "core.h"

namespace rp {
struct BlastRules {
    float tick=0.015f,groundScale=5.f,airScale=10.f,selfScale=.6f;
};
inline Vec selfImpulse(Vec blast,Vec feet,Vec mins,Vec maxs,bool onGround,const BlastRules &rules,bool inWater=false,float baseDamage=90.f,float pushScale=1.f,float radius=121.f) {
    Vec size=maxs-mins;
    if(!blast.valid()||!feet.valid()||!mins.valid()||!maxs.valid()||size.x<1||size.y<1||size.z<1)return {};
    Vec nearest={clamp(blast.x,feet.x+mins.x,feet.x+maxs.x),clamp(blast.y,feet.y+mins.y,feet.y+maxs.y),clamp(blast.z,feet.z+mins.z,feet.z+maxs.z)};
    if(radius<=0||(nearest-blast).length()>radius)return {};
    Vec center=feet+(mins+maxs)*.5f;
    float a=(feet-blast).length(),b=(center-blast).length();
    float distance=a<b?a:b;
    float damage=baseDamage*(1.f-.5f*clamp(distance/radius,0.f,1.f));
    if(!onGround&&!inWater)damage*=rules.selfScale;
    if(fabsf(size.z-62.f)<.1f)size.z=55.f;
    float scale=onGround?rules.groundScale:rules.airScale;
    float force=clamp(damage*(48.f*48.f*82.f/(size.x*size.y*size.z))*scale,0.f,1000.f);
    return (center-blast+Vec{0,0,10}).unit()*(force*pushScale);
}

struct Flight {
    int command=0,tick=0,life=0,impactCommand=0,serverCommand=0;
    int item=18;
    int weaponId=22;
    float damage=90,pushScale=1,radius=121;
    bool rocket=true;
    bool serverSpawn=false,serverEnded=false,selfConfirmed=false,retired=false,impulseCounted=false,staticHit=false,explosionReceived=false;
    double born=0;
    Vec launch{},velocity{},impact{},normal{},impulse{},confirmedImpulse{};
};
struct MotionJournal {
    Flight flights[64]{};
    int epoch=0,lastCommand=0,acknowledged=0;
    void reset(int value=0) {for(auto &f:flights)f={};epoch=value;lastCommand=acknowledged=0;}
    Flight *find(int cmd) {for(auto &f:flights)if(f.command==cmd&&cmd>0)return &f;return nullptr;}
    Flight *fire(int cmd,int tick,Vec launch,Vec velocity,double time,int life) {
        if(!epoch||cmd<=0||tick<0||!launch.valid()||!velocity.valid())return nullptr;
        if(auto f=find(cmd)){
            if(!f->serverSpawn){f->launch=launch;f->velocity=velocity;f->tick=tick;}
            return f;
        }
        for(auto &f:flights)if(!f.command||time-f.born>20){
            f={};f.command=cmd;f.tick=tick;f.launch=launch;f.velocity=velocity;f.born=time;f.life=life;return &f;
        }
        return nullptr;
    }
    void begin(int cmd) {
        if(cmd<=lastCommand)for(auto &f:flights)if(f.impactCommand>=cmd&&!f.retired){f.impactCommand=0;f.staticHit=false;f.impulse={};}
        lastCommand=cmd;
    }
    void acknowledge(int cmd) {
        if(cmd<0)return;
        acknowledged=cmd;
        for(auto &f:flights)if(f.serverCommand>0&&f.serverCommand<=cmd)f.retired=true;
    }
    void selfDamage(int cmd,int applied,Vec impulse) {
        if(auto f=find(cmd)){f->selfConfirmed=true;f->confirmedImpulse=impulse;f->serverCommand=applied;f->retired=applied>0&&applied<=acknowledged;}
    }
    void finish(int cmd,int applied,bool self) {
        if(auto f=find(cmd)){f->serverEnded=true;if(!self){f->serverCommand=applied;f->retired=applied>0&&applied<=acknowledged;}}
    }
};
}
