#include "../client/core.h"
#include "../client/motion.h"
#include "../client/effects.h"
#include "../client/timeline.h"
#include "../client/weapon_profiles.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <initializer_list>
using namespace rp;
int main() {
    ServerClock clock;
    int nonce=clock.request(1000);
    assert(!clock.receive(nonce+1,100,1000.1));
    assert(clock.receive(nonce,100.05,1000.1));
    assert(!clock.receive(nonce,100.05,1000.1));
    float age=0;assert(clock.sampleAge(100.05,1000.1,age)&&fabs(age-.05f)<.0001f);
    nonce=clock.request(1001);assert(clock.receive(nonce,101.05,1001.5));
    assert(clock.sampleAge(101.4,1001.5,age)&&fabs(age-.1f)<.0001f);
    assert(!clock.sampleAge(200,1001.5,age));assert(!clock.fresh(1014));
    clock.reset();assert(!clock.fresh(1001.5));
    assert(weaponSpec(127)&&fabs(weaponSpec(127)->speed-1980)<.001);
    assert(weaponSpec(414)&&fabs(weaponSpec(414)->speed-1540)<.001);
    assert(weaponSpec(513)&&(weaponSpec(513)->flags&1));
    assert(!weaponSpec(-1)&&!weaponSpec(65535));
    Pool timed;timed.reset(100);auto direct=timed.start(1,{},{1980,0,0},1);
    assert(direct&&timed.acknowledgeTimed(100,1,55,{198,0,0},{1980,0,0},1.2,.1));
    assert(fabs(direct->at(1.4).x-792)<.01);
    Pool ballistic;ballistic.reset(101);auto pipe=ballistic.start(2,{0,0,100},{100,0,0},1);assert(pipe);
    pipe->acceleration=pipe->flightAcceleration={0,0,-800};
    assert(fabs(pipe->at(1.5).x-50)<.001&&fabs(pipe->at(1.5).z)<.001);
    assert(ballistic.acknowledgeTimed(101,2,56,{10,0,96},{100,0,-80},1.2,.1));
    assert(ballistic.state(101,2,56,{20,0,84},{100,0,-160},{10,20,30},20,1.3,.1));
    assert(!ballistic.state(101,2,56,{},{},{},19,1.3,.1));
    assert(!ballistic.state(101,2,57,{},{},{},21,1.3,.1));
    assert(ballistic.state(101,2,56,{20,0,10},{},{10,20,30},21,1.4,0));
    assert(pipe->landed&&pipe->acceleration.length()==0);
    assert(ballistic.state(101,2,56,{20,0,10},{100,0,100},{10,20,30},22,1.5,0));
    assert(!pipe->landed&&pipe->acceleration.z==-800);
    pipe->terminal=true;assert(!ballistic.state(101,2,56,{},{},{},23,1.6,0));
    for(float rtt:{.075f,.105f,.175f,.25f})for(float advance:{0.f,.045f,.1f}){
        Pool local;local.reset(9);Vec start{24,-12,60},speed{1980,0,0};
        auto shot=local.start(9,start,speed,100);assert(shot);
        assert(local.acknowledgeFlight(9,9,41,start+speed*advance,speed,advance,100+rtt));
        for(int frame=0;frame<40;frame++){
            double time=100+rtt+frame/144.;
            assert((shot->at(time)-(start+speed*float(time-100))).length()<.001f);
        }
        assert(!local.acknowledgeFlight(9,9,41,start,speed,0,100+rtt));
    }
    Pool corrected;corrected.reset(1);Vec speed{1100,0,0};
    auto shifted=corrected.start(50,{},speed,1);assert(shifted);
    Vec before=shifted->at(1.175);
    assert(corrected.acknowledgeFlight(1,50,42,{110,4,0},speed,.1f,1.175));
    assert((shifted->at(1.175)-before).length()<.001f);
    assert(fabs(shifted->at(1.3).y-4)<.001);
    assert(!validBounds({},{})&&!validBounds({1,1,1},{-1,-1,-1}));
    assert(!validBounds({NAN,0,0},{1,1,1}));
    assert(validBounds({-18,-4,-4},{18,4,4}));
    for(int group:{1,2,3,8,11,13,20,25,27})assert(!rocketCollisionGroup(group));
    for(int group:{0,4,5,6,7,9,21,26})assert(rocketCollisionGroup(group));
    assert(clientReference(uint32_t(-1189937042))==0x0113006eU);
    assert(clientReference(uint32_t(-1626275727))==0x03110071U);
    assert(clientReference(uint32_t(-1878589326))==0x00070072U);
    assert(clientReference(uint32_t(-247922577))==0x0139006fU);
    Matrix poseToBone={{{1,0,0,0},{0,0,1,0},{0,-1,0,0}}};
    Matrix bind=inverseRigid(poseToBone);
    assert(bind.m[1][2]==-1&&bind.m[2][1]==1);
    Matrix world={{{0,-1,0,30},{1,0,0,40},{0,0,1,50}}};
    Matrix skin=multiply(multiply(world,bind),poseToBone);
    for(int r=0;r<3;r++)for(int c=0;c<4;c++)assert(fabs(skin.m[r][c]-world.m[r][c])<.0001);
    assert(Explosion{}.particle==65535);
    uint32_t generation=0x002a0034U;int lookedUp=0;int token=0;
    auto lookup=[&](int index)->void*{lookedUp++;assert(index==0x34);return &token;};
    auto reference=[&](void *entity){assert(entity==&token);return generation;};
    assert(resolveNetworkHandle(generation,lookup,reference)==&token);
    assert(!resolveNetworkHandle(generation+0x10000,lookup,reference));
    assert(lookedUp==2);
    assert(!resolveNetworkHandle(0xffffffffU,lookup,reference));
    assert(!resolveNetworkHandle(0x2a0800U,lookup,reference));
    assert(!resolveNetworkHandle(0x2a0000U,lookup,reference));
    assert(lookedUp==2);
    assert(!resolveNetworkHandle(generation,[](int)->void*{return nullptr;},reference));
    Pool p;
    assert(!p.start(1,{},{1100,0,0},0));
    p.reset(7);
    auto a=p.start(1,{10,0,0},{1100,0,0},1);assert(a);
    assert(!p.start(1,{},{1100,0,0},1.01));
    assert(fabs(a->at(1.05).x-65)<0.001);
    assert(!p.acknowledge(6,1,55,{65,0,0},{1100,0,0},1.1));
    assert(p.acknowledge(7,1,0x80000037U,{65,0,0},{1100,0,0},1.1));
    assert(a->reference==55);
    assert(fabs(a->at(1.1).x-120)<0.001);
    assert(!p.acknowledge(7,1,55,{65,0,0},{1100,0,0},1.12));
    p.end(7,1);assert(!p.find(1));
    assert(!p.start(1,{},{1100,0,0},2));
    p.end(7,2);assert(!p.start(2,{},{1100,0,0},2));
    assert(!p.acknowledge(7,2,55,{},{1100,0,0},2));
    auto b=p.start(3,{},{1100,0,0},2);assert(b);p.expire(3.01);assert(!p.find(3));
    assert(!p.start(4,{NAN,0,0},{1100,0,0},4));
    assert(!p.start(4,{},{INFINITY,0,0},4));
    p.reset(8);assert(p.start(1,{},{1100,0,0},4));
    assert(!p.acknowledge(7,1,1,{},{1100,0,0},4.1));
    const float delays[]={0.f,0.05f,0.1f,0.15f,0.25f};
    for(float rtt:delays) {
        Pool q;q.reset(1);auto s=q.start(8,{},{1100,0,0},10);
        assert(s);assert(q.acknowledge(1,8,15,{1100*rtt*.5f,0,0},{1100,0,0},10+rtt));
        assert(fabs(s->at(10+rtt+.1).x-(rtt+.1)*1100)<.01);
    }
    p.reset(9);
    for(int i=1;i<=32;i++)assert(p.start(i,{},{1100,0,0},0));
    assert(!p.start(33,{},{1100,0,0},0));
    p.reset(10);assert(p.start(INT_MAX,{},{1100,0,0},0));
    p.end(10,INT_MAX);assert(!p.start(INT_MAX,{},{1100,0,0},.1));
    BlastRules rules;
    Vec feet={0,0,0},mins={-24,-24,0},stand={24,24,82},duck={24,24,62},blast={12,0,1};
    Vec ground=selfImpulse(blast,feet,mins,stand,true,rules),air=selfImpulse(blast,feet,mins,stand,false,rules),crouch=selfImpulse(blast,feet,mins,duck,false,rules);
    assert(ground.z>0&&air.z>ground.z&&crouch.z>air.z);
    assert(selfImpulse({500,0,0},feet,mins,stand,false,rules).length()==0);
    assert(selfImpulse(blast,feet,{},{},false,rules).length()==0);
    MotionJournal journal;journal.reset(4);
    auto f=journal.fire(100,10,{0,0,60},{0,0,-1100},1,1);assert(f);
    journal.begin(101);f->impactCommand=101;f->impulseCounted=true;
    journal.begin(102);assert(f->impactCommand==101);
    journal.begin(101);assert(f->impactCommand==0&&f->impulseCounted);
    f->impactCommand=101;journal.selfDamage(100,100,{0,0,500});journal.acknowledge(100);assert(f->retired);
    journal.begin(101);assert(f->impactCommand==101);
    journal.reset(5);assert(!journal.find(100));
    Bindings bindings;bindings.add(100,0x0113006eU,1);
    auto binding=bindings.find(100);assert(binding&&!binding->ended);
    binding->seen=true;bindings.finish(100,1.2);assert(binding->ended&&fabs(binding->expires-1.7)<.0001);
    assert(bindings.find(100));
    bindings.reset();assert(!bindings.find(100));
    Pool ping175;ping175.reset(1);auto pingShot=ping175.start(1,{},{1100,0,0},10);
    assert(pingShot&&ping175.acknowledge(1,1,uint32_t(-1189937042),{1100*.0875f,0,0},{1100,0,0},10.175));
    assert(fabs(pingShot->at(10.4).x-440)<.01);
    Effects effects;effects.reset(5);int sounds=0;
    auto play=[&](const Explosion &e){sounds++;assert(e.position.x==12);return true;};
    assert(effects.queue(5,100,{{10,0,0},{0,0,1}},1));
    assert(effects.flush(true,1,play)==0&&sounds==0);
    assert(effects.queue(5,100,{{12,0,0},{0,0,1}},1.1,true));
    assert(effects.queue(5,100,{{10,0,0},{0,0,1}},1.2));
    assert(effects.flush(false,1.2,play)==1&&sounds==1);
    assert(!effects.queue(5,100,{{12,0,0},{0,0,1}},1.3,true));
    assert(effects.flush(false,1.3,play)==0&&sounds==1);
    assert(!effects.queue(4,101,{{12,0,0},{0,0,1}},1.4,true));
    effects.reset(6);assert(effects.queue(6,100,{{12,0,0},{0,0,1}},2,true));
    assert(effects.flush(false,2,[](const Explosion &){return false;})==0);
    assert(effects.flush(false,2.1,play)==1&&sounds==2);
    assert(effects.queue(6,101,{{12,0,0},{0,0,1}},3,true));
    assert(effects.flush(false,6,play)==0&&sounds==2);
    puts("core: duplicates, ordering, epochs, lifetime, capacity, invalid coordinates, RTT reconciliation passed");
}
