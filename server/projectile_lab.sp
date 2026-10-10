#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#include <commandline>
#include "weapon_profiles.inc"

public Plugin myinfo = {name="projectile lab", author="avxgroup", description="opt-in projectile prediction", version="0.4.1"};

bool g_Enabled[MAXPLAYERS+1];
int g_Protocol[MAXPLAYERS+1];
int g_Command[MAXPLAYERS+1];
int g_Tick[MAXPLAYERS+1];
int g_Pending[MAXPLAYERS+1];
int g_ProjectileCommand[2049];
int g_ProjectileOwner[2049];
int g_ProjectileRef[2049];
bool g_ProjectileSelf[2049];
int g_ProjectileKind[2049],g_WeaponFireCommand[2049],g_WeaponFireRef[2049];
int g_BeforeWeapon[MAXPLAYERS+1];
float g_BeforeFireTime[MAXPLAYERS+1];
float g_BeforeRegen[MAXPLAYERS+1],g_BeforeNextAttack[MAXPLAYERS+1];
ArrayList g_Tracked;
float g_NextState;
int g_TraceProjectile;
int g_TouchEntity;
bool g_ForwardingExplosion;
int g_BeforeRocket[MAXPLAYERS+1],g_BeforeHealth[MAXPLAYERS+1];
float g_BeforeVelocity[MAXPLAYERS+1][3];
int g_Explosions,g_SelfBlasts;
float g_HelloTime[MAXPLAYERS+1];
float g_ClockTime[MAXPLAYERS+1];
int g_Epoch;
int g_Acks, g_Advanced, g_Blocked, g_Rejected, g_Terminals;
ConVar g_MaxAdvance, g_AdvanceEnabled;
Handle g_GetVelocity;
#include "history.inc"

public void OnPluginStart()
{
    char line[2048]; GetCommandLine(line,sizeof(line));
    char padded[2060]; Format(padded,sizeof(padded)," %s ",line);
    bool correctPort=FindConVar("hostport").IntValue==27018;
#if defined RP_TEST
    correctPort=correctPort||FindConVar("hostport").IntValue==27016;
#endif
    if (!correctPort || StrContains(padded," -insecure ")==-1)
        SetFailState("projectile lab requires port 27018 and -insecure");
    GameData sdk=new GameData("sdktools.games");
    if(sdk==null)SetFailState("cannot load sdktools.games for projectile velocity");
    StartPrepSDKCall(SDKCall_Entity);
    bool velocityReady=PrepSDKCall_SetFromConf(sdk,SDKConf_Virtual,"GetVelocity");
    delete sdk;
    if(!velocityReady)SetFailState("CBaseEntity::GetVelocity is missing from SDKTools gamedata");
    PrepSDKCall_AddParameter(SDKType_Vector,SDKPass_Pointer,0,VENCODE_FLAG_COPYBACK);
    PrepSDKCall_AddParameter(SDKType_Vector,SDKPass_Pointer,0,VENCODE_FLAG_COPYBACK);
    g_GetVelocity=EndPrepSDKCall();
    if(g_GetVelocity==null)SetFailState("cannot prepare native projectile velocity reader");
    g_MaxAdvance=CreateConVar("sm_rp_max_advance","0.100","maximum free-flight catch-up in seconds",FCVAR_NONE,true,0.0,true,0.150);
    g_AdvanceEnabled=CreateConVar("sm_rp_advance","1","enable bounded free-flight catch-up",FCVAR_NONE,true,0.0,true,1.0);
    RegConsoleCmd("rp_hello",Hello);
    RegConsoleCmd("rp_clock",Clock);
    RegConsoleCmd("rp_detach",Detach);
    g_Tracked=new ArrayList();
    HookEvent("player_spawn",HistorySpawn);
    RegAdminCmd("sm_rp_status",Status,ADMFLAG_ROOT);
#if defined RP_TEST
    RegAdminCmd("sm_rp_check",NativeCheck,ADMFLAG_ROOT);
    RegAdminCmd("sm_rp_firecheck",NativeFireCheck,ADMFLAG_ROOT);
#endif
    AddTempEntHook("TFExplosion",OnExplosion);
    for(int i=1;i<=MaxClients;i++) {ResetClient(i);if(IsClientInGame(i))HookDamage(i);}
    NewEpoch();
}

