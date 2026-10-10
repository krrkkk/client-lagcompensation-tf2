#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include "abi.h"
#include "command.h"
#include "motion.h"
#include "timeline.h"
#include "weapon_profiles.h"
#include <icvar.h>
#include <tier1/convar.h>
#include "effects.h"
#include "diagnostics.h"
#include "win_api.h"

namespace rp {
static void *engine,*client,*prediction,*entities,*tools,*models,*traces,*cvars;
static IVModelInfo *modelInfo=nullptr;
static Pool pool;
static Bindings bindings;
static ModelPose modelPoses[32];
static const char *modelNames[32]{};
static double nextModelAttempt[32]{};
static MotionJournal motion;
static ServerClock serverClock;
static int serverProtocol=0,helloAttempts=0;
static bool repredictPending=false;
static double clockAt=-10;
struct PendingLaunch {int command=0,tick=0,item=0,life=0;uint32_t weapon=0xffffffffU;double created=0;float charge=0;};
static PendingLaunch pendingLaunches[8];
static Effects effects;
static BlastRules blastRules;
static int life=1;
static bool wasAlive=false,movementInvalid=false;
static uint64_t localBlasts=0,localImpulses=0,confirmedDamage=0;
static Renderable visuals[32],flames[32];
static const model_t *flameModel=nullptr;
static float flameScale=.1f;
static double nextFlameAttempt=0;
static bool paused=false,loaded=false,enabled=true;
static ConsoleCommand controlCommand;
static volatile LONG hidden[2048]{};
static volatile LONG64 hiddenRenderables[64]{};
struct AttachmentTarget {void *renderable=nullptr;Vec position{};void *entity=nullptr;uint32_t reference=0xffffffffU;};
static AttachmentTarget attachmentTargets[64];
static SRWLOCK attachmentLock=SRWLOCK_INIT;
static volatile LONG visualPhase=0;
static thread_local bool inAttachment=false;
static uint64_t boundCount=0,drawCount=0;
static double helloAt=-10,heartbeat=-100;
static int userId=0;
static FILE *logFile=nullptr;
static unsigned logLines=0;
static uint64_t predictedCount=0,ackCount=0,terminalCount=0;
static void (*engineMsg)(const char*,...)=nullptr;

static double now() {
    LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);
    return double(c.QuadPart)/double(f.QuadPart);
}
static void log(const char *format,...) {
    if(logLines++>2000)return;
    char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(engineMsg)engineMsg("[projectile lab] %s\n",text);
    if(logFile){fprintf(logFile,"%.3f %s\n",now(),text);fflush(logFile);}
}
static bool readable(const void *p,size_t size) {
    if(!p||!size)return false;
    uintptr_t begin=reinterpret_cast<uintptr_t>(p),end=begin+size;
    if(end<begin)return false;
    while(begin<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(begin),&m,sizeof(m)))return false;
        if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=begin)return false;begin=next;
    }
    return true;
}
static bool textEqual(const char *a,const char *b) {
    size_t n=strlen(b)+1;return readable(a,n)&&memcmp(a,b,n)==0;
}
static bool executable(void *p) {
    MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT&&
        (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))&&!(m.Protect&PAGE_GUARD);
}
static bool method(void *p,int slot) {
    if(!readable(p,sizeof(void*)))return false;
    auto v=*reinterpret_cast<void***>(p);
    return readable(v,(slot+1)*sizeof(void*))&&executable(v[slot]);
}
struct Hook {
    void **slot=nullptr;void *original=nullptr,*replacement=nullptr;
    bool install(void *object,int index,void *target) {
        if(!method(object,index))return false;
        slot=&(*reinterpret_cast<void***>(object))[index];original=*slot;replacement=target;
        DWORD old;if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&old)){slot=nullptr;return false;}
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot),target);
        DWORD ignored;VirtualProtect(slot,sizeof(void*),old,&ignored);return true;
    }
    void restore() {
        if(!slot)return;
        if(readable(slot,sizeof(void*))&&*slot==replacement){DWORD old;if(VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&old)){
            InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot),original);DWORD ignored;VirtualProtect(slot,sizeof(void*),old,&ignored);
        }}
        slot=nullptr;
    }
};
static Hook runHook,frameHook,messageHook,drawHook,setupHook,finishHook,updateHook,attachmentHook;
static Hook originHooks[32];

struct Offsets {
    int active=-1,life=-1,view=-1,flags=-1,clip=-1,item=-1,deflected=-1,tick=-1,mins=-1,maxs=-1,velocity=-1,owner=-1,lastFire=-1,team=-1,stickyCharge=-1,bowCharge=-1,regen=-1,energy=-1;
    int collision=-1,nextAttack=-1;
    bool ready=false;
};
static Offsets offsets;
struct ExplosionEffect {
    ClientClass *type=nullptr;
    int origin=-1,normal=-1,weapon=-1,definition=-1,sound=-1,particle=-1;
    RecvProp *entity=nullptr;
    bool ready=false;
};
static ExplosionEffect explosion;
struct SmokeEffect {ClientClass *type=nullptr;int origin=-1,model=-1,scale=-1,rate=-1,sprite=-1;bool checked=false;};
static SmokeEffect smoke;

static int prop(RecvTable *table,const char *name,int depth=0) {
    if(depth>16||!readable(table,sizeof(*table))||table->count<0||table->count>2048||!readable(table->props,sizeof(RecvProp)*table->count))return -1;
    for(int i=0;i<table->count;i++){
        auto &p=table->props[i];if(p.offset<0||p.offset>65536)continue;
        if(textEqual(p.name,name))return p.offset;
        if(p.table){int nested=prop(p.table,name,depth+1);if(nested>=0&&p.offset+nested<=65536)return p.offset+nested;}
    }
    return -1;
}
static int netvar(const char *className,const char *name) {
    auto c=call<ClientClass*>(client,8);
    for(int n=0;c&&n<1024;n++,c=c->next){if(!readable(c,sizeof(*c)))break;if(textEqual(c->name,className))return prop(c->table,name);}
    return -1;
}
static RecvProp *directProp(RecvTable *table,const char *name) {
    if(!readable(table,sizeof(*table))||table->count<0||table->count>2048||!readable(table->props,table->count*sizeof(RecvProp)))return nullptr;
    for(int i=0;i<table->count;i++)if(textEqual(table->props[i].name,name))return &table->props[i];
    return nullptr;
}
static bool resolveExplosion() {
    auto c=call<ClientClass*>(client,8);
    for(int n=0;c&&n<1024;n++,c=c->next){
        if(!readable(c,sizeof(*c)))break;
        if(textEqual(c->name,"CTETFExplosion")){explosion.type=c;break;}
    }
    if(!explosion.type||!executable(explosion.type->event))return false;
    auto t=explosion.type->table;
    explosion.origin=prop(t,"m_vecOrigin[0]");explosion.normal=prop(t,"m_vecNormal");
    explosion.weapon=prop(t,"m_iWeaponID");explosion.definition=prop(t,"m_nDefID");
    explosion.sound=prop(t,"m_nSound");explosion.particle=prop(t,"m_iCustomParticleIndex");
    explosion.entity=directProp(t,"entindex");
    explosion.ready=explosion.origin>=0&&explosion.normal>explosion.origin&&explosion.weapon>explosion.normal&&
        explosion.definition>explosion.weapon&&explosion.sound>explosion.definition&&explosion.particle>explosion.sound&&
        explosion.particle+4-explosion.origin<256&&explosion.entity&&executable(explosion.entity->proxy);
    return explosion.ready;
}
static bool playExplosion(Vec position,Vec normal,int weapon=22,int definition=18,int sound=11,int particle=65535,int entity=0) {
    StageScope phase(Stage::Explosion);
    if(!explosion.ready||!position.valid()||!normal.valid())return false;
    void *event=reinterpret_cast<void*(*)()>(explosion.type->event)();
    if(!method(event,11))return false;
    auto base=static_cast<uint8_t*>(call<void*>(event,11));
    int start=explosion.origin,size=explosion.particle+4-start;
    if(!readable(base+start,size))return false;
    uint8_t saved[256];memcpy(saved,base+start,size);
    memcpy(base+explosion.origin,&position,sizeof(position));memcpy(base+explosion.normal,&normal,sizeof(normal));
    memcpy(base+explosion.weapon,&weapon,4);memcpy(base+explosion.definition,&definition,4);
    if(particle==-1)particle=65535;
    memcpy(base+explosion.sound,&sound,4);memcpy(base+explosion.particle,&particle,4);
    alignas(8) uint8_t proxy[40]{};
    memcpy(proxy,&explosion.entity,sizeof(void*));memcpy(proxy+8,&entity,4);
    reinterpret_cast<void(*)(const void*,void*,void*)>(explosion.entity->proxy)(proxy,base,base+explosion.entity->offset);
    call<void,int>(event,7,0);
    memcpy(base+start,saved,size);
    return true;
}

