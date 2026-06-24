# blius-xenon-plugin

Xenon SDK plugins for Overwatch heroes, written in C++ and compiled to WASM
(clang, `wasm32`). Compiled `.wasm` files are in the repo root; the matching C++
source is in [`src/`](src/).

Drop the `.wasm` files into your Xenon plugins load folder and reload in-game.
Each plugin reads/writes its settings via the in-game menu (`on_menu`).

> **Status key:** ✅ working / tested · 🟡 early version, may be unfinished or untested.

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
- **Keybinds** — most plugins use a click-to-bind keybind widget: click the keybind in
  the menu, then press the key/mouse button you want and it captures it (no key numbers).

---

NOTE: I DID NOT MAKE MOST OF THESE PLUGINS.  I GOT THEM FROM @C.

## Hero plugins

### `brigitte` — Brigitte ✅
- **Auto Rocket Flail** — auto-melees (LMB) the aim target while the trigger key is held
  and a valid target is in melee range.
- **Auto Whip Shot** — fires Whip Shot (Skill1) at mid-range targets with travel-time
  prediction; configurable min/max range so it doesn't waste the cooldown point-blank.
- **Shield Bash** — two-phase action: raise Barrier (RMouse) → bash (LMB while shield is
  up). Can be prioritized over melee against low-HP targets, and is skipped when too many
  enemies are grouped around the target (configurable). Shield is driven by one unified
  handler so RMB is held continuously and never re-pressed mid-bash.
- **Auto Block** — raises Barrier automatically when you take incoming damage, and turns
  to face the attacker.
- **Repair Pack** — auto-throws Repair Pack (Skill2) to the lowest-HP ally below a
  per-role HP threshold (separate tank vs. DPS/support cutoffs) within range.
- **Targeting / overlay** — target modes (closest distance / lowest HP / closest to
  crosshair), FOV circle, and an optional on-screen debug overlay.

### `mei` — Mei ✅ (only ice block % logic)
- **Auto Ice Block** — pops Cryo-Freeze (Skill1) the instant your HP drops to/below a
  configurable percentage. Single HP-% slider.
- **Debug overlay** — optional on-screen readout of HP, cooldown, and active state.

### `venture` — Venture✅ 
- **Bone aim** — aim assist onto a selected bone while the trigger is held.
- **Drill Dash combo** — a tap-trigger sequence (LMB → Drill Dash → LMB).
- **Auto-melee** + **smart Burrow** usage.

### `zenyatta` — Zenyatta 🟡
- **Bone aim + auto-fire** — aims and fires the LMB/RMB orb volley.
- **Auto Discord Orb** (Skill2) onto the current target and **auto Harmony Orb** (Skill1)
  onto allies.
- **Kick combo** for close-range melee.
- **Auto Transcendence** — fires the ultimate automatically when your HP gets low.

### `vendetta` — Vendetta 🟡
- **Aim assist + auto-melee** (LMB) while the trigger is held.
- **Auto Soaring Slice (E)** when a target is in gap-close range, using arc-aim distance
  compensation so the fixed-distance ability lands on target instead of overshooting.

### `junker_queen` — Junker Queen  ✅ 
- **Auto Commanding Shout** when your HP is low.
- **Auto-shoot** while the trigger key is held.
- **Auto Carnage blade** when the key is held and conditions are met.
- **Proximity auto-melee.**

### `wuyang` — Wu Yang - untested
- **Steerable projectile** — hold LMB to fire and steer the projectile toward your current
  aim; the plugin holds LMB and re-estimates the projectile's world position each frame to
  keep it tracking.

### `hazard` — Hazard - 🟡
- **Auto Violent Leap + slash** — hold the trigger key; if a target is within slash range
  and Violent Leap is ready, it leaps (Shift) and then auto-executes the slash. Leap is a
  two-part move, so the plugin only auto-fires the second (slash) half — you initiate the
  leap.

### `illari` — Illari ✅ 
- **Aim assist + auto primary fire** — hold the trigger key; auto-fires once the solar
  rifle charge is at/above a configurable threshold so shots aren't wasted under-charged.
  -auto melee

### `zarya` — Zarya
- **Primary beam aim assist.**
- **Secondary orb prediction** — leads moving targets with the projectile.
- **Auto Particle Barrier** on incoming damage.

### `genji` — Genji V1  ✅ 
- **Dash Combo** — one key press runs the full sequence as a state machine: snap aim →
  Swift Strike (Skill1) → wait for the dash to finish → re-aim the selected bone → RMouse
  → Melee.
- **Dash Assist** — when *you* manually dash, it locks the target you were looking at and
  optionally auto-RClicks / auto-melees after the dash lands.
- **Close Aim Adjust** — nudges aim upward at point-blank range (optionally scaled by
  distance with a falloff curve).
- Tunable FOV, aim bone (head/neck/chest/closest), shoot/melee delays, max distance, and
  smoothing.
- 🟡 Early V1 import — not fully tested.

### `tracer` — Tracer V2 🟡
- **Aimbot** — FOV, smoothing, visible-only, target-lock, and max-distance options.
- **Blink Assist** — keeps aim on the locked target through blinks (separate blink/turn
  smoothing).
- **Debug HUD** showing skill/target state.
- 🟡 Early V2 import — not fully tested.

### `roadhog` — Roadhog V1 ✅ 
- **Hook Aimbot** — flicks to the best target in FOV when you hook, with configurable
  flick time, aim bone, FOV, smoothing, visible-only, and max distance.
- **Hook indicator** — on-screen indicator with adjustable padding.
- 🟡 Early V1 import — not fully tested.

### `widowmaker` — Widowmaker 🟡
- **Aim assist** — target selection runs in `on_render` (where WorldToScreen is valid) and
  the cached target is aimed in `on_frame`.
- 🟡 Multiple iteration builds (`widowmaker_v1`–`v4`) are kept as `.wasm` history; these
  are work-in-progress and not all are tested.

---

## Utility / multi-hero

### `auto_block` — Auto Block untestted
- Reactively pops a defensive ability when an incoming CC/damage threat is detected
  (Mei freeze, Tracer pulse bomb, Reaper, Doomfist, Moira, Sigma, etc.). Hero-agnostic, so
  it runs on any hero with a relevant defensive ability.

### `enemy_esp` — Visuals / ESP untestted
- Box / skeleton / health-bar / snapline ESP, with hero names, distance text, dynamic
  distance fading, visible-only filtering, and a max render distance.

### `enemy_outlines` — Outlines untestted
- Applies in-game outline/glow to players, with configurable colors.

### `minimap` — Minimap untestted
- 2D overhead wall map built from raycasts; settled geometry is cached per map ID and
  reloaded next session, so it isn't recast every frame.

### `ability_tracker` — Ability Tracker untestted
- Per-enemy ability/cooldown indicators, color-coded by state (ready / cooldown / active /
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

⚠️ Provided as-is for educational/research use. Plugins marked 🟡 are early/untested.