void NewEpoch()
{
    g_Epoch=GetURandomInt() & 0x3fffffff;
    if(g_Epoch==0) g_Epoch=1;
    for(int i=0;i<sizeof(g_ProjectileCommand);i++) {g_ProjectileCommand[i]=0;g_ProjectileOwner[i]=0;g_ProjectileRef[i]=0;g_ProjectileSelf[i]=false;g_ProjectileKind[i]=0;g_WeaponFireCommand[i]=0;g_WeaponFireRef[i]=0;}
    if(g_Tracked!=null)g_Tracked.Clear();g_NextState=0.0;HistoryReset();
    g_TouchEntity=0;g_ForwardingExplosion=false;
    for(int i=1;i<=MaxClients;i++) ResetClient(i);
}

public void OnMapStart() {NewEpoch();}
public void OnClientPutInServer(int client) {ResetClient(client);HookDamage(client);}
public void OnClientDisconnect(int client) {ResetClient(client);}

void ResetClient(int client)
{
    g_Enabled[client]=false;g_Protocol[client]=0;g_Command[client]=0;g_Tick[client]=0;g_BeforeRocket[client]=INVALID_ENT_REFERENCE;
    g_Pending[client]=INVALID_ENT_REFERENCE;g_HelloTime[client]=-10.0;g_ClockTime[client]=-10.0;
}

void Message(int client,const char[] text,bool reliable=true)
{
    if(client<1||client>MaxClients||!IsClientInGame(client)||IsFakeClient(client)) return;
    Handle msg=StartMessageOne("KeyHintText",client,USERMSG_BLOCKHOOKS|(reliable?USERMSG_RELIABLE:0));
    if(msg==null) return;
    BfWriteByte(msg,1); BfWriteString(msg,text); EndMessage();
}

public Action Hello(int client,int args)
{
    if(client<1||!IsClientInGame(client)||IsFakeClient(client)) return Plugin_Handled;
    char version[16]; GetCmdArg(1,version,sizeof(version));
    int protocol;bool mode=true;
    if(args==1&&StrEqual(version,"1"))protocol=1;
    else if(args==2&&(StrEqual(version,"2")||StrEqual(version,"3")||StrEqual(version,"4"))){
        char setting[8];GetCmdArg(2,setting,sizeof(setting));
        if(!StrEqual(setting,"0")&&!StrEqual(setting,"1"))return Plugin_Handled;
        protocol=StringToInt(version);mode=StrEqual(setting,"1");
    }else return Plugin_Handled;
    if(g_Protocol[client]==protocol&&g_Enabled[client]==mode&&GetEngineTime()-g_HelloTime[client]<1.0)return Plugin_Handled;
    g_HelloTime[client]=GetEngineTime();g_Enabled[client]=mode;g_Protocol[client]=protocol;
    char msg[240];
    if(protocol==1)Format(msg,sizeof(msg),"RP1 H %d %d %.8f %.3f",g_Epoch,GetClientUserId(client),GetTickInterval(),g_MaxAdvance.FloatValue);
    else Format(msg,sizeof(msg),"RP%d H %d %d %.8f %.3f %.4f %.4f %.4f %d",protocol,g_Epoch,GetClientUserId(client),GetTickInterval(),g_MaxAdvance.FloatValue,ForceScale(false),ForceScale(true),FindConVar("tf_damagescale_self_soldier").FloatValue,mode);
    Message(client,msg);
    return Plugin_Handled;
}