static bool playTrailSmoke(Vec position) {
    if(!position.valid()||!modelInfo)return false;
    if(!smoke.checked){
        smoke.checked=true;
        auto type=call<ClientClass*>(client,8);
        for(int i=0;type&&i<1024;i++,type=type->next){if(!readable(type,sizeof(*type)))break;if(textEqual(type->name,"CTESmoke")){smoke.type=type;break;}}
        if(smoke.type){smoke.origin=prop(smoke.type->table,"m_vecOrigin");smoke.model=prop(smoke.type->table,"m_nModelIndex");smoke.scale=prop(smoke.type->table,"m_fScale");smoke.rate=prop(smoke.type->table,"m_nFrameRate");}
        smoke.sprite=modelInfo->GetModelIndex("sprites/steam1.vmt");
        if(smoke.sprite<0)smoke.sprite=0;
    }
    if(!smoke.type||!executable(smoke.type->event)||smoke.origin<0||smoke.model<smoke.origin+12||smoke.scale<=smoke.model||smoke.rate<=smoke.scale||smoke.rate+4-smoke.origin>128)return false;
    void *event=reinterpret_cast<void*(*)()>(smoke.type->event)();if(!method(event,11))return false;
    auto base=static_cast<uint8_t*>(call<void*>(event,11));int bytes=smoke.rate+4-smoke.origin;
    if(!readable(base+smoke.origin,bytes))return false;
    uint8_t saved[128];memcpy(saved,base+smoke.origin,bytes);
    float scale=.15f;int rate=24;
    memcpy(base+smoke.origin,&position,sizeof(position));memcpy(base+smoke.model,&smoke.sprite,4);memcpy(base+smoke.scale,&scale,4);memcpy(base+smoke.rate,&rate,4);
    call<void,int>(event,7,0);memcpy(base+smoke.origin,saved,bytes);return true;
}
template<class T> static T read(void *p,int offset,T fallback={}) {
    if(offset<0||offset>65536)return fallback;
    auto address=static_cast<uint8_t*>(p)+offset;
    if(!readable(address,sizeof(T)))return fallback;
    T value;memcpy(&value,address,sizeof(value));return value;
}
static bool resolveOffsets() {
    offsets.nextAttack=netvar("CTFRocketLauncher","m_flNextPrimaryAttack");
    offsets.collision=netvar("CTFPlayer","m_CollisionGroup");
    offsets.active=netvar("CTFPlayer","m_hActiveWeapon");
    offsets.life=netvar("CTFPlayer","m_lifeState");
    offsets.view=netvar("CTFPlayer","m_vecViewOffset[0]");
    offsets.flags=netvar("CTFPlayer","m_fFlags");
    offsets.clip=netvar("CTFRocketLauncher","m_iClip1");
    offsets.item=netvar("CTFRocketLauncher","m_iItemDefinitionIndex");
    offsets.deflected=netvar("CTFProjectile_Rocket","m_iDeflected");
    offsets.owner=netvar("CTFProjectile_Rocket","m_hOwnerEntity");
    offsets.lastFire=netvar("CTFRocketLauncher","m_flLastFireTime");
    offsets.regen=netvar("CTFRocketLauncher","m_flEffectBarRegenTime");
    offsets.energy=netvar("CTFRocketLauncher","m_flEnergy");
    offsets.team=netvar("CTFPlayer","m_iTeamNum");
    offsets.stickyCharge=netvar("CTFPipebombLauncher","m_flChargeBeginTime");
    offsets.bowCharge=netvar("CTFCompoundBow","m_flChargeBeginTime");
    offsets.tick=netvar("CTFPlayer","m_nTickBase");offsets.mins=netvar("CTFPlayer","m_vecMins");offsets.maxs=netvar("CTFPlayer","m_vecMaxs");
    offsets.velocity=netvar("CTFPlayer","m_vecVelocity[0]");
    offsets.ready=offsets.active>=0&&offsets.life>=0&&offsets.view>=0&&offsets.flags>=0&&offsets.clip>=0&&offsets.item>=0&&offsets.deflected>=0&&offsets.tick>=0&&offsets.mins>=0&&offsets.maxs>=0&&offsets.velocity>=0&&offsets.owner>=0;
    log("netvars active=%d life=%d view=%d flags=%d clip=%d item=%d deflect=%d ready=%d",offsets.active,offsets.life,offsets.view,offsets.flags,offsets.clip,offsets.item,offsets.deflected,offsets.ready);
    return offsets.ready;
}
static bool allowedServer() {
    if(!engine||paused||!call<bool>(engine,26)||call<bool>(engine,76))return false;
    void *channel=call<void*>(engine,72);if(!method(channel,1))return false;
    const char *address=call<const char*>(channel,1);
    return textEqual(address,"185.137.93.189:27018")||textEqual(address,"127.0.0.1:27018");
}
static void setEnabled(const CommandArgs &args) {
    if(args.count==2&&readable(args.argv,sizeof(const char*)*2)){
        if(textEqual(args.argv[1],"0"))enabled=false;
        else if(textEqual(args.argv[1],"1"))enabled=true;
        else {log("usage: cl_projectile_prediction 0 or 1");return;}
        if(!enabled)for(auto &pending:pendingLaunches)pending={};
        if(allowedServer()){char text[64];snprintf(text,sizeof(text),"rp_hello %d %d",serverProtocol?serverProtocol:4,enabled?1:0);call<void,const char*,bool>(engine,6,text,true);}
        helloAt=now();
    }else if(args.count!=1){log("usage: cl_projectile_prediction 0 or 1");return;}
    log("prediction %s; server %s; shots=%llu/%llu bound=%llu drawn=%llu blasts=%llu jumps=%llu damage=%llu; movement=%s",enabled?"on":"off",pool.epoch?"ready":"not connected",(unsigned long long)predictedCount,(unsigned long long)ackCount,(unsigned long long)boundCount,(unsigned long long)drawCount,(unsigned long long)localBlasts,(unsigned long long)localImpulses,(unsigned long long)confirmedDamage,movementInvalid?"unavailable":"ready");
}
static void *localPlayer() {return call<void*,int>(entities,3,call<int>(engine,12));}
static uint32_t entityHandle(void *entity) {
    if(!method(entity,2))return 0xffffffffU;
    const auto &reference=call<const uint32_t&>(entity,2);
    return readable(&reference,sizeof(reference))?reference:0xffffffffU;
}
static void *byHandle(uint32_t handle) {
    StageScope phase(Stage::WeaponLookup);
    return resolveNetworkHandle(handle,[](int index){return call<void*,int>(entities,3,index);},entityHandle);
}

