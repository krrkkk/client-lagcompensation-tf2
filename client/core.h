#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace rp {
struct Vec {
    float x,y,z;
    Vec operator+(Vec b) const {return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b) const {return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(float s) const {return {x*s,y*s,z*s};}
    float dot(Vec b) const {return x*b.x+y*b.y+z*b.z;}
    bool valid() const {return isfinite(x)&&isfinite(y)&&isfinite(z);}
    float length() const {return sqrtf(dot(*this));}
    Vec unit() const {float n=length();return n>0.0001f?*this*(1.f/n):Vec{};}
};
struct Matrix {float m[3][4];};
inline Matrix projectileAlignment() {return {{{0,-1,0,0},{1,0,0,0},{0,0,1,0}}};}
inline Matrix multiply(const Matrix &a,const Matrix &b) {
    Matrix result{};
    for(int r=0;r<3;r++){
        for(int c=0;c<3;c++)for(int k=0;k<3;k++)result.m[r][c]+=a.m[r][k]*b.m[k][c];
        result.m[r][3]=a.m[r][3];
        for(int k=0;k<3;k++)result.m[r][3]+=a.m[r][k]*b.m[k][3];
    }
    return result;
}
inline Matrix inverseRigid(const Matrix &a) {
    Matrix result{};
    for(int r=0;r<3;r++)for(int c=0;c<3;c++)result.m[r][c]=a.m[c][r];
    for(int r=0;r<3;r++)for(int c=0;c<3;c++)result.m[r][3]-=result.m[r][c]*a.m[c][3];
    return result;
}
inline bool validBounds(Vec lo,Vec hi) {
    return lo.valid()&&hi.valid()&&hi.x-lo.x>.01f&&hi.y-lo.y>.01f&&hi.z-lo.z>.01f&&
        (hi-lo).length()<4096;
}
inline void transformBounds(const Matrix &m,Vec lo,Vec hi,Vec &outLo,Vec &outHi) {
    float low[3]={lo.x,lo.y,lo.z},high[3]={hi.x,hi.y,hi.z},a[3],b[3];
    for(int i=0;i<3;i++){
        a[i]=b[i]=m.m[i][3];
        for(int j=0;j<3;j++){
            float x=m.m[i][j]*low[j],y=m.m[i][j]*high[j];
            a[i]+=x<y?x:y;b[i]+=x>y?x:y;
        }
    }
    outLo={a[0],a[1],a[2]};outHi={b[0],b[1],b[2]};
}
inline bool rocketCollisionGroup(int group) {
    return group!=1&&group!=2&&group!=3&&group!=8&&group!=11&&group!=13&&group!=20&&group!=25&&group!=27;
}
inline uint32_t clientReference(uint32_t serverReference) {
    return (serverReference&0xffffU)|(((serverReference>>16)&1023U)<<16);
}
inline float clamp(float x,float a,float b) {return x<a?a:(x>b?b:x);}
inline int networkIndex(uint32_t reference) {
    if(reference==0xffffffffU)return -1;
    unsigned index=reference&0xffffU;
    return index>0&&index<2048?int(index):-1;
}
template<class Lookup,class Reference> void *resolveNetworkHandle(uint32_t handle,Lookup lookup,Reference reference) {
    int index=networkIndex(handle);if(index<0)return nullptr;
    void *entity=lookup(index);
    return entity&&reference(entity)==handle?entity:nullptr;
}
inline void basis(Vec a,Vec &f,Vec &r,Vec &u) {
    float p=a.x*0.01745329252f,y=a.y*0.01745329252f;
    float sp=sinf(p),cp=cosf(p),sy=sinf(y),cy=cosf(y);
    f={cp*cy,cp*sy,-sp};r={sy,-cy,0};u={sp*cy,sp*sy,cp};
}
inline Vec angles(Vec d) {return {-atan2f(d.z,sqrtf(d.x*d.x+d.y*d.y))*57.295779513f,atan2f(d.y,d.x)*57.295779513f,0};}

inline float correctionWeight(float progress) {float t=clamp(progress,0.f,1.f);return 1.f-t*t*(3.f-2.f*t);}

struct Shot {
    bool active,confirmed,stopped;
    int command;
    int item;
    uint32_t reference;
    double born,anchorTime,correctedAt;
    Vec origin,velocity,correction,position,acceleration;
    Vec rotation,angularVelocity;
    Vec flightAcceleration;
    double rotationAt,expires;
    int sampleTick;
    bool landed;
    bool terminal;
    Vec at(double now) const {float dt=float(now-anchorTime);return origin+velocity*dt+acceleration*(dt*dt*.5f)+correction*correctionWeight(float(now-correctedAt)/0.08f);}
};

struct Pool {
    Shot shots[32]{};
    int seen[256]{};
    int epoch=0,write=0;
    void reset(int value=0) {memset(shots,0,sizeof(shots));memset(seen,0,sizeof(seen));epoch=value;write=0;}
    Shot *find(int command) {for(auto &s:shots)if(s.active&&s.command==command)return &s;return nullptr;}
    Shot *start(int command,Vec pos,Vec velocity,double now) {
        if(!epoch||command<=0||!pos.valid()||!velocity.valid())return nullptr;
        for(int value:seen)if(value==command)return nullptr;
        seen[write++%256]=command;
        for(auto &s:shots)if(!s.active){s={};s.active=true;s.command=command;s.born=s.anchorTime=now;s.expires=now+1;s.origin=s.position=pos;s.velocity=velocity;return &s;}
        return nullptr;
    }
    bool acknowledge(int e,int cmd,uint32_t ref,Vec pos,Vec vel,double now) {
        if(e!=epoch||networkIndex(clientReference(ref))<0||!pos.valid()||!vel.valid()||vel.length()<1095||vel.length()>1105)return false;
        Shot *s=find(cmd);if(!s||s->confirmed||now<s->born||now-s->born>1.0)return false;
        Vec before=s->at(now);
        s->origin=pos+vel*float((now-s->born)*0.5);
        s->velocity=vel;s->anchorTime=now;s->correctedAt=now;
        s->correction=before-s->origin;s->confirmed=true;s->reference=clientReference(ref);s->expires=now+12;
        return true;
    }
    bool acknowledgeTimed(int e,int cmd,uint32_t ref,Vec pos,Vec vel,double now,float age) {
        if(e!=epoch||networkIndex(clientReference(ref))<0||!pos.valid()||!vel.valid()||vel.length()>15000||!isfinite(age)||age<0||age>.5f)return false;
        Shot *s=find(cmd);if(!s||s->confirmed||now<s->born||now-s->born>1.0)return false;
        Vec before=s->at(now);
        s->origin=pos+vel*age+s->acceleration*(age*age*.5f);
        s->velocity=vel+s->acceleration*age;s->anchorTime=now;s->correctedAt=now;
        s->correction=before-s->origin;s->confirmed=true;s->reference=clientReference(ref);s->expires=now+12;return true;
    }
    bool state(int e,int cmd,uint32_t ref,Vec pos,Vec vel,Vec rotation,int tick,double now,float age) {
        Shot *s=find(cmd);
        if(e!=epoch||!s||s->terminal||!s->confirmed||s->reference!=clientReference(ref)||tick<=s->sampleTick||!pos.valid()||!vel.valid()||!rotation.valid()||vel.length()>15000||!isfinite(age)||age<0||age>.5f)return false;
        s->acceleration=vel.length()<1?Vec{}:s->flightAcceleration;
        Vec before=s->position;s->origin=pos+vel*age+s->acceleration*(age*age*.5f);
        s->velocity=vel+s->acceleration*age;s->anchorTime=s->correctedAt=now;s->correction=before-s->origin;
        float dt=float(now-age-s->rotationAt);
        if(s->sampleTick&&dt>.005f&&dt<.5f){Vec delta=rotation-s->rotation;delta.x=remainderf(delta.x,360);delta.y=remainderf(delta.y,360);delta.z=remainderf(delta.z,360);s->angularVelocity=delta*(1.f/dt);}
        s->rotation=rotation;s->rotationAt=now-age;s->sampleTick=tick;s->expires=now+12;s->landed=vel.length()<1;
        if(s->landed){s->acceleration={};s->velocity={};}
        s->stopped=false;return true;
    }
    bool acknowledgeFlight(int e,int cmd,uint32_t ref,Vec pos,Vec vel,float advance,double now) {
        Shot *s=find(cmd);
        if(e!=epoch||!s||s->confirmed||networkIndex(clientReference(ref))<0||!pos.valid()||!vel.valid()||vel.length()>15000||!isfinite(advance)||advance<0||advance>.151f||now<s->born||now-s->born>1)return false;
        Vec before=s->at(now);
        s->origin=pos-vel*advance+vel*float(now-s->born);
        s->velocity=vel;s->anchorTime=s->correctedAt=now;s->correction=before-s->origin;
        s->reference=clientReference(ref);s->confirmed=true;
        s->expires=now+12;
        return true;
    }
    void end(int e,int cmd) {
        if(e!=epoch)return;
        if(Shot *s=find(cmd))s->active=false;
        bool found=false;for(int x:seen)if(x==cmd)found=true;
        if(cmd>0&&!found)seen[write++%256]=cmd;
    }
    void expire(double now) {for(auto &s:shots)if(s.active&&(now<s.born||now>s.expires))s.active=false;}
};

struct Binding {
    int command=0;
    uint32_t reference=0xffffffffU;
    double expires=0;
    bool seen=false,ended=false,replacementDrawn=false;
    double boundAt=0;
};
struct Bindings {
    Binding entries[64]{};
    void reset() {for(auto &b:entries)b={};}
    Binding *find(int command) {for(auto &b:entries)if(b.command==command&&command>0)return &b;return nullptr;}
    void add(int command,uint32_t reference,double time) {
        if(command<=0||networkIndex(reference)<0)return;
        Binding *b=find(command);
        if(!b)for(auto &entry:entries)if(!entry.command||entry.expires<time){b=&entry;break;}
        if(b){*b={};b->command=command;b->reference=reference;b->expires=time+12;}
    }
    void finish(int command,double time) {if(auto b=find(command)){b->ended=true;b->expires=time+.5;}}
};
}
