#include <tf2_stocks>
int g_TestActorSerial, g_TestStartAcks, g_TestStartRejected, g_TestCommands, g_TestSpawn;
int g_TestCase,g_TestFiredAt;
int g_TestItem=18;
float g_TestAdvance;
int g_TestFlightRef,g_TestFlightItem;
float g_TestFlightOrigin[3],g_TestFlightVelocity[3],g_TestFlightTime;
void TestVelocitySource(int entity,int item,const float velocity[3])
{
    if(g_TestActorSerial==0)return;
    float legacy[3];GetEntPropVector(entity,Prop_Data,"m_vecAbsVelocity",legacy);
    LogMessage("RP_VELOCITY item=%d move=%d legacy=%.3f native=%.3f vx=%.3f vy=%.3f vz=%.3f",item,GetEntityMoveType(entity),GetVectorLength(legacy),GetVectorLength(velocity),velocity[0],velocity[1],velocity[2]);
}
void TestWatchFlight(int entity,int item,const float origin[3],const float velocity[3])
{
    if(g_TestActorSerial==0||g_TestCase!=0)return;
    g_TestFlightRef=EntIndexToEntRef(entity);g_TestFlightItem=item;
    g_TestFlightOrigin=origin;g_TestFlightVelocity=velocity;g_TestFlightTime=GetGameTime();
}
void TestFlightFrame()
{
    if(g_TestFlightRef==0)return;
    float sampleAfter=GetVectorLength(g_TestFlightVelocity)>1500.0?0.06:0.20;
    if(GetGameTime()-g_TestFlightTime<sampleAfter)return;
    int entity=EntRefToEntIndex(g_TestFlightRef);g_TestFlightRef=0;
    if(entity<=MaxClients||!IsValidEntity(entity))return;
    float origin[3],velocity[3],delta[3],axis[3];GetEntPropVector(entity,Prop_Send,"m_vecOrigin",origin);
    if(!ProjectileVelocity(entity,velocity))return;
    SubtractVectors(origin,g_TestFlightOrigin,delta);axis=g_TestFlightVelocity;axis[2]=0.0;NormalizeVector(axis,axis);
    LogMessage("RP_FLIGHT item=%d dt=%.3f forward=%.3f height=%.3f speed=%.3f initial=%.3f advance=%.3f",g_TestFlightItem,GetGameTime()-g_TestFlightTime,GetVectorDotProduct(delta,axis),delta[2],GetVectorLength(velocity),GetVectorLength(g_TestFlightVelocity),g_TestAdvance);
}


bool TestControl(int client,int &buttons,float vel[3],float angles[3],int &cmdnum,int &tickcount)
{
    if(g_TestActorSerial==0||GetClientSerial(client)!=g_TestActorSerial) return false;
    g_TestCommands++;
    g_Enabled[client]=true;g_Protocol[client]=g_TestItem!=18||g_TestCase>=2?4:2;buttons=0;vel[0]=0.0;vel[1]=0.0;vel[2]=0.0;
    angles[0]=g_TestItem==1152?70.0:(g_TestCase==0?0.0:89.0);angles[1]=0.0;angles[2]=0.0;
    if(g_TestCase==3){
        buttons|=IN_DUCK;
        SetEntProp(client,Prop_Send,"m_bDucked",1);SetEntProp(client,Prop_Send,"m_bDucking",0);SetEntityFlags(client,GetEntityFlags(client)|FL_DUCKING);
        float duckMaxs[3]={24.0,24.0,62.0};SetEntPropVector(client,Prop_Send,"m_vecMaxs",duckMaxs);
    }
    int active=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    bool ready=(GetEntityFlags(client)&FL_ONGROUND)!=0&&g_TestCommands>=50&&active>MaxClients;
    if(ready&&GetEntPropFloat(active,Prop_Send,"m_flNextPrimaryAttack")>float(GetEntProp(client,Prop_Send,"m_nTickBase"))*GetTickInterval())ready=false;
    if(g_TestCase==3&&g_TestCommands<10)ready=false;
    if(ready&&GetEntPropFloat(client,Prop_Send,"m_flNextAttack")>float(GetEntProp(client,Prop_Send,"m_nTickBase"))*GetTickInterval())ready=false;
    if(g_TestFiredAt==0&&ready){
        g_TestFiredAt=GetGameTickCount();
        LogMessage("RP_INPUT case=%d round=%d flags=%d next=%.3f",g_TestCase,GameRules_GetProp("m_iRoundState"),GetEntityFlags(client),GetEntPropFloat(client,Prop_Send,"m_flNextAttack"));
    }
    if(g_TestFiredAt>0&&active>MaxClients&&g_Acks==g_TestStartAcks){
        WeaponSpec spec;WeaponProfile(g_TestItem,spec);
        if((spec.kind!=3&&spec.kind!=5)||GetGameTickCount()-g_TestFiredAt<8)buttons|=IN_ATTACK;
        if(spec.kind==14||spec.kind==15){buttons&=~IN_ATTACK;buttons|=IN_ATTACK2;}
        if(g_TestCase>=2)buttons|=IN_JUMP;
    }
    TeleportEntity(client,NULL_VECTOR,angles,NULL_VECTOR);
    cmdnum=1000000+GetGameTickCount();tickcount=GetGameTickCount();
    return true;
}

