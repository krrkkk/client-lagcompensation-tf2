#include "../client/abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void DefaultRenderBoundsWorldspace(IClientRenderable *renderable,Vector &lo,Vector &hi) {
    Vector mins,maxs;renderable->GetRenderBounds(mins,maxs);
    const auto &world=renderable->RenderableToWorldTransform();
    for(int axis=0;axis<3;axis++){
        lo[axis]=hi[axis]=world[axis][3];
        for(int local=0;local<3;local++){
            float a=world[axis][local]*mins[local],b=world[axis][local]*maxs[local];
            lo[axis]+=a<b?a:b;hi[axis]+=a>b?a:b;
        }
    }
}

class AttachmentProbe final:public CDefaultClientRenderable {
    Vector position{};QAngle angles{};matrix3x4_t world{};
public:
    const Vector &GetRenderOrigin() override {return position;}
    const QAngle &GetRenderAngles() override {return angles;}
    const matrix3x4_t &RenderableToWorldTransform() override {return world;}
    bool ShouldDraw() override {return true;}
    bool IsTransparent() override {return false;}
    void GetRenderBounds(Vector &lo,Vector &hi) override {lo.Init();hi.Init();}
    void GetRenderBoundsWorldspace(Vector &lo,Vector &hi) override {GetRenderBounds(lo,hi);}
    bool GetAttachment(int number,matrix3x4_t &out) override {out[0][3]=float(number+100);return true;}
    bool GetAttachment(int,Vector&,QAngle&) override {return false;}
};

static bool drewSprite=false;
static bool spriteCall(void*,IClientRenderable *renderable,float scale,float frame,int mode,int fx,const Color &color,float radius,int *visibility) {
    assert(renderable&&renderable->IsTransparent()&&scale==.125f&&frame==0&&mode==5&&fx==0&&radius==0);
    assert(color.r()==255&&color.g()==240&&color.b()==210&&color.a()==200);
    assert(visibility&&*visibility==0);*visibility=1;drewSprite=true;return true;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *file=fopen(argv[1],"rb");if(!file)return 3;
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    if(size<long(sizeof(studiohdr_t))||size>1024*1024)return 4;
    auto data=static_cast<unsigned char*>(malloc(size));assert(data);
    assert(fread(data,1,size,file)==size_t(size));fclose(file);
    auto header=reinterpret_cast<studiohdr_t*>(data);
    assert(header->id==0x54534449&&header->numbones==1&&header->boneindex>=0&&header->boneindex+int(sizeof(mstudiobone_t))<=size);
    auto bone=header->pBone(0);
    assert(fabs(bone->quat.x-.7071067f)<.0001&&fabs(bone->quat.w-.7071067f)<.0001);
    rp::Matrix bind;memcpy(&bind,&bone->poseToBone,sizeof(bind));
    rp::ModelPose pose;pose.count=1;pose.bones[0]=rp::inverseRigid(bind);
    rp::Renderable model;model.pose=&pose;model.transform({30,40,50},{0,90,0});
    IClientRenderable *native=&model;matrix3x4_t output[1];
    assert(native->SetupBones(output,1,0,0));
    assert(fabs(output[0][0][1])<.0001&&fabs(output[0][1][1])<.0001&&fabs(output[0][2][1]-1)<.0001);
    assert(fabs(output[0][0][2]-1)<.0001&&fabs(output[0][1][2])<.0001&&fabs(output[0][2][2])<.0001);
    assert(fabs(output[0][0][3]-30)<.0001&&fabs(output[0][1][3]-40)<.0001&&fabs(output[0][2][3]-50)<.0001);
    pose.alignment=rp::projectileAlignment();
    assert(native->SetupBones(output,1,0,0));
    rp::Matrix posed;memcpy(&posed,&output[0],sizeof(posed));
    rp::Matrix skin=rp::multiply(posed,bind);
    assert(fabs(skin.m[0][0]+1)<.0001&&fabs(skin.m[1][0])<.0001&&fabs(skin.m[2][2]-1)<.0001);
    model.transform({30,40,50},{90,0,0});
    assert(native->SetupBones(output,1,0,0));memcpy(&posed,&output[0],sizeof(posed));skin=rp::multiply(posed,bind);
    assert(fabs(skin.m[0][1])<.0001&&fabs(skin.m[1][1])<.0001&&fabs(skin.m[2][1]-1)<.0001);
    AttachmentProbe probe;IClientRenderable *attached=&probe;rp::Matrix attachment{};
    assert((rp::call<bool,int,rp::Matrix&>(attached,36,7,attachment)));
    assert(attachment.m[0][3]==107);
    Vector lo,hi;model.GetRenderBoundsWorldspace(lo,hi);
    assert(lo.IsValid()&&hi.IsValid()&&hi.x>lo.x&&hi.y>lo.y&&hi.z>lo.z);
    rp::Vec mins{-18.6f,-3.8f,-3.8f},maxs{18.6f,3.8f,3.8f};pose.mins=mins;pose.maxs=maxs;
    model.transform({30,40,50},{27,143,0});model.GetRenderBoundsWorldspace(lo,hi);
    auto combined=rp::multiply(model.matrix,pose.alignment);
    for(int corner=0;corner<8;corner++){
        float local[3]={corner&1?maxs.x:mins.x,corner&2?maxs.y:mins.y,corner&4?maxs.z:mins.z};
        for(int axis=0;axis<3;axis++){
            float p=combined.m[axis][3];for(int j=0;j<3;j++)p+=combined.m[axis][j]*local[j];
            assert(p>=lo[axis]-.001f&&p<=hi[axis]+.001f);
        }
    }
    void *table[20]{};table[19]=reinterpret_cast<void*>(&spriteCall);void **fakeTools=table;
    rp::Renderable sprite;sprite.sprite=true;sprite.clientTools=&fakeTools;sprite.visible=true;sprite.model=data;
    sprite.spriteScale=.125f;sprite.spriteColor.SetColor(255,240,210,200);
    assert(sprite.GetModelInstance()==0xffff);
    assert(sprite.DrawModel(1)==1&&drewSprite&&sprite.spriteVisibility==1);
    free(data);
    puts("stock rocket MDL: native bind orientation and world transform passed; Microsoft attachment slot 36 verified");
}