static bool projectileOwned(void *entity,uint32_t owner) {
    auto network=call<void*>(entity,4);if(!method(network,2))return false;
    auto type=call<ClientClass*>(network,2);if(!readable(type,sizeof(*type)))return false;
    struct Properties {ClientClass *type=nullptr;int owner=-1,thrower=-1,deflected=-1;};
    static Properties cache[32];Properties *fields=nullptr;
    for(auto &c:cache)if(c.type==type){fields=&c;break;}
    if(!fields)for(auto &c:cache)if(!c.type){c={type,prop(type->table,"m_hOwnerEntity"),prop(type->table,"m_hThrower"),prop(type->table,"m_iDeflected")};fields=&c;break;}
    if(!fields)return false;
    if(read<int>(entity,fields->deflected,0)>0)return false;
    uint32_t actual=read<uint32_t>(entity,fields->owner,0xffffffffU);
    if(actual==owner)return true;
    uint32_t thrower=read<uint32_t>(entity,fields->thrower,0xffffffffU);
    return thrower==owner||(networkIndex(actual)<0&&networkIndex(thrower)<0);
}

static ModelPose *prepareModel(const WeaponSpec &spec) {
    int slot=-1;
    for(int i=0;i<32;i++)if(modelNames[i]&&strcmp(modelNames[i],spec.model)==0){slot=i;break;}
    if(slot<0)for(int i=0;i<32;i++)if(!modelNames[i]){slot=i;modelNames[i]=spec.model;break;}
    if(slot<0)return nullptr;
    auto &rocketPose=modelPoses[slot];
    if(rocketPose.model&&rocketPose.count>0)return &rocketPose;
    if(!modelInfo||now()<nextModelAttempt[slot])return nullptr;
    nextModelAttempt[slot]=now()+1;
    auto model=call<const model_t*,const char*,bool>(engine,13,spec.model,false);
    if(!model)return nullptr;
    studiohdr_t *header=modelInfo->GetStudiomodel(model);
    if(!readable(header,sizeof(*header))||header->id!=0x54534449||header->length<int(sizeof(*header))||header->length>16*1024*1024||header->numbones<1||header->numbones>128||header->boneindex<0)return nullptr;
    size_t bytes=size_t(header->numbones)*sizeof(mstudiobone_t);
    if(size_t(header->boneindex)+bytes>size_t(header->length))return nullptr;
    auto bones=reinterpret_cast<const mstudiobone_t*>(reinterpret_cast<const uint8_t*>(header)+header->boneindex);
    if(!readable(bones,bytes))return nullptr;
    for(int i=0;i<header->numbones;i++){
        Matrix bind;memcpy(&bind,&bones[i].poseToBone,sizeof(bind));
        for(auto &row:bind.m)for(float value:row)if(!isfinite(value))return nullptr;
        rocketPose.bones[i]=inverseRigid(bind);
    }
    Vector mins,maxs;modelInfo->GetModelRenderBounds(model,mins,maxs);
    if(!validBounds({mins.x,mins.y,mins.z},{maxs.x,maxs.y,maxs.z}))modelInfo->GetModelBounds(model,mins,maxs);
    if(validBounds({mins.x,mins.y,mins.z},{maxs.x,maxs.y,maxs.z})){rocketPose.mins={mins.x,mins.y,mins.z};rocketPose.maxs={maxs.x,maxs.y,maxs.z};}
    rocketPose.alignment=projectileAlignment();
    rocketPose.model=model;rocketPose.count=header->numbones;rocketPose.skins=header->numskinfamilies>0&&header->numskinfamilies<=64?header->numskinfamilies:1;
    log("projectile model prepared: item=%d bones=%d",spec.item,rocketPose.count);
    return &rocketPose;
}

static const Vec &projectileOrigin(void *self) {
    void **slot=&(*reinterpret_cast<void***>(self))[10];void *original=nullptr;
    for(const auto &hook:originHooks)if(hook.slot==slot){original=hook.original;break;}
    static const Vec zero{};
    if(!original)return zero;
    const Vec &native=invoke<const Vec&>(original,self);
    if(inAttachment||InterlockedCompareExchange(&visualPhase,0,0)==0||call<bool>(prediction,14))return native;
    uint32_t reference=entityHandle(self);const Vec *target=nullptr;
    AcquireSRWLockShared(&attachmentLock);
    for(const auto &entry:attachmentTargets)if(entry.entity==self&&entry.reference==reference){target=&entry.position;break;}
    ReleaseSRWLockShared(&attachmentLock);
    return target?*target:native;
}

static void bindOrigin(void *entity) {
    if(!method(entity,10))return;
    void **slot=&(*reinterpret_cast<void***>(entity))[10];
    for(const auto &hook:originHooks)if(hook.slot==slot)return;
    for(auto &hook:originHooks)if(!hook.slot){hook.install(entity,10,reinterpret_cast<void*>(&projectileOrigin));return;}
}

static void prepareFlame() {
    if(flameModel||!modelInfo||now()<nextFlameAttempt||!method(tools,19))return;
    nextFlameAttempt=now()+5;
    auto model=call<const model_t*,const char*,bool>(engine,13,"effects/brightglow_y_nomodel.vmt",false);
    if(!model||modelInfo->GetModelType(model)!=2)return;
    Vector lo,hi;modelInfo->GetModelBounds(model,lo,hi);
    float width=fmaxf(hi.x-lo.x,fmaxf(hi.y-lo.y,hi.z-lo.z));
    flameScale=isfinite(width)&&width>1?clamp(14.f/width,.005f,1.f):.1f;
    flameModel=model;log("local exhaust ready");
}
static void prewarmProjectiles() {
    void *player=localPlayer();if(!player)return;
    void *weapon=byHandle(read<uint32_t>(player,offsets.active,0xffffffffU));if(!weapon)return;
    auto spec=weaponSpec(read<uint16_t>(weapon,offsets.item,65535));
    if(spec){prepareModel(*spec);if(spec->rocket())prepareFlame();}
}

static bool rocketAttachment(void *self,int number,Matrix &matrix) {
    bool previous=inAttachment;inAttachment=true;
    bool result=invoke<bool,int,Matrix&>(attachmentHook.original,self,number,matrix);
    Vec original=call<const Vec&>(self,1);
    inAttachment=previous;
    if(!result)return false;
    Vec target{};bool found=false;
    AcquireSRWLockShared(&attachmentLock);
    for(const auto &entry:attachmentTargets)if(entry.renderable==self){target=entry.position;found=true;break;}
    ReleaseSRWLockShared(&attachmentLock);
    if(found){
        Vec delta=target-original;
        if(delta.valid()&&delta.length()<2048){matrix.m[0][3]+=delta.x;matrix.m[1][3]+=delta.y;matrix.m[2][3]+=delta.z;}
    }
    return true;
}

