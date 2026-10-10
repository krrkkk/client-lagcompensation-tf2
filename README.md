[русский](README.ru.md)

# projectile lab

experimental tf2 projectile prediction. client + server 0.4.1, by avxgroup.

fire locally, let the server confirm it. clear flight can catch up by up to 100 ms.
damage, health, hitsounds and kills stay with the server.

## install

1. close tf2. copy `tf` from the windows archive into `Team Fortress 2`, replacing the old files.
2. launch tf2 with `-insecure -console`.
3. enter `exec projectile_lab`.

connects to `185.137.93.189:27018`. choose an arena with `!add`.
the dll uses `plugin_load`, no injector or automatic startup. sourcemod is only needed on the server.

- `cl_projectile_prediction 0` — off.
- `cl_projectile_prediction 1` — on.
- `cl_projectile_prediction` — status.

close tf2 and remove `-insecure` to return to normal play.

## 0.4.1 fixes

- corrected the 90-degree model turn for the other projectile families.
- fixed pipes, stickies and other physics projectiles losing their launch speed during catch-up.
- server updates now read their actual physics velocity, including after bounces.

## included

- immediate local rockets and exhaust, corrected model bounds and smoother server confirmation.
- rocket launcher variants, pipes, stickies, flares, arrows, healing/repair bolts, energy shots, jars, milk, gas, cleavers, balls and the grappling hook.
- gravity, charge speed, basic bouncing, sticking and server position updates.
- predicted rocketjump push, command replay and server corrections.
- collision checks against the map and recent player/door positions before fast-forwarding.

this is a lab build. grenade bounces, random spread and custom attributes can still differ from the server.
sticky/flare jumps, spells, sentry rockets and unusual projectiles keep native behavior; full local prediction for those is unfinished.
there is no early damage number or fake hit/kill sound. confirmed damage uses the normal tf2 hud.

## server

install [metamod](https://www.sourcemm.net/) and [sourcemod](https://www.sourcemod.net/),
then copy `tf` from the server archive into the server folder. start with `-insecure -port 27018`.
set `sm_rp_max_advance 0.100` and `sm_rp_advance 1` in your server config.
`sm_rp_status` prints status in the server console.

the shipped client only activates on the lab address or `127.0.0.1:27018`.
for your own remote server, change the address in `allowedServer()` and `config/projectile_lab.cfg`, then rebuild.
the server helper refuses other ports or a launch without `-insecure`.

## build

needs python 3, g++, clang 19+, lld, Microsoft CRT/Windows SDK files from xwin,
and [Valve's SDK](https://github.com/ValveSoftware/source-sdk-2013/tree/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0).
use the pinned commit. Microsoft C++ ABI and RTTI are required.

```sh
python3 tools/build.py --client-only --windows-sdk /path/to/xwin --source-sdk /path/to/source-sdk-2013
```

for the server too, remove `--client-only` and add `--sdk /path/to/sourcemod/scripting`.
output: `.dserv/tmp/projectile-prediction/build`. generated weapon profiles are included.

optional profile refresh:

```sh
python3 tools/weapons.py --schema /path/to/tf/scripts/items/items_game.txt
```

## logs

`tf/logs/projectile_prediction.log` records loading, shots and confirmation timing.
`tf/logs/projectile_prediction_exceptions.log` records crashes seen by the module.
see [changes](CHANGELOG.md) and [dependencies](THIRD_PARTY.md).
