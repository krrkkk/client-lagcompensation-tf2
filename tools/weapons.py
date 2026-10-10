#!/usr/bin/env python3
import argparse
import schema
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT.parents[1] if ROOT.parent.name=="tf2server" else ROOT

def generate(source,output):
    metadata,items=schema.build(source)
    bases={
        'tf_weapon_rocketlauncher':(1,1100,0,90,146,'models/weapons/w_models/w_rocket.mdl'),
        'tf_weapon_rocketlauncher_directhit':(1,1100,0,90,146,'models/weapons/w_models/w_rocket.mdl'),
        'tf_weapon_rocketlauncher_airstrike':(1,1100,0,90,146,'models/weapons/w_models/w_rocket_airstrike/w_rocket_airstrike.mdl'),
        'tf_weapon_grenadelauncher':(2,1200,1,100,146,'models/weapons/w_models/w_grenade_grenadelauncher.mdl'),
        'tf_weapon_cannon':(2,1200,1,60,146,'models/weapons/w_models/w_cannonball.mdl'),
        'tf_weapon_pipebomblauncher':(3,900,1,120,146,'models/weapons/w_models/w_stickybomb.mdl'),
        'tf_weapon_flaregun':(4,2000,.3,30,110,'models/weapons/w_models/w_flaregun_shell.mdl'),
        'tf_weapon_flaregun_revenge':(4,2000,.3,30,110,'models/weapons/w_models/w_flaregun_shell.mdl'),
        'tf_weapon_compound_bow':(5,1800,.5,50,0,'models/weapons/w_models/w_arrow.mdl'),
        'tf_weapon_crossbow':(6,2400,.2,75,0,'models/weapons/w_models/w_syringe_proj.mdl'),
        'tf_weapon_shotgun_building_rescue':(7,2400,.2,40,0,'models/weapons/w_models/w_repair_claw.mdl'),
        'tf_weapon_particle_cannon':(8,1100,0,90,146,'models/weapons/w_models/w_drg_ball.mdl'),
        'tf_weapon_raygun':(9,1200,0,20,0,'models/weapons/w_models/w_drg_ball.mdl'),
        'tf_weapon_drg_pomson':(9,1200,0,60,0,'models/weapons/w_models/w_drg_ball.mdl'),
        'tf_weapon_jar':(10,1000,1,0,200,'models/weapons/c_models/urinejar.mdl'),
        'tf_weapon_jar_milk':(11,1000,1,0,200,'models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl'),
        'tf_weapon_jar_gas':(12,2000,1,0,200,'models/weapons/c_models/c_gascan/c_gascan.mdl'),
        'tf_weapon_cleaver':(13,3000,1,50,0,'models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl'),
        'tf_weapon_bat_wood':(14,3000,.5,15,0,'models/weapons/w_models/w_baseball.mdl'),
        'tf_weapon_bat_giftwrap':(15,3000,.5,15,0,'models/weapons/c_models/c_xms_festive_ornament.mdl'),
        'tf_weapon_grapplinghook':(16,1500,0,0,0,'models/weapons/c_models/c_grapple_proj/c_grapple_proj.mdl'),
    }
    rows=[]
    for item,entry in items.items():
        if entry['class'] not in bases:continue
        kind,speed,gravity,damage,radius,model=bases[entry['class']]
        attrs={a['class']:a['value'] for a in entry['attributes'].values()}
        def value(name,default):
            try:return float(attrs.get(name,default))
            except (TypeError,ValueError):return default
        speed*=value('mult_projectile_speed',1)
        damage*=value('mult_dmg',1)
        radius*=value('mult_explosion_radius',1)
        flags=(1 if value('centerfire_projectile',0) else 0)|(2 if value('no_self_blast_dmg',0) else 0)|(4 if value('grenade_no_bounce',0) else 0)|(8 if value('set_detonate_mode',0)==2 else 0)|(16 if value('projectile_spread_angle',0) else 0)|(32 if value('grenade_no_spin',0) else 0)
        model=attrs.get('custom_projectile_model',model)
        if item=='130':model='models/weapons/w_models/w_stickybomb2.mdl'
        if item=='1005':model='models/weapons/w_models/w_arrow_xmas.mdl'
        rows.append(dict(item=int(item),name=entry['name'],kind=kind,speed=speed,gravity=gravity,damage=damage,radius=radius,selfPush=value('mult_dmgself_push_force',1),chargeTime=4*value('stickybomb_charge_rate',1) if kind==3 else (1 if kind==5 else 0),maxSpeed=2400 if kind==3 else (2600 if kind==5 else speed),up=200 if kind in [2,3,10,11,12] else 0,hull=8 if kind in [2,3,10,11,12,13,14,15] else 0,delay=.1 if kind in [10,11,12,13,14,15] else 0,flags=flags,model=model))
    rows.sort(key=lambda row:row['item'])
    for row in rows:
        row['weaponId']={1:22,2:23,3:24,4:58,5:61,6:73,7:90,8:79,9:78}.get(row['kind'],0)
        if row['item']==127:row['weaponId']=65
        if row['item']==996:row['weaponId']=91
        if row['item']==595:row['weaponId']=84
        if row['item']==588:row['weaponId']=81
    fields=['speed','gravity','damage','radius','selfPush','chargeTime','maxSpeed','up','hull','delay']
    cpp=['#pragma once','#include "weapons.h"','namespace rp {','inline const WeaponSpec weaponSpecs[]={']
    for row in rows:cpp.append('    {'+str(row['item'])+',ProjectileKind('+str(row['kind'])+'),'+','.join(f'{row[k]:.6f}f' for k in fields)+','+str(row['flags'])+','+str(row['weaponId'])+','+json.dumps(row['model'])+'},')
    cpp+=['};','inline const WeaponSpec *weaponSpec(int item) {','    int lo=0,hi=int(sizeof(weaponSpecs)/sizeof(weaponSpecs[0]));','    while(lo<hi){int mid=(lo+hi)/2;if(weaponSpecs[mid].item<item)lo=mid+1;else hi=mid;}','    return lo<int(sizeof(weaponSpecs)/sizeof(weaponSpecs[0]))&&weaponSpecs[lo].item==item?&weaponSpecs[lo]:nullptr;','}','}']
    (ROOT/'client/weapon_profiles.h').write_text('\n'.join(cpp)+'\n')
    sp=['enum struct WeaponSpec','{','    int item;','    int kind;']+['    float '+field+';' for field in fields]+['    int flags;','}','bool WeaponProfile(int item,WeaponSpec spec)','{','    spec.item=item;','    switch(item)','    {']
    for row in rows:
        sp+=['        case '+str(row['item'])+':','        {','            spec.kind='+str(row['kind'])+';']+['            spec.'+field+'='+f'{row[field]:.6f}'+';' for field in fields]+['            spec.flags='+str(row['flags'])+';','        }']
    sp+=['        default:return false;','    }','    return true;','}']
    (ROOT/'server/weapon_profiles.inc').write_text('\n'.join(sp)+'\n')
    out=output;out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps({'schema_sha256':metadata['source_sha256'],'status':'generated parameters; per-family runtime verification required','weapons':rows},ensure_ascii=False,indent=2)+'\n')
    print(f'{len(rows)} weapon definitions generated')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--schema',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=PROJECT/'.dserv/files/projectile-lab/weapon-profiles.json')
    args=parser.parse_args();generate(args.schema,args.output)