struct Filter {
    void *skip,*second;bool ignorePlayers,projectile;
    explicit Filter(void *s,void *other,bool ignore,bool rocket):skip(s),second(other),ignorePlayers(ignore),projectile(rocket){}
    virtual bool hit(void *entity,int) {
        if(entity==skip||entity==second)return false;
        if(projectile&&entity){
            uint32_t ref=entityHandle(entity);int index=networkIndex(ref);
            if(index>0&&call<void*,int>(entities,3,index)==entity&&!rocketCollisionGroup(read<int>(entity,offsets.collision,-1)))return false;
        }
        if(ignorePlayers)for(int i=1;i<=call<int>(engine,21);i++)if(entity==call<void*,int>(entities,3,i))return false;
        return true;
    }
    virtual int type() const {return 0;}
};
static Trace trace(Vec a,Vec b,void *skip,uint32_t mask=0x4600400bU,void *second=nullptr,bool ignorePlayers=false,float hull=0) {
    Ray ray(a,b,hull);Trace out{};out.fraction=1;out.end=b;Filter filter(skip,second,ignorePlayers,mask==0x0200400bU);
    call<void,const Ray&,uint32_t,Filter*,Trace*>(traces,4,ray,mask,&filter,&out);
    return out;
}
static void removeVisual(Renderable &v) {
    v.visible=false;
    if(v.registered){call<void,void*>(tools,15,&v);v.registered=false;v.renderHandle=0xffff;}
    if(v.instance!=0xffff){call<void,uint16_t>(models,4,v.instance);v.instance=0xffff;}
    v.model=nullptr;
    v.pose=nullptr;v.command=0;v.firstDrawMicros=0;v.drawReported=false;v.lastSmoke=0;
}
static void reset(int epoch=0) {
    for(auto &entry:hidden)InterlockedExchange(&entry,0);
    for(auto &entry:hiddenRenderables)InterlockedExchange64(&entry,0);
    InterlockedExchange(&visualPhase,0);
    AcquireSRWLockExclusive(&attachmentLock);for(auto &entry:attachmentTargets)entry={};ReleaseSRWLockExclusive(&attachmentLock);
    for(auto &v:visuals)removeVisual(v);
    for(auto &v:flames)removeVisual(v);
    flameModel=nullptr;nextFlameAttempt=0;
    pool.reset(epoch);heartbeat=-100;userId=0;
    motion.reset(epoch);effects.reset(epoch);wasAlive=false;
    serverClock.reset();serverProtocol=0;helloAttempts=0;clockAt=-10;repredictPending=false;
    for(auto &pending:pendingLaunches)pending={};
    bindings.reset();smoke={};for(auto &pose:modelPoses)pose={};for(auto &name:modelNames)name=nullptr;for(auto &time:nextModelAttempt)time=0;
}

static void predictionUpdate(void *self,int start,bool valid,int acknowledged,int outgoing) {
    if(pool.epoch)motion.acknowledge(acknowledged);
    if(repredictPending&&valid&&pool.epoch&&!call<bool>(prediction,14)){
        call<void>(self,7);repredictPending=false;
    }
    invoke<void,int,bool,int,int>(updateHook.original,self,start,valid,acknowledged,outgoing);
}

static void setupMove(void *self,void *player,UserCmd *cmd,void *helper,MoveData *move) {
    invoke<void,void*,UserCmd*,void*,MoveData*>(setupHook.original,self,player,cmd,helper,move);
    if(movementInvalid||!offsets.ready||!pool.epoch||player!=localPlayer()||!readable(move,sizeof(*move)))return;
    if(move->player!=entityHandle(player)||!move->velocity.valid()||!move->origin.valid()||move->velocity.length()>10000||(move->velocity-read<Vec>(player,offsets.velocity)).length()>1.f){movementInvalid=true;log("movement binding rejected; prediction disabled for movement");}
}

static void predictImpulse(void *player,UserCmd *cmd,MoveData *move) {
    StageScope phase(Stage::MoveSetup);
    if(movementInvalid||!offsets.ready||!pool.epoch||!allowedServer()||now()-heartbeat>10||player!=localPlayer()||read<uint8_t>(player,offsets.life,1)!=0)return;
    if(!readable(cmd,sizeof(*cmd))||!readable(move,sizeof(*move))||cmd->command<=0)return;
    if(!wasAlive){life++;wasAlive=true;}
    if(move->maxSpeed<=1.f||(read<int>(player,offsets.flags)&32)!=0)return;
    if(move->player!=entityHandle(player)||!move->velocity.valid()||!move->origin.valid()||!move->view.valid()||move->maxSpeed<0||move->maxSpeed>2000||move->velocity.length()>10000||(move->view-cmd->view).length()>10){
        movementInvalid=true;log("movement binding rejected; prediction disabled for movement");return;
    }
    motion.begin(cmd->command);
    int tick=read<int>(player,offsets.tick,-1);
    Vec mins=read<Vec>(player,offsets.mins),maxs=read<Vec>(player,offsets.maxs);
    void *world=call<void*,int>(entities,3,0);
    for(auto &flight:motion.flights){
        if(flight.command<=0||flight.command>=cmd->command||flight.life!=life||flight.retired||flight.impactCommand||now()-flight.born>12)continue;
        if(flight.selfConfirmed){
            int applied=flight.serverCommand>flight.command?flight.serverCommand:flight.command+1;
            if(cmd->command==applied){move->velocity=move->velocity+flight.confirmedImpulse;flight.impactCommand=cmd->command;}
            continue;
        }
        if(!flight.rocket)continue;
        int ticks=tick-flight.tick;if(ticks<=0||ticks>800)continue;
        Vec end=flight.launch+flight.velocity*(ticks*blastRules.tick);
        auto visual=pool.find(flight.command);void *rocket=visual&&visual->confirmed?byHandle(visual->reference):nullptr;
        diagnostics.set(Stage::MoveTrace);Trace hit=trace(flight.launch,end,player,0x0200400bU,rocket);
        if(hit.fraction>=1||hit.startSolid||hit.allSolid||(!hit.entity&&hit.fraction==0))continue;
        if(hit.entity&&hit.entity!=world)continue;
        flight.impactCommand=cmd->command;flight.staticHit=true;flight.normal=hit.plane.normal;
        flight.impact=hit.end+hit.plane.normal;
        if(visual){visual->stopped=true;visual->position=hit.end;}
        if(hit.surface.flags&0x4)continue;
        Vec center=move->origin+(mins+maxs)*.5f;
        Trace sight=trace(flight.impact,center,player,0x06004003U,rocket,true);
        if(!sight.startSolid&&sight.fraction>=.999f){
            int flags=read<int>(player,offsets.flags);
            Vec push=selfImpulse(flight.impact,move->origin,mins,maxs,(flags&1)!=0,blastRules,(flags&512)!=0,flight.damage,flight.pushScale,flight.radius);
            if(flight.selfConfirmed&&flight.confirmedImpulse.valid())push=flight.confirmedImpulse;
            if(push.valid()&&push.length()>0){move->velocity=move->velocity+push;flight.impulse=push;if(!flight.impulseCounted){localImpulses++;flight.impulseCounted=true;}}
        }
        effects.queue(pool.epoch,flight.command,{flight.impact,flight.normal,flight.weaponId,flight.item},now());
    }
}

static void finishMove(void *self,void *player,UserCmd *cmd,MoveData *move) {
    predictImpulse(player,cmd,move);
    invoke<void,void*,UserCmd*,MoveData*>(finishHook.original,self,player,cmd,move);
}