public Action Clock(int client,int args)
{
    if(client<1||args!=1||!IsClientInGame(client)||IsFakeClient(client)||g_Protocol[client]<3||GetEngineTime()-g_ClockTime[client]<0.25)return Plugin_Handled;
    char text[24];GetCmdArg(1,text,sizeof(text));int nonce=StringToInt(text);
    if(nonce<1||nonce>1000000)return Plugin_Handled;
    g_ClockTime[client]=GetEngineTime();char reply[96];Format(reply,sizeof(reply),"RP%d C %d %d %d",g_Protocol[client],g_Epoch,nonce,GetGameTickCount());Message(client,reply);
    return Plugin_Handled;
}

public Action Detach(int client,int args)
{
    if(client>0&&client<=MaxClients){g_Enabled[client]=false;g_Protocol[client]=0;}
    return Plugin_Handled;
}

public Action Status(int client,int args)
{
    int enabled;
    for(int i=1;i<=MaxClients;i++) if(g_Enabled[i]&&IsClientInGame(i)) enabled++;
    ReplyToCommand(client,"RP4 version=0.4.1 epoch=%d clients=%d ack=%d advanced=%d blocked=%d rejected=%d terminal=%d explosions=%d self=%d cap=%.3f enabled=%d",g_Epoch,enabled,g_Acks,g_Advanced,g_Blocked,g_Rejected,g_Terminals,g_Explosions,g_SelfBlasts,g_MaxAdvance.FloatValue,g_AdvanceEnabled.BoolValue);
    return Plugin_Handled;
}

public Action OnPlayerRunCmd(int client,int &buttons,int &impulse,float vel[3],float angles[3],int &weapon,int &subtype,int &cmdnum,int &tickcount,int &seed,int mouse[2])
{
#if defined RP_TEST
    bool controlled=TestControl(client,buttons,vel,angles,cmdnum,tickcount);
#endif
    g_Command[client]=cmdnum;g_Tick[client]=tickcount;
    int active=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    g_BeforeWeapon[client]=active>MaxClients&&HasEntProp(active,Prop_Send,"m_flLastFireTime")?EntIndexToEntRef(active):INVALID_ENT_REFERENCE;
    g_BeforeFireTime[client]=g_BeforeWeapon[client]!=INVALID_ENT_REFERENCE?GetEntPropFloat(active,Prop_Send,"m_flLastFireTime"):0.0;
    g_BeforeRegen[client]=g_BeforeWeapon[client]!=INVALID_ENT_REFERENCE?GetEntPropFloat(active,Prop_Send,"m_flEffectBarRegenTime"):0.0;
    g_BeforeNextAttack[client]=g_BeforeWeapon[client]!=INVALID_ENT_REFERENCE?GetEntPropFloat(active,Prop_Send,"m_flNextPrimaryAttack"):0.0;
#if defined RP_TEST
    if(controlled)return Plugin_Changed;
#endif
    return Plugin_Continue;
}

public void OnEntityCreated(int entity,const char[] classname)
{
    if(entity>MaxClients&&entity<sizeof(g_ProjectileCommand)&&StrEqual(classname,"tf_projectile_rocket"))
    {
        SDKHook(entity,SDKHook_SpawnPost,RocketSpawned);
        SDKHook(entity,SDKHook_Touch,RocketTouch);
        SDKHook(entity,SDKHook_TouchPost,RocketTouched);
    }
    else if(entity>MaxClients&&entity<sizeof(g_ProjectileCommand)&&StrContains(classname,"tf_projectile_")==0){
        SDKHook(entity,SDKHook_SpawnPost,OtherSpawned);SDKHook(entity,SDKHook_Touch,RocketTouch);SDKHook(entity,SDKHook_TouchPost,RocketTouched);
    }
}

bool ProjectileVelocity(int entity,float velocity[3])
{
    if(g_GetVelocity==null||!IsValidEntity(entity))return false;
    float angular[3];SDKCall(g_GetVelocity,entity,velocity,angular);
    for(int axis=0;axis<3;axis++)if(!(velocity[axis]>=-15000.0&&velocity[axis]<=15000.0))return false;
    return GetVectorLength(velocity)<=15000.0;
}