public Action NativeFireCheck(int client,int args)
{
    if(g_TestActorSerial!=0) return Plugin_Handled;
    for(int i=1;i<=MaxClients;i++) if(IsClientInGame(i))
    {
        ReplyToCommand(client,"RP_FIRE refused: server must be empty");return Plugin_Handled;
    }
    g_TestStartAcks=g_Acks;g_TestStartRejected=g_Rejected;
    char mode[16];GetCmdArg(1,mode,sizeof(mode));g_TestCase=StringToInt(mode);g_TestFiredAt=0;g_TestCommands=0;
    g_TestItem=18;if(args>1){GetCmdArg(2,mode,sizeof(mode));g_TestItem=StringToInt(mode);}
    g_TestAdvance=0.0;g_TestFlightRef=0;if(args>2){GetCmdArg(3,mode,sizeof(mode));g_TestAdvance=StringToFloat(mode);if(g_TestAdvance<0.0||g_TestAdvance>0.1)g_TestAdvance=0.0;}
    int spawn=CreateEntityByName("info_player_teamspawn");
    DispatchKeyValue(spawn,"TeamNum","2");DispatchKeyValue(spawn,"StartDisabled","0");DispatchSpawn(spawn);AcceptEntityInput(spawn,"Enable");
    float pos[3]={-14375.0,-6875.0,160.0};TeleportEntity(spawn,pos,NULL_VECTOR,NULL_VECTOR);g_TestSpawn=spawn;
    FindConVar("tf_bot_quota_mode").SetString("normal");
    FindConVar("mp_waitingforplayers_time").FloatValue=0.0;
    FindConVar("mp_waitingforplayers_cancel").BoolValue=true;
    ServerCommand("tf_bot_add 1 soldier red easy RPProbe");
    CreateTimer(0.6,PrepareNativeFire,_,TIMER_FLAG_NO_MAPCHANGE);
    ReplyToCommand(client,"RP_FIRE started");return Plugin_Handled;
}

