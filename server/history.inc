#define RP_HISTORY_FRAMES 16
#define RP_HISTORY_DOORS 96
float g_HistoryTime[RP_HISTORY_FRAMES];
bool g_HistoryPlayerValid[MAXPLAYERS+1][RP_HISTORY_FRAMES];
float g_HistoryPlayerMin[MAXPLAYERS+1][RP_HISTORY_FRAMES][3],g_HistoryPlayerMax[MAXPLAYERS+1][RP_HISTORY_FRAMES][3];
int g_HistoryDoors[RP_HISTORY_DOORS],g_HistoryDoorCount,g_HistoryCursor;
float g_HistoryDoorMin[RP_HISTORY_DOORS][RP_HISTORY_FRAMES][3],g_HistoryDoorMax[RP_HISTORY_DOORS][RP_HISTORY_FRAMES][3];
bool g_HistoryDoorValid[RP_HISTORY_DOORS][RP_HISTORY_FRAMES];

void HistoryReset()
{
    g_HistoryCursor=0;g_HistoryDoorCount=0;
    for(int frame=0;frame<RP_HISTORY_FRAMES;frame++){
        g_HistoryTime[frame]=-100.0;
        for(int client=1;client<=MaxClients;client++)g_HistoryPlayerValid[client][frame]=false;
        for(int door=0;door<RP_HISTORY_DOORS;door++)g_HistoryDoorValid[door][frame]=false;
    }
    for(int entity=MaxClients+1;entity<GetMaxEntities()&&g_HistoryDoorCount<RP_HISTORY_DOORS;entity++)if(IsValidEntity(entity)){
        char cls[64];GetEntityClassname(entity,cls,sizeof(cls));
        if(StrEqual(cls,"func_door")||StrEqual(cls,"func_door_rotating")||StrEqual(cls,"func_movelinear")||StrEqual(cls,"func_tracktrain")||StrEqual(cls,"func_train"))g_HistoryDoors[g_HistoryDoorCount++]=EntIndexToEntRef(entity);
    }
}

public void HistorySpawn(Event event,const char[] name,bool dontBroadcast)
{
    int client=GetClientOfUserId(event.GetInt("userid"));if(client<1)return;
    for(int frame=0;frame<RP_HISTORY_FRAMES;frame++)g_HistoryPlayerValid[client][frame]=false;
}

void HistoryCapture()
{
    int frame=g_HistoryCursor;g_HistoryCursor=(g_HistoryCursor+1)%RP_HISTORY_FRAMES;g_HistoryTime[frame]=GetGameTime();
    for(int client=1;client<=MaxClients;client++){
        bool valid=IsClientInGame(client)&&IsPlayerAlive(client);g_HistoryPlayerValid[client][frame]=valid;if(!valid)continue;
        float origin[3],mins[3],maxs[3];GetClientAbsOrigin(client,origin);GetClientMins(client,mins);GetClientMaxs(client,maxs);
        for(int axis=0;axis<3;axis++){g_HistoryPlayerMin[client][frame][axis]=origin[axis]+mins[axis];g_HistoryPlayerMax[client][frame][axis]=origin[axis]+maxs[axis];}
    }
    for(int door=0;door<g_HistoryDoorCount;door++){
        int entity=EntRefToEntIndex(g_HistoryDoors[door]);bool valid=entity>MaxClients&&IsValidEntity(entity);g_HistoryDoorValid[door][frame]=valid;if(!valid)continue;
        float origin[3],angles[3],mins[3],maxs[3],fwd[3],right[3],up[3];
        GetEntPropVector(entity,Prop_Send,"m_vecOrigin",origin);GetEntPropVector(entity,Prop_Send,"m_angRotation",angles);
        GetEntPropVector(entity,Prop_Send,"m_vecMins",mins);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",maxs);GetAngleVectors(angles,fwd,right,up);
        for(int axis=0;axis<3;axis++){g_HistoryDoorMin[door][frame][axis]=999999.0;g_HistoryDoorMax[door][frame][axis]=-999999.0;}
        for(int corner=0;corner<8;corner++){
            float x=(corner&1)?maxs[0]:mins[0],y=(corner&2)?maxs[1]:mins[1],z=(corner&4)?maxs[2]:mins[2];
            for(int axis=0;axis<3;axis++){
                float value=origin[axis]+x*fwd[axis]-y*right[axis]+z*up[axis];
                if(value<g_HistoryDoorMin[door][frame][axis])g_HistoryDoorMin[door][frame][axis]=value;
                if(value>g_HistoryDoorMax[door][frame][axis])g_HistoryDoorMax[door][frame][axis]=value;
            }
        }
    }
}

float HistoryBoxFraction(const float from[3],const float to[3],const float mins[3],const float maxs[3],float hull)
{
    float enter=0.0,leave=1.0;
    for(int axis=0;axis<3;axis++){
        float delta=to[axis]-from[axis],lo=mins[axis]-hull,hi=maxs[axis]+hull;
        if(FloatAbs(delta)<0.00001){if(from[axis]<lo||from[axis]>hi)return 1.0;continue;}
        float a=(lo-from[axis])/delta,b=(hi-from[axis])/delta;
        if(a>b){float temp=a;a=b;b=temp;}
        if(a>enter)enter=a;if(b<leave)leave=b;if(leave<enter)return 1.0;
    }
    return enter;
}

float HistoryFraction(const float from[3],const float to[3],float hull,float time,int owner)
{
    float fraction=1.0;
    for(int frame=0;frame<RP_HISTORY_FRAMES;frame++)if(FloatAbs(g_HistoryTime[frame]-time)<=GetTickInterval()*1.5){
        for(int client=1;client<=MaxClients;client++)if(client!=owner&&IsClientInGame(client)&&IsPlayerAlive(client)&&g_HistoryPlayerValid[client][frame]){
            float hit=HistoryBoxFraction(from,to,g_HistoryPlayerMin[client][frame],g_HistoryPlayerMax[client][frame],hull+4.0);if(hit<fraction)fraction=hit;
        }
        for(int door=0;door<g_HistoryDoorCount;door++)if(g_HistoryDoorValid[door][frame]&&EntRefToEntIndex(g_HistoryDoors[door])>MaxClients){
            float hit=HistoryBoxFraction(from,to,g_HistoryDoorMin[door][frame],g_HistoryDoorMax[door][frame],hull);if(hit<fraction)fraction=hit;
        }
    }
    return fraction;
}