public void OtherSpawned(int entity) {RequestFrame(DeferredProjectile,EntIndexToEntRef(entity));}

int ProjectileOwner(int entity)
{
    int owner=HasEntProp(entity,Prop_Send,"m_hOwnerEntity")?GetEntPropEnt(entity,Prop_Send,"m_hOwnerEntity"):-1;
    if(owner<1||owner>MaxClients)owner=HasEntProp(entity,Prop_Send,"m_hThrower")?GetEntPropEnt(entity,Prop_Send,"m_hThrower"):-1;
    return owner;
}

public void DeferredProjectile(any reference)
{
    int entity=EntRefToEntIndex(reference);if(entity<=MaxClients||!IsValidEntity(entity))return;
    if(g_ProjectileRef[entity]==reference&&g_ProjectileCommand[entity]>0)return;
    int owner=ProjectileOwner(entity);if(owner<1||owner>MaxClients||!IsClientInGame(owner)||!g_Enabled[owner]||g_Protocol[owner]<4)return;
    if(!HasEntProp(entity,Prop_Send,"m_hLauncher"))return;
    int weapon=GetEntPropEnt(entity,Prop_Send,"m_hLauncher");if(weapon<=MaxClients||!IsValidEntity(weapon)||!HasEntProp(weapon,Prop_Send,"m_iItemDefinitionIndex"))return;
    WeaponSpec spec;if(!WeaponProfile(GetEntProp(weapon,Prop_Send,"m_iItemDefinitionIndex"),spec)||spec.kind==1)return;
    int cmd=g_WeaponFireRef[weapon]==EntIndexToEntRef(weapon)?g_WeaponFireCommand[weapon]:0;if(cmd<=0)return;
    float origin[3],velocity[3];GetEntPropVector(entity,Prop_Send,"m_vecOrigin",origin);
    if(!ProjectileVelocity(entity,velocity))return;
#if defined RP_TEST
    TestVelocitySource(entity,spec.item,velocity);
#endif
    float gravity=spec.gravity;if(GetEntityMoveType(entity)==MOVETYPE_FLYGRAVITY)gravity=GetEntityGravity(entity);
    float age=0.0;if(!IsFakeClient(owner)&&g_AdvanceEnabled.BoolValue)age=FloatMin(g_MaxAdvance.FloatValue,GetClientAvgLatency(owner,NetFlow_Outgoing)*0.5);
#if defined RP_TEST
    if(GetClientSerial(owner)==g_TestActorSerial)age=g_TestAdvance;
#endif
    float advanced=AdvanceCurved(entity,owner,origin,velocity,gravity,spec.hull,age);
    if(advanced>0.0){
        if(HasEntProp(entity,Prop_Send,"m_flCreationTime")){float created=GetEntPropFloat(entity,Prop_Send,"m_flCreationTime");if(created>0.0)SetEntPropFloat(entity,Prop_Send,"m_flCreationTime",created-advanced);}
        if(HasEntProp(entity,Prop_Data,"m_flDetonateTime")){float detonate=GetEntPropFloat(entity,Prop_Data,"m_flDetonateTime");if(detonate>GetGameTime())SetEntPropFloat(entity,Prop_Data,"m_flDetonateTime",detonate-advanced);}
    }
    g_ProjectileCommand[entity]=cmd;g_ProjectileOwner[entity]=GetClientSerial(owner);g_ProjectileRef[entity]=reference;g_ProjectileKind[entity]=spec.kind;g_ProjectileSelf[entity]=false;
    g_Tracked.Push(reference);
#if defined RP_TEST
    LogMessage("RP_PROJECTILE item=%d command=%d reference=%d kind=%d",spec.item,cmd,reference,spec.kind);
    TestWatchFlight(entity,spec.item,origin,velocity);
#endif
    char text[250];Format(text,sizeof(text),"RP4 P %d %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d",g_Epoch,cmd,reference,spec.item,origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2],gravity,GetGameTickCount());Message(owner,text);g_Acks++;
}