public Action PrepareNativeFire(Handle timer)
{
    for(int i=1;i<=MaxClients;i++) if(IsClientInGame(i)&&IsFakeClient(i))
    {
        g_TestActorSerial=GetClientSerial(i);
        WeaponSpec spec;WeaponProfile(g_TestItem,spec);TFClassType playerClass=TFClass_Soldier;
        char classname[64]="tf_weapon_rocketlauncher";
        switch(spec.kind){
            case 2:{playerClass=TFClass_DemoMan;strcopy(classname,sizeof(classname),g_TestItem==996?"tf_weapon_cannon":"tf_weapon_grenadelauncher");}
            case 3:{playerClass=TFClass_DemoMan;strcopy(classname,sizeof(classname),"tf_weapon_pipebomblauncher");}
            case 4:{playerClass=TFClass_Pyro;strcopy(classname,sizeof(classname),g_TestItem==595?"tf_weapon_flaregun_revenge":"tf_weapon_flaregun");}
            case 5:{playerClass=TFClass_Sniper;strcopy(classname,sizeof(classname),"tf_weapon_compound_bow");}
            case 6:{playerClass=TFClass_Medic;strcopy(classname,sizeof(classname),"tf_weapon_crossbow");}
            case 7:{playerClass=TFClass_Engineer;strcopy(classname,sizeof(classname),"tf_weapon_shotgun_building_rescue");}
            case 8:{strcopy(classname,sizeof(classname),"tf_weapon_particle_cannon");}
            case 9:{playerClass=g_TestItem==588?TFClass_Engineer:TFClass_Soldier;strcopy(classname,sizeof(classname),g_TestItem==588?"tf_weapon_drg_pomson":"tf_weapon_raygun");}
            case 10:{playerClass=TFClass_Sniper;strcopy(classname,sizeof(classname),"tf_weapon_jar");}
            case 11:{playerClass=TFClass_Scout;strcopy(classname,sizeof(classname),"tf_weapon_jar_milk");}
            case 12:{playerClass=TFClass_Pyro;strcopy(classname,sizeof(classname),"tf_weapon_jar_gas");}
            case 13:{playerClass=TFClass_Scout;strcopy(classname,sizeof(classname),"tf_weapon_cleaver");}
            case 14:{playerClass=TFClass_Scout;strcopy(classname,sizeof(classname),"tf_weapon_bat_wood");}
            case 15:{playerClass=TFClass_Scout;strcopy(classname,sizeof(classname),"tf_weapon_bat_giftwrap");}
            case 16:{strcopy(classname,sizeof(classname),"tf_weapon_grapplinghook");}
        }
        if(g_TestItem==127)strcopy(classname,sizeof(classname),"tf_weapon_rocketlauncher_directhit");
        if(g_TestItem==1104)strcopy(classname,sizeof(classname),"tf_weapon_rocketlauncher_airstrike");
        ChangeClientTeam(i,2);TF2_SetPlayerClass(i,playerClass);TF2_RespawnPlayer(i);
        if(g_TestItem!=18){
            TF2_RemoveAllWeapons(i);int weapon=GivePlayerItem(i,classname);
            if(weapon>MaxClients){SetEntProp(weapon,Prop_Send,"m_iItemDefinitionIndex",g_TestItem);EquipPlayerWeapon(i,weapon);SetEntPropEnt(i,Prop_Send,"m_hActiveWeapon",weapon);}
            for(int ammo=1;ammo<=5;ammo++)SetEntProp(i,Prop_Send,"m_iAmmo",200,4,ammo);
            g_Protocol[i]=4;
        }
        GameRules_SetProp("m_iRoundState",4);GameRules_SetProp("m_bInWaitingForPlayers",0);
        TF2_RemoveCondition(i,TFCond_Ubercharged);TF2_RemoveCondition(i,TFCond_UberchargeFading);
        float pos[3]={-14375.0,-6875.0,406.0},ang[3]={0.0,0.0,0.0},zero[3],end[3];end=pos;end[2]-=1400.0;
        float mins[3]={-24.0,-24.0,0.0},maxs[3]={24.0,24.0,82.0};
        Handle ground=TR_TraceHullFilterEx(pos,end,mins,maxs,MASK_PLAYERSOLID,RaySkipActor,i);
        if(TR_DidHit(ground)&&!TR_StartSolid(ground)){TR_GetEndPosition(pos,ground);pos[2]+=2.0;}
        delete ground;
        SetEntityMoveType(i,MOVETYPE_WALK);SetEntityGravity(i,1.0);
        TeleportEntity(i,pos,ang,zero);
        SetEntityHealth(i,300);
        CreateTimer(5.0,FinishNativeFire,_,TIMER_FLAG_NO_MAPCHANGE);
        PrintToServer("RP_FIRE actor=%d weapon=%d",i,GetPlayerWeaponSlot(i,0));
        LogMessage("RP_FIRE actor=%d alive=%d weapon=%d",i,IsPlayerAlive(i),GetPlayerWeaponSlot(i,0));
        return Plugin_Stop;
    }
    LogMessage("RP_FIRE fail: no actor");PrintToServer("RP_FIRE fail: no actor");return Plugin_Stop;
}

public Action FinishNativeFire(Handle timer)
{
    int actor=GetClientFromSerial(g_TestActorSerial);g_TestActorSerial=0;
    if(actor>0){float pos[3];GetClientAbsOrigin(actor,pos);int weapon=GetEntPropEnt(actor,Prop_Send,"m_hActiveWeapon");
        LogMessage("RP_STATE case=%d fired=%d pos=%.2f %.2f %.2f flags=%d movetype=%d weapon=%d clip=%d time=%.3f ticktime=%.3f next=%.3f",g_TestCase,g_TestFiredAt,pos[0],pos[1],pos[2],GetEntityFlags(actor),GetEntityMoveType(actor),weapon,weapon>MaxClients?GetEntProp(weapon,Prop_Send,"m_iClip1"):-1,GetGameTime(),float(GetEntProp(actor,Prop_Send,"m_nTickBase"))*GetTickInterval(),weapon>MaxClients?GetEntPropFloat(weapon,Prop_Send,"m_flNextPrimaryAttack"):0.0);
    }
    PrintToServer("RP_FIRE ack=%d rejected=%d terminal=%d",g_Acks-g_TestStartAcks,g_Rejected-g_TestStartRejected,g_Terminals);
    LogMessage("RP_FIRE case=%d ack=%d rejected=%d terminal=%d explosions=%d self=%d commands=%d actor=%d alive=%d",g_TestCase,g_Acks-g_TestStartAcks,g_Rejected-g_TestStartRejected,g_Terminals,g_Explosions,g_SelfBlasts,g_TestCommands,actor,actor>0&&IsPlayerAlive(actor));
    if(g_TestSpawn!=0&&IsValidEntity(g_TestSpawn))RemoveEntity(g_TestSpawn);g_TestSpawn=0;
    if(actor>0) KickClient(actor,"probe complete");return Plugin_Stop;
}

