#pragma once
#include "core.h"
#include <stddef.h>
#include <basehandle.h>
#include <iclientrenderable.h>
#include <tier1/bitbuf.h>
#include <icliententity.h>
#include <cmodel.h>
#include <engine/ivmodelinfo.h>
#include <studio.h>
#include <Color.h>

#if !defined(_MSC_VER) || !defined(_CPPRTTI)
#error projectile client requires Microsoft C++ ABI with RTTI enabled
#endif

namespace rp {
using Factory=void*(*)(const char*,int*);
template<class R,class... A> R call(void *o,int i,A... a) {return reinterpret_cast<R(*)(void*,A...)>((*reinterpret_cast<void***>(o))[i])(o,a...);}
template<class R,class... A> R invoke(void *fn,void *o,A... a) {return reinterpret_cast<R(*)(void*,A...)>(fn)(o,a...);}
struct alignas(16) AlignedVec {float x,y,z,w;};
struct Ray {
    AlignedVec start,delta,offset,extents;
    bool isRay,isSwept;
    Ray(Vec a,Vec b):start{a.x,a.y,a.z,0},delta{b.x-a.x,b.y-a.y,b.z-a.z,0},offset{},extents{},isRay(true),isSwept(true){}
    Ray(Vec a,Vec b,float radius):Ray(a,b){extents={radius,radius,radius,0};isRay=radius==0;}
};
struct Plane {Vec normal;float distance;uint8_t type,sign,pad[2];};
struct Trace {
    Vec start,end;
    Plane plane;
    float fraction;
    int contents;
    uint16_t dispFlags;
    bool allSolid,startSolid;
    float fractionLeftSolid;
    struct {const char *name;short props;uint16_t flags;} surface;
    int hitgroup;short bone;uint16_t worldSurface;
    void *entity;
    int hitbox;
};
struct RecvTable;
struct RecvProp {
    const char *name;int type,flags,stringSize;bool inArray;
    const void *extra;RecvProp *array;void *arrayLength,*proxy,*tableProxy;
    RecvTable *table;int offset,stride,count;const char *parent;
};
struct RecvTable {RecvProp *props;int count;void *decoder;const char *name;bool initialized,inList;};
struct ClientClass {void *create,*event;const char *name;RecvTable *table;ClientClass *next;int id;};
struct UserCmd {
    void *vtable;int command,tick;Vec view;
    float forward,side,up;int buttons;
    uint8_t impulse,pad[3];int weapon,subtype,seed;short mousex,mousey;bool predicted;
};
struct BitReader {const uint8_t *data;int bytes,bits,current;bool overflow,assertOverflow;const char *debug;};
struct MoveData {
    uint8_t flags,pad[3];uint32_t player;
    int impulse;Vec view,absView;int buttons,oldButtons;
    float forward,oldForward,side,up,maxSpeed,clientMaxSpeed;
    Vec velocity,angles,oldAngles;float stepHeight;Vec wishVelocity,jumpVelocity;
    Vec constraintCenter;float constraintRadius,constraintWidth,constraintFactor;Vec origin;
};
struct ModelInfo {
    Vec origin,angles;void *renderable;const void *model;
    const Matrix *transform,*lightingOffset;const Vec *lightingOrigin;
    int flags,entity,skin,body,hitboxset;uint16_t instance;
};
static_assert(sizeof(RecvProp)==96);
static_assert(offsetof(RecvProp,offset)==72);
static_assert(offsetof(UserCmd,command)==8&&offsetof(UserCmd,buttons)==40);
static_assert(sizeof(Trace)==104);
static_assert(sizeof(Ray)==80);
static_assert(sizeof(Trace)==sizeof(trace_t));
static_assert(sizeof(Ray)==sizeof(Ray_t));
static_assert(sizeof(ModelInfo)==sizeof(ModelRenderInfo_t));
static_assert(offsetof(ModelInfo,entity)==68);
static_assert(offsetof(MoveData,velocity)==68&&offsetof(MoveData,origin)==156&&sizeof(MoveData)==168);

struct Unknown final:public IClientUnknown {
    IClientRenderable *renderable=nullptr;
    CBaseHandle ref;
    void SetRefEHandle(const CBaseHandle &r) override {ref=r;}
    const CBaseHandle &GetRefEHandle() const override {return ref;}
    ICollideable *GetCollideable() override {return nullptr;}
    IClientNetworkable *GetClientNetworkable() override {return nullptr;}
    IClientRenderable *GetClientRenderable() override {return renderable;}
    IClientEntity *GetIClientEntity() override {return nullptr;}
    C_BaseEntity *GetBaseEntity() override {return nullptr;}
    IClientThinkable *GetClientThinkable() override {return nullptr;}
};

struct ModelPose {
    const void *model=nullptr;
    int count=0;
    int skins=1;
    Matrix alignment={{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    Matrix bones[128]{};
    Vec mins{-16,-8,-8},maxs{16,8,8};
};

struct Renderable final:public IClientRenderable {
    Unknown unknown;
    Vec origin{},rotation{};
    Matrix matrix{};
    const void *model=nullptr;
    const ModelPose *pose=nullptr;
    void *modelRender=nullptr,*clientTools=nullptr;
    bool sprite=false;
    float spriteScale=.1f;
    Color spriteColor{255,255,255,255};
    int spriteVisibility=0;
    uint16_t renderHandle=0xffff,instance=0xffff;
    bool visible=false,registered=false;
    volatile long long firstDrawMicros=0;
    bool drawReported=false;
    double lastSmoke=0;
    int command=0;
    int skin=0;
    Renderable() {unknown.renderable=this;}
    void transform(Vec p,Vec a) {
        origin=p;rotation=a;Vec f,r,u;basis(a,f,r,u);
        matrix={{{f.x,-r.x,u.x,p.x},{f.y,-r.y,u.y,p.y},{f.z,-r.z,u.z,p.z}}};
    }
    IClientUnknown *GetIClientUnknown() override {return &unknown;}
    const Vector &GetRenderOrigin() override {return reinterpret_cast<const Vector&>(origin);}
    const QAngle &GetRenderAngles() override {return reinterpret_cast<const QAngle&>(rotation);}
    bool ShouldDraw() override {return visible&&model;}
    bool IsTransparent() override {return sprite;}
    bool UsesPowerOfTwoFrameBufferTexture() override {return false;}
    bool UsesFullFrameBufferTexture() override {return false;}
    ClientShadowHandle_t GetShadowHandle() const override {return CLIENTSHADOW_INVALID_HANDLE;}
    ClientRenderHandle_t &RenderHandle() override {return renderHandle;}
    const model_t *GetModel() const override {return static_cast<const model_t*>(model);}
    int DrawModel(int flags) override {
        if(!ShouldDraw())return 0;
        if(sprite){
            if(!clientTools)return 0;
            return call<bool,IClientRenderable*,float,float,int,int,const Color&,float,int*>(clientTools,19,this,spriteScale,0.f,5,0,spriteColor,0.f,&spriteVisibility)?1:0;
        }
        return call<int,int,void*,uint16_t,int,const void*,const Vec&,const Vec&,int,int,int,const Matrix*,const Matrix*>(modelRender,0,flags,this,instance,-1,model,origin,rotation,skin,0,0,&matrix,nullptr);
    }
    int GetBody() override {return 0;}
    void ComputeFxBlend() override {}
    int GetFxBlend() override {return 255;}
    void GetColorModulation(float *c) override {c[0]=c[1]=c[2]=1;}
    bool LODTest() override {return true;}
    bool SetupBones(matrix3x4_t *out,int count,int,float) override {
        if(!out||!pose||pose->count<1||pose->count>128||count<pose->count)return false;
        Matrix aligned=multiply(matrix,pose->alignment);
        for(int i=0;i<pose->count;i++){
            Matrix bone=multiply(aligned,pose->bones[i]);memcpy(&out[i],&bone,sizeof(bone));
        }
        return true;
    }
    void SetupWeights(const matrix3x4_t*,int,float*,float*) override {}
    void DoAnimationEvents() override {}
    IPVSNotify *GetPVSNotifyInterface() override {return nullptr;}
    void GetRenderBounds(Vector &mins,Vector &maxs) override {
        Vec lo=pose?pose->mins:Vec{-16,-8,-8},hi=pose?pose->maxs:Vec{16,8,8};
        if(sprite){lo={-16,-16,-16};hi={16,16,16};}
        else if(pose)transformBounds(pose->alignment,lo,hi,lo,hi);
        mins.Init(lo.x,lo.y,lo.z);maxs.Init(hi.x,hi.y,hi.z);
    }
    void GetRenderBoundsWorldspace(Vector &mins,Vector &maxs) override {
        Vector lo,hi;GetRenderBounds(lo,hi);Vec a,b;
        transformBounds(matrix,{lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z},a,b);
        mins.Init(a.x,a.y,a.z);maxs.Init(b.x,b.y,b.z);
    }
    void GetShadowRenderBounds(Vector &mins,Vector &maxs,ShadowType_t) override {GetRenderBounds(mins,maxs);}
    bool ShouldReceiveProjectedTextures(int) override {return false;}
    bool GetShadowCastDistance(float*,ShadowType_t) const override {return false;}
    bool GetShadowCastDirection(Vector*,ShadowType_t) const override {return false;}
    bool IsShadowDirty() override {return false;}
    void MarkShadowDirty(bool) override {}
    IClientRenderable *GetShadowParent() override {return nullptr;}
    IClientRenderable *FirstShadowChild() override {return nullptr;}
    IClientRenderable *NextShadowPeer() override {return nullptr;}
    ShadowType_t ShadowCastType() override {return SHADOWS_NONE;}
    void CreateModelInstance() override {if(!sprite&&instance==0xffff&&modelRender)instance=call<uint16_t,void*,void*>(modelRender,3,this,nullptr);}
    ModelInstanceHandle_t GetModelInstance() override {CreateModelInstance();return instance;}
    const matrix3x4_t &RenderableToWorldTransform() override {return reinterpret_cast<const matrix3x4_t&>(matrix);}
    int LookupAttachment(const char*) override {return 0;}
    bool GetAttachment(int,Vector&,QAngle&) override {return false;}
    bool GetAttachment(int,matrix3x4_t&) override {return false;}
    float *GetRenderClipPlane() override {return nullptr;}
    int GetSkin() override {return skin;}
    bool IsTwoPass() override {return false;}
    void OnThreadedDrawSetup() override {}
    bool UsesFlexDelayedWeights() override {return false;}
    void RecordToolMessage() override {}
    bool IgnoresZBuffer() const override {return false;}
};
static_assert(sizeof(Vector)==sizeof(Vec)&&alignof(Vector)==alignof(Vec));
static_assert(sizeof(QAngle)==sizeof(Vec)&&alignof(QAngle)==alignof(Vec));
static_assert(sizeof(matrix3x4_t)==sizeof(Matrix)&&alignof(matrix3x4_t)==alignof(Matrix));

struct PluginCallbacks {
    virtual bool Load(Factory,Factory)=0;
    virtual void Unload()=0;
    virtual void Pause()=0;
    virtual void UnPause()=0;
    virtual const char *GetPluginDescription()=0;
    virtual void LevelInit(const char*) {}
    virtual void ServerActivate(void*,int,int) {}
    virtual void GameFrame(bool) {}
    virtual void LevelShutdown() {}
    virtual void ClientActive(void*) {}
    virtual void ClientDisconnect(void*) {}
    virtual void ClientPutInServer(void*,const char*) {}
    virtual void SetCommandClient(int) {}
    virtual void ClientSettingsChanged(void*) {}
    virtual int ClientConnect(bool*,void*,const char*,const char*,char*,int) {return 0;}
    virtual int ClientCommand(void*,const void*) {return 0;}
    virtual int NetworkIDValidated(const char*,const char*) {return 0;}
    virtual void OnQueryCvarValueFinished(int,void*,int,const char*,const char*) {}
    virtual void OnEdictAllocated(void*) {}
    virtual void OnEdictFreed(const void*) {}
};
}