public void RocketSpawned(int entity)
{
    int owner=GetEntPropEnt(entity,Prop_Send,"m_hOwnerEntity");
    if(owner<1||owner>MaxClients||!g_Enabled[owner]||!IsClientInGame(owner)||!IsPlayerAlive(owner)) return;
    if(g_Command[owner]<=0) return;
    g_Pending[owner]=EntIndexToEntRef(entity);
}

public void OnPlayerRunCmdPost(int client,int buttons,int impulse,const float vel[3],const float angles[3],int weapon,int subtype,int cmdnum,int tickcount,int seed,const int mouse[2])
{
    int fired=EntRefToEntIndex(g_BeforeWeapon[client]);
    if(fired>MaxClients&&IsValidEntity(fired)){
        bool attack=GetEntPropFloat(fired,Prop_Send,"m_flLastFireTime")>g_BeforeFireTime[client]+0.0001;
        WeaponSpec firingSpec;
        if(WeaponProfile(GetEntProp(fired,Prop_Send,"m_iItemDefinitionIndex"),firingSpec)&&(firingSpec.kind==14||firingSpec.kind==15))
            attack=(buttons&IN_ATTACK2)!=0&&GetEntPropFloat(fired,Prop_Send,"m_flNextPrimaryAttack")>g_BeforeNextAttack[client]+0.0001;
        else attack=attack||GetEntPropFloat(fired,Prop_Send,"m_flEffectBarRegenTime")>g_BeforeRegen[client]+0.0001;
        if(attack){g_WeaponFireCommand[fired]=cmdnum;g_WeaponFireRef[fired]=EntIndexToEntRef(fired);}
    }
    if(g_Pending[client]==INVALID_ENT_REFERENCE) return;
    int entity=EntRefToEntIndex(g_Pending[client]);g_Pending[client]=INVALID_ENT_REFERENCE;
    if(entity<=MaxClients||!IsValidEntity(entity)||!g_Enabled[client]) return;
    int launcher=GetEntPropEnt(entity,Prop_Send,"m_hLauncher");
    char classname[64];
    if(launcher<=MaxClients||!IsValidEntity(launcher)) {g_Rejected++;return;}
    GetEntityClassname(launcher,classname,sizeof(classname));
    int item=GetEntProp(launcher,Prop_Send,"m_iItemDefinitionIndex");
    WeaponSpec spec;
    if(!WeaponProfile(item,spec)||spec.kind!=1||StrContains(classname,"tf_weapon_rocketlauncher")!=0||(g_Protocol[client]<3&&item!=18&&item!=205)) {g_Rejected++;return;}
    float origin[3],velocity[3];
    GetEntPropVector(entity,Prop_Send,"m_vecOrigin",origin);
    if(!ProjectileVelocity(entity,velocity)){g_Rejected++;return;}
#if defined RP_TEST
    LogMessage("RP_SPAWN case=%d command=%d pos=%.3f %.3f %.3f velocity=%.3f %.3f %.3f",g_TestCase,cmdnum,origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2]);
#endif
    float speed=GetVectorLength(velocity);
    if(speed<100.0||speed>15000.0) {g_Rejected++;return;}
    int ageTicks=GetGameTickCount()-tickcount;
    float age=0.0;
    if(!IsFakeClient(client)&&g_AdvanceEnabled.BoolValue&&ageTicks>=-4&&ageTicks<=64)
    {
        float rtt=GetClientAvgLatency(client,NetFlow_Outgoing);
        if(rtt>0.0) age=FloatMin(g_MaxAdvance.FloatValue,rtt*0.5);
    }
    float advance=AdvanceRocket(entity,client,origin,velocity,age);
    int reference=EntIndexToEntRef(entity);
    g_ProjectileCommand[entity]=cmdnum;
    g_ProjectileOwner[entity]=GetClientSerial(client);
    g_ProjectileRef[entity]=reference;
    g_ProjectileSelf[entity]=false;
    g_ProjectileKind[entity]=1;
    char message[250];
    if(g_Protocol[client]>=3)Format(message,sizeof(message),"RP%d A %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.4f %d",g_Protocol[client],g_Epoch,cmdnum,reference,origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2],advance,GetGameTickCount());
    else Format(message,sizeof(message),"RP%d A %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.4f",g_Protocol[client],g_Epoch,cmdnum,reference,origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2],advance);
    Message(client,message);g_Acks++;
}