bool RaySkipActor(int entity,int mask,any actor) {return entity!=actor;}

void TestSelfDamage(int victim,int rocket,const float actual[3])
{
    if(g_TestActorSerial==0||GetClientSerial(victim)!=g_TestActorSerial)return;
    float blast[3],feet[3],mins[3],maxs[3];GetEntPropVector(rocket,Prop_Send,"m_vecOrigin",blast);
    GetClientAbsOrigin(victim,feet);GetClientMins(victim,mins);GetClientMaxs(victim,maxs);
    char file[PLATFORM_MAX_PATH];BuildPath(Path_SM,file,sizeof(file),"logs/projectile-self.csv");
    File output=OpenFile(file,"a");if(output==null)return;
    output.WriteLine("%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d",g_TestCase,g_ProjectileCommand[rocket],GetEntityFlags(victim),blast[0],blast[1],blast[2],feet[0],feet[1],feet[2],mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2],actual[0],actual[1],actual[2],g_BeforeHealth[victim],GetClientHealth(victim));
    delete output;
}

public Action NativeCheck(int client,int args)
{
    for(int i=1;i<=MaxClients;i++) if(IsClientInGame(i))
    {
        ReplyToCommand(client,"RP_TEST refused: server must be empty");return Plugin_Handled;
    }
    float start[3]={-14375.0,-6875.0,230.0};
    float velocity[3]={1100.0,0.0,0.0};
    float origin[3];origin=start;
    int rocket=CreateEntityByName("tf_projectile_rocket");
    DispatchSpawn(rocket);TeleportEntity(rocket,start,NULL_VECTOR,velocity);
    float result=AdvanceRocket(rocket,-1,origin,velocity,0.05);
    float actual[3],speed[3];GetEntPropVector(rocket,Prop_Send,"m_vecOrigin",actual);GetEntPropVector(rocket,Prop_Data,"m_vecAbsVelocity",speed);
    bool openOk=FloatAbs(result-0.05)<0.0001&&FloatAbs(actual[0]-start[0]-55.0)<0.01&&FloatAbs(GetVectorLength(speed)-1100.0)<0.01;
    RemoveEntity(rocket);
    float end[3];end=start;end[2]-=1024.0;
    Handle tr=TR_TraceRayFilterEx(start,end,MASK_SHOT,RayType_EndPoint,RocketFilter,-1);
    bool hit=TR_DidHit(tr)&&TR_GetEntityIndex(tr)==0;
    if(hit){TR_GetEndPosition(start,tr);start[2]+=20.0;}delete tr;
    velocity[0]=0.0;velocity[2]=-1100.0;
    rocket=CreateEntityByName("tf_projectile_rocket");DispatchSpawn(rocket);TeleportEntity(rocket,start,NULL_VECTOR,velocity);
    origin=start;result=AdvanceRocket(rocket,-1,origin,velocity,0.05);GetEntPropVector(rocket,Prop_Send,"m_vecOrigin",actual);
    bool wallOk=hit&&result>=0.0&&result<0.05&&actual[2]>=start[2]-20.05&&actual[2]<=start[2];
    RemoveEntity(rocket);
    float boxMin[3]={40.0,-5.0,-5.0},boxMax[3]={60.0,5.0,5.0},rayStart[3],rayEnd[3]={100.0,0.0,0.0};
    bool historyOk=FloatAbs(HistoryBoxFraction(rayStart,rayEnd,boxMin,boxMax,2.0)-0.38)<0.0001;
    rayStart[1]=20.0;rayEnd[1]=20.0;historyOk=historyOk&&HistoryBoxFraction(rayStart,rayEnd,boxMin,boxMax,2.0)==1.0;
    float curvedStart[3]={-14375.0,-6875.0,230.0},curvedVelocity[3]={1100.0,0.0,0.0};origin=curvedStart;
    rocket=CreateEntityByName("tf_projectile_flare");DispatchSpawn(rocket);TeleportEntity(rocket,curvedStart,NULL_VECTOR,curvedVelocity);
    result=AdvanceCurved(rocket,-1,origin,curvedVelocity,0.3,0.0,0.05);
    bool gravityOk=FloatAbs(result-0.05)<0.0001&&FloatAbs(origin[0]-curvedStart[0]-55.0)<0.01&&FloatAbs(origin[2]-curvedStart[2]+0.3)<0.01&&FloatAbs(curvedVelocity[2]+12.0)<0.01;
    RemoveEntity(rocket);
    ReplyToCommand(client,"RP_TEST open=%d wall=%d native_wall=%d normal_speed=%d history=%d gravity=%d",openOk,wallOk,hit,FloatAbs(GetVectorLength(speed)-1100.0)<0.01,historyOk,gravityOk);
    return Plugin_Handled;
}
