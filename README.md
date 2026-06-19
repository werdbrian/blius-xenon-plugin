# blius-xenon-plugin

Compiled Xenon SDK WASM plugins for Overwatch heroes (clang, wasm32).

Drop the `.wasm` files into your Xenon plugins load folder and reload in-game.

NOTE: I DID NOT MAKE MOST OF THESE PLUGINS.  I GOT THEM FROM @C. 

## Hero plugins

Each hero plugin is **hero-gated** — it stays fully dormant (no aim, no input) unless you're
playing that hero, so several can be loaded at once without interfering.

| Plugin | Hero | Notes |
|---|---|---|
| `zenyatta.wasm` | Zenyatta | Orbs, Discord, kick combo, auto Transcendence on low HP |
| `brigitte.wasm` | Brigitte | Auto melee, Whip Shot prediction, Shield Bash combo, auto block, repair |
| `venture.wasm` | Venture | Bone aim, Drill Dash combo, auto melee, smart Burrow |
| `vendetta.wasm` | Vendetta | Combat + arc-aim ability |
| `junker_queen.wasm` | Junker Queen | Combat automation |
| `wuyang.wasm` | Wuyang | Combat automation |
| `mei.wasm` | Mei | Auto Ice Block (Cryo-Freeze) at a configurable HP % |
| `genji.wasm` | Genji | |
| `hazard.wasm` | Hazard | |
| `illari.wasm` | Illari | |
| `roadhog.wasm` | Roadhog | |
| `tracer.wasm` | Tracer | |
| `widowmaker.wasm` | Widowmaker | (plus `widowmaker_v1`–`v4` iterations) |
| `zarya.wasm` | Zarya | |

## Utility / multi-hero

| Plugin | Purpose |
|---|---|
| `auto_block.wasm` | Reactive defense — auto-pops a defensive ability vs incoming threats (Mei/Tracer/Reaper/Doom/Moira/Sigma) |
| `enemy_esp.wasm` / `enemy_outlines.wasm` | Enemy ESP / outlines |
| `minimap.wasm` | Minimap overlay |
| `ability_tracker.wasm` | Enemy ability/cooldown tracker |

## Diagnostics / dev

`los_test`, `diagnostics`, `api_test`, `ingame_test`, `live_values`, `local_player_test`,
`keymap`, `math_helpers_lib` — test/diagnostic plugins used during development.

---

⚠️ Provided as-is for educational/research use.