public void OnGameFrame()
{
#if defined RP_TEST
    TestFlightFrame();
#endif
    HistoryCapture();
    if(GetGameTime()<g_NextState||g_Tracked==null)return;g_NextState=GetGameTime()+0.05;
    for(int i=g_Tracked.Length-1;i>=0;i--){
        int reference=g_Tracked.Get(i),entity=EntRefToEntIndex(reference);
        if(entity<=MaxClients||!IsValidEntity(entity)||g_ProjectileRef[entity]!=reference){g_Tracked.Erase(i);continue;}
        int owner=GetClientFromSerial(g_ProjectileOwner[entity]);if(owner<1||g_Protocol[owner]<4)continue;
        float pos[3],velocity[3],angles[3];GetEntPropVector(entity,Prop_Send,"m_vecOrigin",pos);
        if(!ProjectileVelocity(entity,velocity))continue;
        GetEntPropVector(entity,Prop_Send,"m_angRotation",angles);
        char text[250];Format(text,sizeof(text),"RP4 S %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %d",g_Epoch,g_ProjectileCommand[entity],reference,pos[0],pos[1],pos[2],velocity[0],velocity[1],velocity[2],angles[0],angles[1],angles[2],GetGameTickCount());
        Message(owner,text,false);
    }
}

float FloatMin(float a,float b) {return a<b?a:b;}

float AdvanceCurved(int entity,int owner,float origin[3],float velocity[3],float gravity,float hull,float age)
{
    if(age<=0.0)return 0.0;
    float point[3],motion[3],end[3],mins[3],maxs[3];point=origin;motion=velocity;
    for(int axis=0;axis<3;axis++){mins[axis]=-hull;maxs[axis]=hull;}
    int steps=RoundToCeil(age/0.01);if(steps>16)steps=16;
    float dt=age/float(steps),accel=-FindConVar("sv_gravity").FloatValue*gravity,advanced;
    for(int step=0;step<steps;step++){
        for(int axis=0;axis<3;axis++)end[axis]=point[axis]+motion[axis]*dt;
        end[2]+=0.5*accel*dt*dt;
        g_TraceProjectile=entity;
        Handle hit=TR_TraceHullFilterEx(point,end,mins,maxs,MASK_SHOT,RocketFilter,owner);
        float fraction=TR_StartSolid(hit)?0.0:TR_GetFraction(hit);delete hit;g_TraceProjectile=0;
        float historical=HistoryFraction(point,end,hull,GetGameTime()-age+(float(step)+0.5)*dt,owner);
        if(historical<fraction)fraction=historical;
        if(fraction<1.0){
            float impact[3];for(int axis=0;axis<3;axis++)impact[axis]=point[axis]+(end[axis]-point[axis])*fraction;
            if(owner>0&&owner<=MaxClients&&IsClientInGame(owner)){
                float body[3];GetClientAbsOrigin(owner,body);body[2]+=40.0;
                if(GetVectorDistance(body,impact)<180.0+520.0*age){g_Blocked++;return 0.0;}
            }
            float distance=GetVectorDistance(point,end);float safe=fraction-(distance>0.0?0.5/distance:0.0);if(safe<0.0)safe=0.0;
            for(int axis=0;axis<3;axis++)point[axis]+=(end[axis]-point[axis])*safe;
            motion[2]+=accel*dt*safe;advanced+=dt*safe;g_Blocked++;break;
        }
        point=end;motion[2]+=accel*dt;advanced+=dt;
    }
    if(advanced<=0.0)return 0.0;
    TeleportEntity(entity,point,NULL_VECTOR,motion);origin=point;velocity=motion;g_Advanced++;return advanced;
}

