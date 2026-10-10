# changes

## 0.4.1

- applied the same 90-degree model alignment to the remaining projectile families.
- fixed pipes and stickies dropping at your feet: catch-up read a zero entity velocity instead of their physics velocity.
- fixed subsequent velocity updates for bouncing and flying physics projectiles.

## 0.4.0

- fixed rocket slowdown when the server cannot advance it.
- fixed empty model bounds and hiding the native rocket before its replacement draws.
- added immediate local exhaust and preloading of projectile models.
- rockets no longer stop on dropped weapons and debris in the local trace.
- added more projectile families, charge speed, basic bounces and server state updates.
- fixed duplicate projectile registration and preserved the native self-jump force of rocket launcher variants.
- added recent player/door collision history and replay after jump corrections.
- kept damage, hit/kill sounds and other players' effects authoritative.