static void launch(void *player,int shotCommand,int fireTick,Vec view,const WeaponSpec *spec,float charge,bool detailed=false,bool charged=false) {
    diagnostics.set(Stage::ShotPosition);if(detailed)log("first attack: shot position");
    if(!method(player,10))return;
    const Vector &absolute=static_cast<IClientEntity*>(player)->GetAbsOrigin();
    Vec pos={absolute.x,absolute.y,absolute.z};
    Vec eyeOffset=read<Vec>(player,offsets.view);
    if(!eyeOffset.valid()||eyeOffset.z<0||eyeOffset.z>100||!pos.valid())return;
    Vec eye=pos+eyeOffset,f,r,u;basis(view,f,r,u);
    diagnostics.set(Stage::AimTrace);if(detailed)log("first attack: aim trace");
    Vec aimEnd=eye+f*2000.f;Trace target=trace(eye,aimEnd,player);
    auto flip=static_cast<ICvar*>(cvars)->FindVar("cl_flipviewmodels");
    float side=12.f,forward=23.5f,up=(read<int>(player,offsets.flags)&2)?8.f:-3.f;
    if(spec->kind==ProjectileKind::Arrow||spec->kind==ProjectileKind::Bolt||spec->kind==ProjectileKind::Repair||spec->kind==ProjectileKind::EnergyBall||spec->kind==ProjectileKind::EnergyRing||spec->kind==ProjectileKind::Hook){side=-8;up=-3;}
    if(spec->physical()){forward=16;side=8;up=-6;}
    if(spec->kind==ProjectileKind::Bolt||spec->kind==ProjectileKind::Repair||spec->kind==ProjectileKind::EnergyBall)side=-side;
    if(spec->flags&1)side=0;
    if(flip&&flip->GetBool())side=-side;
    Vec start=eye+f*forward+r*side+u*up;
    diagnostics.set(Stage::MuzzleTrace);if(detailed)log("first attack: muzzle trace");
    Trace muzzle=trace(eye,start,player,0x400bU,nullptr,false,spec->hull);if(muzzle.startSolid)return;start=muzzle.end;
    Vec direction=spec->physical()?f:((target.fraction>0.1f?target.end:aimEnd)-start).unit();
    float speed=spec->speed+(spec->maxSpeed-spec->speed)*charge;
    float gravity=spec->kind==ProjectileKind::Arrow?.5f-.4f*charge:spec->gravity;
    Vec velocity=direction*speed+u*spec->up;
    if(spec->kind==ProjectileKind::Cleaver||spec->kind==ProjectileKind::Ball||spec->kind==ProjectileKind::Ornament)velocity=(f*10.f+u).unit()*speed;
    if(spec->kind==ProjectileKind::Ball||spec->kind==ProjectileKind::Ornament)start=pos+Vec{0,0,50}+f*32.f;
    if(!wasAlive){life++;wasAlive=true;}
    diagnostics.set(Stage::StoreShot);
    if(auto flight=motion.fire(shotCommand,fireTick,start,velocity,now(),life)){flight->item=spec->item;flight->weaponId=spec->weaponId;flight->rocket=spec->rocket()||spec->kind==ProjectileKind::EnergyBall;flight->damage=flight->rocket?90.f:spec->damage;flight->pushScale=spec->selfPush;flight->radius=charged?121.f*1.33f:121.f;}
    if(auto s=pool.start(shotCommand,start,velocity,now())){
        s->item=spec->item;
        s->acceleration=s->flightAcceleration={0,0,-800*gravity};s->rotation=view;s->rotationAt=now();if(spec->physical()&&(spec->flags&32)==0)s->angularVelocity={600,0,0};
        predictedCount++;if(predictedCount<=12)log("predicted command=%d item=%d",s->command,s->item);
    }
}

static void run(void *self,void *player,UserCmd *command,void *helper) {
    StageScope phase(Stage::Prediction);
    bool valid=enabled&&offsets.ready&&allowedServer()&&pool.epoch&&now()-heartbeat<10&&readable(command,sizeof(UserCmd))&&command->command>0&&player==localPlayer()&&read<uint8_t>(player,offsets.life,1)==0;
    int fireTick=read<int>(player,offsets.tick,-1),clip=-1;uint32_t weaponHandle=0xffffffffU;void *weapon=nullptr;
    const WeaponSpec *spec=nullptr;float lastFire=-1,nextAttack=-1,charge=0,energy=0;
    if(valid){
        InterlockedExchange(&diagnostics.command,command->command);
        if(!wasAlive){life++;wasAlive=true;}
        weaponHandle=read<uint32_t>(player,offsets.active,0xffffffffU);weapon=byHandle(weaponHandle);
        if(weapon){
            int item=read<uint16_t>(weapon,offsets.item,0xffff);spec=weaponSpec(item);
            if(spec&&!(serverProtocol>=4||(serverProtocol==3&&spec->rocket())||item==18||item==205))spec=nullptr;
            clip=read<int>(weapon,offsets.clip,-1);lastFire=read<float>(weapon,offsets.lastFire,-1);nextAttack=read<float>(weapon,offsets.nextAttack,-1);
            energy=read<float>(weapon,offsets.energy,0);
            if(spec&&spec->chargeTime>0){float began=read<float>(weapon,spec->kind==ProjectileKind::Sticky?offsets.stickyCharge:offsets.bowCharge,0);if(began>0)charge=clamp((fireTick*blastRules.tick-began)/spec->chargeTime,0,1);}
        }
    }
    diagnostics.set(Stage::NativeCommand);
    invoke<void,void*,UserCmd*,void*>(runHook.original,self,player,command,helper);
    if(!valid)return;
    if(spec&&byHandle(weaponHandle)==weapon){
        bool ball=spec->kind==ProjectileKind::Ball||spec->kind==ProjectileKind::Ornament;
        bool fired=(clip>0&&read<int>(weapon,offsets.clip,-1)==clip-1)||(lastFire>=0&&read<float>(weapon,offsets.lastFire,-1)>lastFire+.0001f);
        if(ball)fired=(command->buttons&2048)!=0&&nextAttack>=0&&read<float>(weapon,offsets.nextAttack,-1)>nextAttack+.0001f;
        if(fired){
            if(spec->delay>0){
                PendingLaunch *slot=nullptr;
                for(auto &pending:pendingLaunches)if(pending.command==command->command){slot=&pending;break;}
                if(!slot)for(auto &pending:pendingLaunches)if(!pending.command||now()-pending.created>1){slot=&pending;break;}
                if(slot)*slot={command->command,fireTick+int(ceilf(spec->delay/blastRules.tick)),spec->item,life,weaponHandle,now(),charge};
            }else launch(player,command->command,fireTick,command->view,spec,charge,false,spec->kind==ProjectileKind::EnergyBall&&energy>=10&&read<float>(weapon,offsets.energy,0)<.1f);
        }
    }
    for(auto &pending:pendingLaunches)if(pending.command){
        if(pending.life!=life||now()-pending.created>1||!byHandle(pending.weapon)){pending={};continue;}
        if(fireTick>=pending.tick){
            const auto scheduled=weaponSpec(pending.item);
            if(scheduled)launch(player,pending.command,fireTick,command->view,scheduled,pending.charge);
            pending={};
        }
    }
}