bool RocketFilter(int entity,int mask,any skip)
{
    if(entity==skip||entity==g_TraceProjectile) return false;
    if(entity>MaxClients&&IsValidEntity(entity))
    {
        char cls[64];GetEntityClassname(entity,cls,sizeof(cls));
        if(StrEqual(cls,"tf_projectile_rocket")) return false;
    }
    return true;
}

float AdvanceRocket(int entity,int owner,float origin[3],const float velocity[3],float age)
{
    float motion[3];motion=velocity;return AdvanceCurved(entity,owner,origin,motion,0.0,0.0,age);
}

public void OnEntityDestroyed(int entity)
{
    if(entity<=MaxClients||entity>=sizeof(g_ProjectileCommand)||g_ProjectileCommand[entity]<=0) return;
    int owner=GetClientFromSerial(g_ProjectileOwner[entity]);
    char message[160];
    if(owner>0&&g_Protocol[owner]>=2)Format(message,sizeof(message),"RP%d E %d %d %d %d %d",g_Protocol[owner],g_Epoch,g_ProjectileCommand[entity],g_ProjectileRef[entity],g_Command[owner],g_ProjectileSelf[entity]);
    else Format(message,sizeof(message),"RP1 E %d %d %d",g_Epoch,g_ProjectileCommand[entity],g_ProjectileRef[entity]);
    if(owner>0&&g_Protocol[owner]>0)Message(owner,message);
    g_ProjectileCommand[entity]=0;g_ProjectileOwner[entity]=0;g_ProjectileRef[entity]=0;g_Terminals++;
    g_ProjectileSelf[entity]=false;if(g_TouchEntity==entity)g_TouchEntity=0;
}

float ForceScale(bool airborne)
{
    ConVar c=FindConVar(airborne?"tf_damageforcescale_self_soldier_rj":"tf_damageforcescale_self_soldier_badrj");
    return c!=null?c.FloatValue:(airborne?10.0:5.0);
}

void HookDamage(int client)
{
    SDKHook(client,SDKHook_OnTakeDamage,DamageBefore);
    SDKHook(client,SDKHook_OnTakeDamagePost,DamageAfter);
}

public Action DamageBefore(int victim,int &attacker,int &inflictor,float &damage,int &type,int &weapon,float force[3],float point[3],int custom)
{
    g_BeforeRocket[victim]=INVALID_ENT_REFERENCE;
    if(inflictor<=MaxClients||inflictor>=sizeof(g_ProjectileCommand)||g_ProjectileCommand[inflictor]<=0)return Plugin_Continue;
    if(victim!=GetClientFromSerial(g_ProjectileOwner[inflictor]))return Plugin_Continue;
    if(ProjectileOwner(inflictor)!=victim)return Plugin_Continue;
    g_BeforeRocket[victim]=EntIndexToEntRef(inflictor);g_BeforeHealth[victim]=GetClientHealth(victim);
    GetEntPropVector(victim,Prop_Data,"m_vecAbsVelocity",g_BeforeVelocity[victim]);
    return Plugin_Continue;
}

