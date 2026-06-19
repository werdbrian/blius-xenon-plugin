# blius-xenon-plugin

Xenon SDK plugins for Overwatch heroes, written in C++ and compiled to WASM
(clang, `wasm32`). Compiled `.wasm` files are in the repo root; the matching C++
source is in [`src/`](src/).

Drop the `.wasm` files into your Xenon plugins load folder and reload in-game.
Each plugin reads/writes its settings via the in-game menu (`on_menu`).

## How the plugins work (shared concepts)

- **Hero gating** — hero plugins stay fully dormant (no aim, no input) unless you're
  playing that hero, so several can be loaded at once without fighting each other.
  Gating is done with `LocalPlayer().GetHeroId()` (the older `GetCurrentHero()` is
  broken and always returns 0).
- **Lifecycle callbacks** — `on_load`/`on_unload` load & persist config, `on_frame`
  runs game logic, `on_render` draws overlays + does screen-space target selection,
  `on_menu` draws the settings panel.
- **Input** — abilities are simulated with `PressGameButton` / `ReleaseGameButton`.
  There's no one-shot "pulse," so taps are done with a short hold window.

---

NOTE: I DID NOT MAKE MOST OF THESE PLUGINS.  I GOT THEM FROM @C. 

## Hero plugins

### `brigitte` — Brigitte
Auto Rocket Flail (melee), auto Whip Shot with target prediction, auto Shield Bash,
auto Barrier block on incoming damage, and Repair Pack auto-heal for low-HP allies.
The Shield Bash is a two-phase action (raise shield on RMouse → bash on LMouse) and
can be prioritized over melee against low-HP targets. The shield is held by a single
unified handler so it never gets re-pressed mid-action.

### `genji` — Genji *(hero-specific)*
**Dash Combo:** one key press runs Swift Strike → re-aim → RMouse → Melee as a state
machine. **Dash Assist:** when you manually dash, it locks the target you were looking
at and optionally auto-shoots/melees after. Includes a point-blank "close aim" upward
nudge, plus tunable FOV, aim bone, delays, and smoothing.

### `mei` — Mei
Auto Ice Block (Cryo-Freeze / Skill1) the moment your HP drops to a configurable
percentage. Single HP-% slider; optional on-screen debug readout.

### `tracer` — Tracer V2 *(hero-specific)*
Aimbot (FOV, smoothing, visible-only, target-lock, max distance) plus a Blink Assist
that keeps aim on target through blinks. Debug HUD shows skill/target state.

### `roadhog` — Roadhog V1 *(hero-specific)*
Hook aimbot — flicks to the best target in FOV when you hook, with configurable flick
time, aim bone, FOV, smoothing, visible-only, and max distance. On-screen hook indicator.

### `venture` — Venture
Bone aim + a tap-trigger combo (LMB → Drill Dash → LMB), auto-melee, and smart Burrow.

### `zenyatta` — Zenyatta
Bone aim + auto-fire (LMB/RMB volley), auto-Discord Orb (Skill2), auto-Harmony Orb
(Skill1), a kick combo, and auto-Transcendence when low on HP.

### `vendetta` — Vendetta
Aim assist + auto-melee, with auto Soaring Slice (E) when a target is in gap-close
range. Uses arc-aim distance compensation so the fixed-distance ability lands on target.

### `junker_queen` — Junker Queen
Auto Commanding Shout on low HP, auto-shoot (held key), auto Carnage blade (key +
conditions), and proximity auto-melee.

### `wuyang` — Wu Yang
Hold LMB to fire a steerable projectile; the plugin holds LMB and steers the projectile
toward your current aim by estimating its world position each frame.

### `hazard` — Hazard
Hold the trigger key: if a target is within slash range and Violent Leap is ready it
leaps (Shift), then auto-executes the slash. (Leap is a two-part move — the plugin only
auto-slashes the second half.)

### `illari` — Illari
Hold the trigger key for aim assist + auto primary fire once the solar rifle charge is
at/above a threshold.

### `zarya` — Zarya
Primary beam aim assist + secondary orb prediction + auto Particle Barrier on incoming
damage.

### `widowmaker` — Widowmaker
Aim assist; target selection runs in `on_render` (where WorldToScreen is valid) and the
cached target is aimed in `on_frame`. Several iteration builds (`widowmaker_v1`–`v4`)
are kept as `.wasm` history.

---

## Utility / multi-hero

### `auto_block` — Auto Block
Reactively pops a defensive ability when an incoming CC/damage threat is detected
(Mei freeze, Tracer pulse, Reaper, Doomfist, Moira, Sigma, etc.). Hero-agnostic.

### `enemy_esp` — Visuals / ESP
Box / skeleton / health-bar / snapline ESP with hero names, distance, distance fading,
visible-only filtering, and a max render distance.

### `enemy_outlines` — Outlines
Applies in-game outline/glow to players with configurable colors.

### `minimap` — Minimap
2D overhead wall map built from raycasts; settled geometry is cached per map ID and
reloaded next session, so it isn't recast every frame.

### `ability_tracker` — Ability Tracker
Per-enemy ability/cooldown indicators color-coded by state (ready / cooldown / active /
ultimate) across a large hero table.

---

## Diagnostics / dev tools

These were used while building the SDK bindings — handy references, not gameplay plugins.

| File | What it does |
|---|---|
| `api_test` | Probes every SDK call and reports LIVE / STUB / FAIL on the HUD |
| `diagnostics` | Continuous health-check of all SDK subsystems (HUD + menu) |
| `live_values` | Dumps raw API values to the HUD every frame |
| `keymap` | Shows which input action index is DOWN as you press buttons |
| `los_test` | Tests whether `IsVisible` / `IsPointVisible` / `Raycast` actually detect walls |
| `ingame_test` | Minimal load test — draws static text to confirm loading works |
| `local_player_test` | Local-player entity API crash-repro harness |
| `math_helpers_lib` | Example *library* plugin (built with `build.bat --library`) |

---

⚠️ Provided as-is for educational/research use.