static bool readMessage(BitReader &reader,char text[256]) {
    if(!readable(&reader,sizeof(reader))||reader.overflow||reader.bytes<8||reader.bytes>4096||reader.bits<0||reader.bits>reader.bytes*8||reader.current<0||reader.current%8!=0)return false;
    int at=reader.current/8,limit=(reader.bits+7)/8;
    if(at+6>limit||!readable(reader.data,reader.bytes)||reader.data[at++]!=1)return false;
    if(memcmp(reader.data+at,"RP2 ",4)!=0&&memcmp(reader.data+at,"RP3 ",4)!=0&&memcmp(reader.data+at,"RP4 ",4)!=0)return false;
    int out=0;while(at<limit&&out<255){text[out++]=char(reader.data[at++]);if(text[out-1]==0)return true;}
    return false;
}
static bool userMessage(void *self,int type,BitReader &reader) {
    char text[256];
    if(allowedServer()&&readMessage(reader,text)){
        int protocol=text[2]-'0';text[2]='2';
        char kind=0;int epoch=0,cmd=0,ref=0,n=0;
        if(sscanf(text,"RP2 %c %d",&kind,&epoch)!=2||epoch<=0)return true;
        if(kind=='H'){
            float tick=0,cap=0,ground=0,air=0,self=0;int uid=0,mode=0;
            if(sscanf(text,"RP2 H %d %d %f %f %f %f %f %d %n",&epoch,&uid,&tick,&cap,&ground,&air,&self,&mode,&n)==8&&n>0&&text[n]==0&&uid>0&&isfinite(tick)&&tick>0.005f&&tick<0.1f&&isfinite(cap)&&cap>=0&&cap<=0.151f&&isfinite(ground)&&ground>=0&&ground<=30&&isfinite(air)&&air>=0&&air<=30&&isfinite(self)&&self>=0&&self<=5){
                if(pool.epoch!=epoch||userId!=uid){reset(epoch);userId=uid;log("server accepted protocol=%d epoch=%d userid=%d",protocol,epoch,uid);}
                serverProtocol=protocol;
                blastRules={tick,ground,air,self};
                heartbeat=now();
            }
        }else if(kind=='C'&&protocol>=3&&epoch==pool.epoch){
            int nonce=0,tick=0;
            if(sscanf(text,"RP2 C %d %d %d %n",&epoch,&nonce,&tick,&n)==3&&n>0&&text[n]==0&&tick>=0)
                serverClock.receive(nonce,double(tick)*blastRules.tick,now());
        }else if(kind=='A'){
            Vec pos{},velocity{};float advance=0;
            int sampleTick=0;
            bool parsed=protocol>=3?
                sscanf(text,"RP2 A %d %d %d %f %f %f %f %f %f %f %d %n",&epoch,&cmd,&ref,&pos.x,&pos.y,&pos.z,&velocity.x,&velocity.y,&velocity.z,&advance,&sampleTick,&n)==11:
                sscanf(text,"RP2 A %d %d %d %f %f %f %f %f %f %f %n",&epoch,&cmd,&ref,&pos.x,&pos.y,&pos.z,&velocity.x,&velocity.y,&velocity.z,&advance,&n)==10;
            if(parsed&&n>0&&text[n]==0&&isfinite(advance)&&advance>=0&&advance<=0.151f){
                double time=now();
                if(pool.acknowledgeFlight(epoch,cmd,uint32_t(ref),pos,velocity,advance,time)){
                    ackCount++;if(ackCount<=12)log("confirmed command=%d reference=%d advance=%.4f age=%.4f correction=%.2f",cmd,ref,advance,float(time-pool.find(cmd)->born),pool.find(cmd)->correction.length());
                    if(auto f=motion.find(cmd)){f->launch=pos-velocity*advance;f->velocity=velocity;f->serverSpawn=true;}
                    bindings.add(cmd,clientReference(uint32_t(ref)),now());
                }
            }
        }else if(kind=='P'&&protocol>=4&&epoch==pool.epoch){
            int item=0,tick=0;Vec pos{},velocity{};float gravity=0;
            if(sscanf(text,"RP2 P %d %d %d %d %f %f %f %f %f %f %f %d %n",&epoch,&cmd,&ref,&item,&pos.x,&pos.y,&pos.z,&velocity.x,&velocity.y,&velocity.z,&gravity,&tick,&n)==12&&n>0&&text[n]==0&&gravity>=0&&gravity<=4){
                auto shot=pool.find(cmd);const auto spec=weaponSpec(item);
                if(shot&&spec&&shot->item==item){
                    float age=clamp(float(now()-shot->born)*.5f,0,.5f);serverClock.sampleAge(double(tick)*blastRules.tick,now(),age);
                    shot->acceleration=shot->flightAcceleration={0,0,-gravity*800};
                    if(pool.acknowledgeTimed(epoch,cmd,uint32_t(ref),pos,velocity,now(),age)){bindings.add(cmd,clientReference(uint32_t(ref)),now());ackCount++;}
                }
            }
        }else if(kind=='S'&&protocol>=4&&epoch==pool.epoch){
            int tick=0;Vec pos{},velocity{},rotation{};
            if(sscanf(text,"RP2 S %d %d %d %f %f %f %f %f %f %f %f %f %d %n",&epoch,&cmd,&ref,&pos.x,&pos.y,&pos.z,&velocity.x,&velocity.y,&velocity.z,&rotation.x,&rotation.y,&rotation.z,&tick,&n)==13&&n>0&&text[n]==0){
                float age=0;if(serverClock.sampleAge(double(tick)*blastRules.tick,now(),age)&&pool.state(epoch,cmd,uint32_t(ref),pos,velocity,rotation,tick,now(),age))if(auto b=bindings.find(cmd))b->expires=now()+12;
            }
        }else if(kind=='E'){
            int applied=0,selfDamage=0;
            if(sscanf(text,"RP2 E %d %d %d %d %d %n",&epoch,&cmd,&ref,&applied,&selfDamage,&n)==5&&n>0&&text[n]==0&&epoch==pool.epoch){pool.end(epoch,cmd);bindings.finish(cmd,now());motion.finish(cmd,applied,selfDamage!=0);terminalCount++;}
        }else if(kind=='B'){
            int applied=0,health=0;Vec impulse{};float damage=0;
            if(sscanf(text,"RP2 B %d %d %d %f %f %f %f %d %n",&epoch,&cmd,&applied,&impulse.x,&impulse.y,&impulse.z,&damage,&health,&n)==8&&n>0&&text[n]==0&&epoch==pool.epoch&&impulse.valid()&&impulse.length()<6000&&isfinite(damage)){
                motion.selfDamage(cmd,applied,impulse);confirmedDamage++;
                repredictPending=true;
            }
        }else if(kind=='X'){
            Vec pos{},normal{};int weapon=0,definition=0,sound=0,particle=0,entity=0;
            if(sscanf(text,"RP2 X %d %d %f %f %f %f %f %f %d %d %d %d %d %n",&epoch,&cmd,&pos.x,&pos.y,&pos.z,&normal.x,&normal.y,&normal.z,&weapon,&definition,&sound,&particle,&entity,&n)==13&&n>0&&text[n]==0&&epoch==pool.epoch&&pos.valid()&&normal.valid()&&weapon>=0&&weapon<256&&definition>=-1&&definition<100000&&sound>=0&&sound<16&&particle>=-1&&particle<65536&&entity>=0&&entity<=2047){
                auto f=motion.find(cmd);
                effects.queue(epoch,cmd,{pos,normal,weapon,definition,sound,particle,entity},now(),true);
                if(f){if(f->staticHit&&(f->impact-pos).length()>8)repredictPending=true;f->impact=pos;f->normal=normal;f->explosionReceived=true;}
                if(auto s=pool.find(cmd)){s->stopped=true;s->terminal=true;s->position=pos;}
            }
        }
        return true;
    }
    return invoke<bool,int,BitReader&>(messageHook.original,self,type,reader);
}