public void DamageAfter(int victim,int attacker,int inflictor,float damage,int type,int weapon,const float force[3],const float point[3],int custom)
{
    int rocket=EntRefToEntIndex(g_BeforeRocket[victim]);g_BeforeRocket[victim]=INVALID_ENT_REFERENCE;
    if(rocket<=MaxClients||rocket>=sizeof(g_ProjectileCommand)||!IsValidEntity(rocket)||g_ProjectileCommand[rocket]<=0)return;
    float velocity[3];GetEntPropVector(victim,Prop_Data,"m_vecAbsVelocity",velocity);
    for(int a=0;a<3;a++)velocity[a]-=g_BeforeVelocity[victim][a];
    g_ProjectileSelf[rocket]=true;g_SelfBlasts++;
    if(g_Protocol[victim]>=2){
        char message[240];Format(message,sizeof(message),"RP%d B %d %d %d %.4f %.4f %.4f %.3f %d",g_Protocol[victim],g_Epoch,g_ProjectileCommand[rocket],g_Command[victim],velocity[0],velocity[1],velocity[2],float(g_BeforeHealth[victim]-GetClientHealth(victim)),GetClientHealth(victim));Message(victim,message);
    }
#if defined RP_TEST
    TestSelfDamage(victim,rocket,velocity);
#endif
}

public Action RocketTouch(int rocket,int other)
{
    g_TouchEntity=rocket;return Plugin_Continue;
}
public void RocketTouched(int rocket,int other) {if(g_TouchEntity==rocket)g_TouchEntity=0;}

public Action OnExplosion(const char[] name,const int[] clients,int count,float delay)
{
    if(g_ForwardingExplosion)return Plugin_Continue;
    int rocket=g_TouchEntity;
    if(rocket<=MaxClients||rocket>=sizeof(g_ProjectileCommand)||!IsValidEntity(rocket)||g_ProjectileCommand[rocket]<=0)return Plugin_Continue;
    int owner=GetClientFromSerial(g_ProjectileOwner[rocket]);
    if(owner<=0||g_Protocol[owner]<2||ProjectileOwner(rocket)!=owner||(HasEntProp(rocket,Prop_Send,"m_iDeflected")&&GetEntProp(rocket,Prop_Send,"m_iDeflected")!=0))return Plugin_Continue;
    float origin[3],normal[3],actual[3];
    origin[0]=TE_ReadFloat("m_vecOrigin[0]");origin[1]=TE_ReadFloat("m_vecOrigin[1]");origin[2]=TE_ReadFloat("m_vecOrigin[2]");
    TE_ReadVector("m_vecNormal",normal);GetEntPropVector(rocket,Prop_Send,"m_vecOrigin",actual);
    if(GetVectorDistance(origin,actual)>4.0)return Plugin_Continue;
    int weapon=TE_ReadNum("m_iWeaponID"),entity=TE_ReadNum("entindex"),definition=TE_ReadNum("m_nDefID"),sound=TE_ReadNum("m_nSound"),particle=TE_ReadNum("m_iCustomParticleIndex");
    if(weapon<0||weapon>=256)return Plugin_Continue;
    char message[250];Format(message,sizeof(message),"RP%d X %d %d %.3f %.3f %.3f %.4f %.4f %.4f %d %d %d %d %d",g_Protocol[owner],g_Epoch,g_ProjectileCommand[rocket],origin[0],origin[1],origin[2],normal[0],normal[1],normal[2],weapon,definition,sound,particle,entity);
    Message(owner,message);g_Explosions++;
    int recipients[MAXPLAYERS+1],number;
    for(int i=0;i<count;i++)if(clients[i]!=owner)recipients[number++]=clients[i];
    if(number>0){
        g_ForwardingExplosion=true;
        TE_Start("TFExplosion");TE_WriteFloat("m_vecOrigin[0]",origin[0]);TE_WriteFloat("m_vecOrigin[1]",origin[1]);TE_WriteFloat("m_vecOrigin[2]",origin[2]);
        TE_WriteVector("m_vecNormal",normal);TE_WriteNum("m_iWeaponID",weapon);TE_WriteNum("entindex",entity);TE_WriteNum("m_nDefID",definition);TE_WriteNum("m_nSound",sound);TE_WriteNum("m_iCustomParticleIndex",particle);
        TE_Send(recipients,number,delay);g_ForwardingExplosion=false;
    }
    return Plugin_Handled;
}

#if defined RP_TEST
    #include "../tests/server_checks.inc"
#endif
