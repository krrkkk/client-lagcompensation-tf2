#pragma once
#include "core.h"

namespace rp {
enum class ProjectileKind {Rocket=1,Pipe,Sticky,Flare,Arrow,Bolt,Repair,EnergyBall,EnergyRing,Jar,Milk,Gas,Cleaver,Ball,Ornament,Hook};
struct WeaponSpec {
    int item=0;
    ProjectileKind kind=ProjectileKind::Rocket;
    float speed=1100,gravity=0,damage=90,radius=146,selfPush=1,chargeTime=0,maxSpeed=1100,up=0,hull=0,delay=0;
    int flags=0;
    int weaponId=22;
    const char *model="models/weapons/w_models/w_rocket.mdl";
    bool rocket() const {return kind==ProjectileKind::Rocket;}
    bool physical() const {return kind==ProjectileKind::Pipe||kind==ProjectileKind::Sticky||kind==ProjectileKind::Jar||kind==ProjectileKind::Milk||kind==ProjectileKind::Gas||kind==ProjectileKind::Cleaver||kind==ProjectileKind::Ball||kind==ProjectileKind::Ornament;}
};
}