static void updateVisuals() {
    StageScope phase(Stage::ModelUpdate);
    for(auto &entry:hidden)InterlockedExchange(&entry,0);
    for(auto &entry:hiddenRenderables)InterlockedExchange64(&entry,0);
    double time=now();pool.expire(time);void *player=localPlayer();
    for(int i=0;i<32;i++){
        auto &s=pool.shots[i];auto &v=visuals[i];
        if(auto b=bindings.find(v.command))if(InterlockedCompareExchange64(&v.firstDrawMicros,0,0))b->replacementDrawn=true;
        if(!s.active){removeVisual(v);continue;}
        if(v.command&&v.command!=s.command)removeVisual(v);
        if(s.confirmed){void *entity=byHandle(s.reference);if(entity&&entityHandle(entity)==s.reference&&!projectileOwned(entity,entityHandle(player))){s.active=false;if(auto f=motion.find(s.command))f->retired=true;if(auto b=bindings.find(s.command))*b={};removeVisual(v);continue;}}
        Vec next=s.at(time);
        if(!next.valid()||(next-s.position).length()>2048.f){s.active=false;if(auto b=bindings.find(s.command))*b={};removeVisual(v);continue;}
        const auto spec=weaponSpec(s.item?s.item:18);
        if(s.landed)s.position=s.at(time);
        if(!s.stopped&&!s.landed){
            Trace collision=trace(s.position,next,player,spec&&spec->rocket()?0x0200400bU:0x4600400bU,s.confirmed?byHandle(s.reference):nullptr,false,spec?spec->hull:0);
            if(collision.startSolid||collision.fraction<1){
                s.position=collision.end;
                if(collision.surface.flags&4)s.stopped=true;
                else if(spec&&spec->kind==ProjectileKind::Sticky){s.landed=true;s.origin=s.position;s.velocity={};s.acceleration={};s.correction={};s.anchorTime=time;}
                else if(spec&&spec->kind==ProjectileKind::Pipe&&(spec->flags&8)==0&&!collision.startSolid){
                    Vec velocity=s.velocity+s.acceleration*float(time-s.anchorTime);float bounce=(spec->flags&4)?.05f:.45f;
                    s.velocity=(velocity-collision.plane.normal*(2*velocity.dot(collision.plane.normal)))*bounce;
                    s.origin=s.position+collision.plane.normal*.5f;s.anchorTime=time;s.correction={};
                    if(s.velocity.length()<20){s.landed=true;s.velocity={};s.acceleration={};}
                }else s.stopped=true;
            }else s.position=next;
        }
        if(s.stopped){removeVisual(v);continue;}
        auto pose=spec?prepareModel(*spec):nullptr;
        if(!v.model&&pose){v.model=pose->model;v.pose=pose;v.modelRender=models;v.command=s.command;}
        if(!v.model){removeVisual(v);continue;}
        Vec direction=s.velocity+s.acceleration*float(time-s.anchorTime);
        Vec rotation=s.landed?s.rotation:(spec&&spec->physical()?s.rotation+s.angularVelocity*float(time-s.rotationAt):angles(direction));
        v.transform(s.position,rotation);v.skin=pose&&pose->skins>1&&read<int>(player,offsets.team,2)==3?1:0;v.visible=true;
        auto binding=bindings.find(s.command);
        if(spec&&spec->rocket()&&(!binding||!binding->seen)&&time-v.lastSmoke>=.03){playTrailSmoke(s.position);v.lastSmoke=time;}
        if(!v.registered){
            {StageScope instance(Stage::ModelInstance);v.CreateModelInstance();}
            {StageScope registration(Stage::ModelRegister);call<void,void*,int>(tools,14,&v,7);v.registered=true;}
        }
        call<void,void*>(tools,17,&v);
        auto drawn=InterlockedCompareExchange64(&v.firstDrawMicros,0,0);
        if(drawn&&!v.drawReported){v.drawReported=true;drawCount++;if(drawCount<=8)log("first draw command=%d after=%.2f ms",s.command,(double(drawn)/1000000.0-s.born)*1000.0);}
    }
    for(int i=0;i<32;i++){
        const auto &s=pool.shots[i];auto &flame=flames[i];
        auto binding=bindings.find(s.command);auto spec=weaponSpec(s.item?s.item:18);
        if(!s.active||s.stopped||!spec||!spec->rocket()||(binding&&binding->seen&&time-binding->boundAt>=.05)){
            removeVisual(flame);continue;
        }
        prepareFlame();if(!flameModel)continue;
        flame.sprite=true;flame.clientTools=tools;flame.model=flameModel;flame.spriteScale=flameScale;
        float fade=binding&&binding->seen?clamp(1.f-float(time-binding->boundAt)/.05f,0,1):1.f;
        flame.spriteColor.SetColor(255,255,255,int(255*fade));
        flame.transform(s.position-s.velocity.unit()*9.f,{});flame.visible=true;
        if(!flame.registered){call<void,void*,int>(tools,14,&flame,8);flame.registered=true;}
        call<void,void*>(tools,17,&flame);
    }
    AttachmentTarget targets[64]{};
    uint32_t owner=entityHandle(player);
    for(int i=0;i<64;i++){
        auto &b=bindings.entries[i];if(!b.command)continue;
        if(time>b.expires){b={};continue;}
        void *entity=byHandle(b.reference);
        if(!entity){if(b.seen)b={};continue;}
        if(!projectileOwned(entity,owner)){b={};continue;}
        auto flight=motion.find(b.command);const auto spec=weaponSpec(flight?flight->item:18);
        if(!spec||!prepareModel(*spec))continue;
        auto renderable=call<void*>(entity,5);
        if(!method(renderable,37))continue;
        if(!b.seen){b.seen=true;b.boundAt=time;boundCount++;if(boundCount<=12)log("bound command=%d client_handle=%u index=%d",b.command,b.reference,networkIndex(b.reference));}
        if(!b.replacementDrawn)continue;
        InterlockedExchange(&hidden[networkIndex(b.reference)],1);
        InterlockedExchange64(&hiddenRenderables[i],reinterpret_cast<LONG64>(renderable));
        bindOrigin(entity);
        if(spec->rocket()&&!attachmentHook.slot)attachmentHook.install(renderable,36,reinterpret_cast<void*>(&rocketAttachment));
        if(auto s=pool.find(b.command))targets[i]={renderable,s->position,entity,b.reference};
        else if(auto f=motion.find(b.command))if(f->staticHit||f->explosionReceived)targets[i]={renderable,f->impact,entity,b.reference};
    }
    AcquireSRWLockExclusive(&attachmentLock);
    memcpy(attachmentTargets,targets,sizeof(targets));
    ReleaseSRWLockExclusive(&attachmentLock);
}
static void frame(void *self,int stage) {
    if(stage==0){
        if(!allowedServer()){
            if(pool.epoch||userId){log("prediction stopped; left lab");reset();}helloAt=-10;
        }else{
            double time=now();
            void *player=localPlayer();
            if(!player||read<uint8_t>(player,offsets.life,1)!=0)wasAlive=false;
            if(time-helloAt>3){char text[64];int protocol=serverProtocol?serverProtocol:4-(helloAttempts%3);snprintf(text,sizeof(text),"rp_hello %d %d",protocol,enabled?1:0);call<void,const char*,bool>(engine,6,text,true);helloAt=time;helloAttempts++;}
            if(serverProtocol>=3&&time-clockAt>1){char text[64];snprintf(text,sizeof(text),"rp_clock %d",serverClock.request(time));call<void,const char*,bool>(engine,6,text,true);clockAt=time;}
            if(pool.epoch&&time-heartbeat>10){log("server handshake expired");reset();}
        }
    }
    if(stage==5&&allowedServer()&&pool.epoch){
        if(!offsets.ready)resolveOffsets();
        if(offsets.ready){prewarmProjectiles();updateVisuals();}
        localBlasts+=effects.flush(call<bool>(prediction,14),now(),[](const Explosion &e){
            return playExplosion(e.position,e.normal,e.weapon,e.definition,e.sound,e.particle,e.entity);
        });
    }
    bool visual=stage==5&&allowedServer()&&pool.epoch;
    if(visual)InterlockedExchange(&visualPhase,1);
    invoke<void,int>(frameHook.original,self,stage);
    if(visual)InterlockedExchange(&visualPhase,0);
}

static void draw(void *self,const void *state,const ModelInfo &info,Matrix *bones) {
    if(info.entity>0&&info.entity<2048&&InterlockedCompareExchange(&hidden[info.entity],0,0)!=0)return;
    auto address=reinterpret_cast<uintptr_t>(info.renderable),begin=reinterpret_cast<uintptr_t>(visuals);
    if(address>=begin&&address<begin+sizeof(visuals)&&(address-begin)%sizeof(Renderable)==0){
        auto &v=visuals[(address-begin)/sizeof(Renderable)];
        InterlockedCompareExchange64(&v.firstDrawMicros,LONG64(now()*1000000.0),0);
        StageScope phase(Stage::ModelDraw);
        invoke<void,const void*,const ModelInfo&,Matrix*>(drawHook.original,self,state,info,bones);return;
    }
    for(auto &entry:hiddenRenderables)if(InterlockedCompareExchange64(&entry,0,0)==reinterpret_cast<LONG64>(info.renderable)&&info.renderable)return;
    invoke<void,const void*,const ModelInfo&,Matrix*>(drawHook.original,self,state,info,bones);
}

static bool insecureFlag() {
    int count=0;LPWSTR *args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    bool found=false;for(int i=1;i<count;i++)if(wcscmp(args[i],L"-insecure")==0)found=true;
    LocalFree(args);return found;
}
static Factory moduleFactory(const wchar_t *module) {
    return exportedFunction<Factory>(GetModuleHandleW(module),"CreateInterface");
}

class Plugin final:public PluginCallbacks {
public:
    bool Load(Factory engineFactory,Factory) override {
        if(loaded)return false;
        engineMsg=exportedFunction<decltype(engineMsg)>(GetModuleHandleW(L"tier0.dll"),"Msg");
        if(!insecureFlag()){log("not loaded: start TF2 with -insecure");return false;}
        auto getClient=moduleFactory(L"client.dll");if(!getClient||!engineFactory){log("not loaded: TF2 client interfaces unavailable");return false;}
        engine=engineFactory("VEngineClient014",nullptr);if(!engine)engine=engineFactory("VEngineClient013",nullptr);
        client=getClient("VClient017",nullptr);prediction=getClient("VClientPrediction001",nullptr);
        entities=getClient("VClientEntityList003",nullptr);tools=getClient("VCLIENTTOOLS001",nullptr);
        models=engineFactory("VEngineModel016",nullptr);traces=engineFactory("EngineTraceClient003",nullptr);
        modelInfo=static_cast<IVModelInfo*>(engineFactory(VMODELINFO_CLIENT_INTERFACE_VERSION,nullptr));
        cvars=engineFactory("VEngineCvar004",nullptr);
        if(!cvars){auto factory=moduleFactory(L"vstdlib.dll");if(factory)cvars=factory("VEngineCvar004",nullptr);}
        if(!modelInfo||!method(engine,104)||!method(client,36)||!method(prediction,18)||!method(entities,3)||!method(tools,17)||!method(models,19)||!method(traces,4)||!method(cvars,10)){
            log("not loaded: unsupported engine/client interfaces");return false;
        }
        if(call<int>(engine,104)!=440){log("not loaded: expected TF2 app 440");return false;}
        char path[MAX_PATH];const char *dir=call<const char*>(engine,35);
        if(dir&&readable(dir,1)){snprintf(path,sizeof(path),"%s/logs",dir);CreateDirectoryA(path,nullptr);snprintf(path,sizeof(path),"%s/logs/projectile_prediction.log",dir);logFile=fopen(path,"w");}
        log("loading 0.4.1, game=%s",call<const char*>(engine,99));
        if(!resolveOffsets()){log("not loaded: receive tables do not match supported TF2 layout");if(logFile){fclose(logFile);logFile=nullptr;}return false;}
        if(!resolveExplosion()){log("not loaded: TF2 explosion interface unavailable");if(logFile){fclose(logFile);logFile=nullptr;}return false;}
        if(call<void*,const char*>(cvars,10,"cl_projectile_prediction")){log("not loaded: cl_projectile_prediction already exists");if(logFile){fclose(logFile);logFile=nullptr;}return false;}
        controlCommand.name="cl_projectile_prediction";controlCommand.help="local rocket prediction: 0 off, 1 on";
        controlCommand.dll=call<int>(cvars,5);controlCommand.callback=&setEnabled;
        call<void,void*>(cvars,6,&controlCommand);controlCommand.registered=true;
        if(dir&&readable(dir,1))diagnostics.open(dir,recordException);
        if(!runHook.install(prediction,17,reinterpret_cast<void*>(&run))||!frameHook.install(client,35,reinterpret_cast<void*>(&frame))||
           !messageHook.install(client,36,reinterpret_cast<void*>(&userMessage))||!drawHook.install(models,19,reinterpret_cast<void*>(&draw))||
           !setupHook.install(prediction,18,reinterpret_cast<void*>(&setupMove))||!finishHook.install(prediction,19,reinterpret_cast<void*>(&finishMove))||!updateHook.install(prediction,3,reinterpret_cast<void*>(&predictionUpdate))){
            Unload();return false;
        }
        loaded=true;log("loaded; waiting for 185.137.93.189:27018");return true;
    }
    void Unload() override {
        if(loaded&&serverProtocol>=4&&allowedServer())call<void,const char*,bool>(engine,6,"rp_detach",true);
        runHook.restore();frameHook.restore();messageHook.restore();drawHook.restore();setupHook.restore();finishHook.restore();updateHook.restore();attachmentHook.restore();
        for(auto &hook:originHooks)hook.restore();
        if(cvars&&controlCommand.registered){call<void,void*>(cvars,7,&controlCommand);controlCommand.registered=false;}
        if(tools&&models)reset();
        diagnostics.close();
        log("unloaded predicted=%llu confirmed=%llu terminal=%llu",(unsigned long long)predictedCount,(unsigned long long)ackCount,(unsigned long long)terminalCount);
        if(logFile){fclose(logFile);logFile=nullptr;}
        loaded=false;
    }
    void Pause() override {if(serverProtocol>=4&&allowedServer())call<void,const char*,bool>(engine,6,"rp_detach",true);paused=true;reset();}
    void UnPause() override {paused=false;helloAt=-10;}
    const char *GetPluginDescription() override {return "projectile prediction lab 0.4.1 by avxgroup (insecure only)";}
};
static Plugin plugin;
}

extern "C" __declspec(dllexport) void *CreateInterface(const char *name,int *code) {
    if(name&&strcmp(name,"ISERVERPLUGINCALLBACKS003")==0){if(code)*code=0;return &rp::plugin;}
    if(code)*code=1;return nullptr;
}

#if defined(RP_RENDER_TEST)
extern "C" __declspec(dllexport) IClientRenderable *rp_test_renderable() {return &rp::visuals[0];}
#endif
