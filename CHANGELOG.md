# Changelog

All notable changes to the Xenon Plugin SDK will be documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- **Per-entity cooldowns and durations** — cooldown/duration data is now available on **all hero entities**, not just the local player:
  - `Entity::GetSkill1Cooldown()` / `GetSkill2Cooldown()` / `GetSkill3Cooldown()` / `GetUltCooldown()` — per-entity cooldown state
  - `Entity::GetSkill1Duration()` / `GetSkill2Duration()` / `GetSkill3Duration()` / `GetUltDuration()` — per-entity duration state
  - `PluginEntity` struct now includes `skill1Cd`, `skill1Duration`, `skill2Cd`, `skill2Duration`, `skill3Cd`, `skill3Duration`, `ultCd`, `ultDuration` fields (`PluginCooldown` struct)
- `GetSkill3Duration()` — Skill 3 duration for the local player (was missing from the global API)
- `SkillCooldown` struct moved to `Types.hpp` (was in `Core.hpp`) for use by both `Core.hpp` and `Entity.hpp`
- **Hotkey struct** (`Hotkey.hpp`) — convenience type wrapping key binding, edge detection, toggle, and config persistence:
  - `Update()` — call once per frame for edge detection
  - `IsDown()` / `Pressed()` / `Released()` — level and edge-triggered key state
  - `Toggle(bool&)` — flip a bool on press
  - `Render(label)` — keybind menu widget
  - `Load(key)` / `Save(key)` — config persistence
- **Ability Duration API** — track remaining time on scripted abilities (e.g. Genji Dragonblade, Soldier Visor):
  - `GetSkill1Duration()` / `GetSkill2Duration()` / `GetUltDuration()` — returns `SkillCooldown{current, max, enabled}` where `current` = time remaining, `max` = total duration, `enabled` = ability actively running
  - Hero-specific: duration variables are resolved per-hero using the same allocIdx as cooldowns with varKey `0xD7`
- `WorldToScreenUnclamped(worldPos, screenPos)` — projects a 3D position to screen coordinates without rejecting off-screen results. Use for AABB bounding box projection where corners may extend beyond screen edges.
- `ImGui::ColorPicker(label, color)` — color swatch button that opens a full color picker popup (uses host's `XenonColorButton` widget). Alpha is preserved but not editable in the picker.
- **Skill Cooldown API** — per-skill cooldown state for the local player:
  - `GetSkill1Cooldown()` / `GetSkill2Cooldown()` / `GetSkill3Cooldown()` / `GetUltCooldown()` — returns `SkillCooldown{current, max, enabled}` with `IsOnCooldown()` and `GetPercent()` helpers
  - Hero-specific: cooldown variables are resolved per-hero automatically
- `GetWidowCharge()` — Widowmaker sniper scope charge level
- **Aim Control API** — full aim system for plugins, matching the internal aimbot's mechanics:
  - `AimSetDirection(dir)` — instantly set the view direction vector
  - `AimGetDirection()` — read the current view direction vector
  - `AimSetAngles(pitch, yaw)` — instantly set view angles in radians
  - `AimGetAngles()` — read current view angles (pitch, yaw) in radians
  - `AimAtPosition(target, stiffness)` — aim at a world position with critically-damped spring smoothing
  - `AimAtBone(entityIndex, boneId, stiffness)` — aim at an entity's bone with spring smoothing
  - `AimResetSmoothing()` — reset spring velocity (call on target switch)
  - `AimHitsHitbox(entityIndex, hitboxScale)` — check if current aim intersects entity hitboxes (returns BodySlot or -1)
- `atan2f(y, x)` — math helper for angle calculations (was missing from freestanding WASM)

### Changed
- `aim_to` / `aim_to_bone` host bindings are now functional (were stubs). Third arg is spring stiffness (0 = instant).
- **Breaking:** Renamed skill fields and functions for accuracy:
  - `PluginEntity.skill1Cd` / `skill2Cd` / `skillECd` → `skill1Active` / `skill2Active` / `skill3Active` (these are active-state flags, not cooldowns)
  - `GetSkill1Cooldown()` / `GetSkill2Cooldown()` / `GetSkillECooldown()` → `IsSkill1Active()` / `IsSkill2Active()` / `IsSkill3Active()` (now return `bool`)
  - WASM imports renamed: `get_skill1_cooldown` → `is_skill1_active`, `get_skill2_cooldown` → `is_skill2_active`, `get_skill_e_cooldown` → `is_skill3_active`

### Added (previous)
- `Entity::GetTotalHealth()` — alias for `GetHealth()` (total HP including all pools)
- `Entity::IsFullHealth()` — returns true if health >= maxHealth
- `Entity::GetOverhealth()` — returns current overhealth amount
- `Entity::GetArmor()` — returns current armor
- `Entity::GetBarrier()` — returns current barrier
- `Entity::GetForward()` — returns the entity's forward-facing direction vector
- `Entity::IsTargetable()` — returns true if entity can be targeted (not invulnerable/phased)
- `Entity::IsReloading()` — returns true if the entity is currently reloading
- `Entity::GetHitboxCount()` — returns the number of hitboxes for this entity
- `Entity::GetHitbox(index, out)` — retrieves a single hitbox by index
- `Entity::GetHitboxes(out, maxCount)` — retrieves all hitboxes into a buffer, returns count
- `Entity::GetLerpHistory(out, maxCount)` — retrieves server tick position history (newest first)
- `Hitbox` struct — hitbox data with boneIndex, radius, bodySlot, isCapsule, worldPos, capsuleEnd
- `LerpEntry` struct — tick + position pair for lerp history
- `BodySlot` namespace — body region classification constants (Head, Neck, Body, Chest, etc.)
- `PluginEntity.overhealth` — overhealth field in raw entity data
- `PluginEntity.hitboxCount` — hitbox count in raw entity data
- `PluginEntity.isTargetable` — targetability flag in raw entity data
- `PluginEntity.isReloading` — reload state flag in raw entity data
- `PluginEntity.forward` — forward direction vector in raw entity data
- `WeaponInfo.reloading` — explicit reload state in weapon info
- `WeaponInfo.skillBlocked` — ability-blocked state in weapon info

### Added (previous)
- `GetCameraPosition()` — returns world-space camera position (extracted from VP matrix)
- `GetCameraForward()` — returns normalized camera forward direction (derived from VP matrix)
- `GetViewMatrix(float* out16)` — copies the raw 4x4 view-projection matrix (row-major)
- `GetUltCharge()` — returns local player ult charge percentage (0-100)
- `IsUltReady()` — returns true if ult charge >= 100%
- `IsUltActive()` — returns true if ultimate is currently active
- `IsSkill1Active()` / `IsSkill2Active()` / `IsSkill3Active()` — returns true if skill is active (renamed from `GetSkill*Cooldown`)
- `GetHeroState()` — returns bitmask with `HeroState::Ulting` (0x1) and `HeroState::Reloading` (0x2)
- `GetRailgunCharge()` / `GetIllariCharge()` / `GetHanzoCharge()` — hero-specific charge values
- `GetLookupSkill(lookupId)` — generic skill value lookup by ID
- `SkillId` namespace with known lookup IDs (`UltCharge`, `SojournCharge`, `IllariCharge`, `HanzoCharge`, `SombraAmmo`)
- `HeroState` namespace with bitmask flags (`Ulting`, `Reloading`)
- `Entity::GetUltCharge()`, `IsUltActive()`, `IsSkill1Active()`, `IsSkill2Active()`, `IsSkill3Active()` accessors
- `GetWeaponInfo(inputFlags, out)` — query local player weapon state (valid, useable, shootable, hasGravity, projectile speed)
- `WeaponInfo` struct with `hasGravity` field and `InputFlag` constants (`PrimaryFire`, `SecondaryFire`, `ScopedShoot`, `Skill1`, `Skill2`, `Ultimate`, `PrimaryRelease`)
- `Color::Lerp(a, b, t)` — linear interpolation between colors
- `Color::HealthGradient(percent, alpha)` — red→yellow→green health color
- `Vector3::RotatedY(yaw)` — rotate vector around Y axis
- `Draw::TextShadow()` — text with 1px shadow for readability
- `Draw::TextCentered()` — roughly centered text with shadow
- `Draw::RectCorners()` — tactical corner bracket rectangle
- `ImGui::ColorSliders(label, color)` — 4-slider RGBA color picker
- `Config::GetColor(key, default)` / `Config::SetColor(key, color)` — load/save Color as 4 int keys
- `IsIngame()` — check if currently in a match
- `GetMapId()` — current map identifier
- `GetSensitivity()` — in-game mouse sensitivity
- `LogDebug(msg)`, `LogWarning(msg)`, `LogError(msg)` — log level wrappers in Core.hpp
- `GetCurrentHero()` — returns the local player's current hero pool ID
- `Entity::GetBoundsMin()` / `Entity::GetBoundsMax()` — AABB accessor aliases (same as GetDelta1/GetDelta2)
- `Raycast(from, to)` — world-space line trace, returns `RaycastResult` with hit position, fraction, and hit flag
- `IsPointVisible(from, to)` — quick LOS check between two world points (fraction > 0.98 = visible)
- `IsRaycastReady()` — check if the native raycast system is initialized
- `RaycastResult` struct with `IsHit()` and `IsVisible()` helpers
- `GameButton` namespace — game-level button constants (LMouse, RMouse, Jump, Crouch, Skill1, Skill2, Ult, Reload, Melee, etc.)
- `GetClientPing()` / `GetServerPing()` — query current network latency in ms
- `PressGameButton(bit)` / `ReleaseGameButton(bit)` — inject game-level button inputs

### Changed
- `PluginEntity.ultCharge` now returns real ult charge for the local player (was always 0)
- `PluginEntity.ultActive` now returns real ult active state (was always 0)
- `PluginEntity.skill1Active/skill2Active/skill3Active` `.x` now returns 1.0 when skill is active (was always 0; renamed from `skill1Cd/skill2Cd/skillECd`)
- `GetCrosshairPosition()` now returns screen center instead of `(0, 0)`
- `IsAimKeyDown()` now checks right mouse button instead of returning false
- `enemy_esp.cpp` — refactored to use SDK drawing helpers (removed ~55 lines of local boilerplate)
- `enemy_outlines.cpp` — refactored to use `Color`, `ImGui::ColorSliders`, `Config::GetColor/SetColor`
- `enemy_glow_outlines.cpp` — refactored to use `Color`, `Config::GetColor/SetColor`
- Clarified `SetOutlineGlow` comment — it is equivalent to `SetOutline`, uses the glow import path internally

## [1.0.0] — 2026-02-28

Initial documented release. SDK headers, build system, and examples.

### Core
- `Log`, `GetTime`, `IsKeyDown`, `ScreenSize`, `ScreenCenter`, `WorldToScreen`

### Entity
- `Entity` class with full state queries (health, position, bones, visibility, team)
- `Players()` range iterator, `LocalPlayer()`, `GetPlayer()`, `GetPlayerCount()`
- `FindBestTarget(flags)`, `FindBestTargetInFov(fov, bone, flags)`
- `CalcAngle`, `GetHeroName`, `PredictPosition`, `GetFovTo`, `GetScreenOffsetTo`, `GetClosestBoneInFov`
- `SetOutline`, `SetOutlineGlow` + Visible/Occluded shorthands
- `TargetFlags` namespace (Enemy, Team, Visible, LowHP)

### Drawing
- `Draw::Line`, `Circle`, `CircleFilled`, `Rect`, `RectFilled`, `Text`, `HealthBar`

### ImGui
- `Checkbox`, `SliderFloat`, `SliderInt`, `Combo`, `CollapsingHeader`, `Separator`, `Hotkey`

### Config
- `GetBool`, `GetFloat`, `GetInt`, `SetBool`, `SetFloat`, `SetInt`, `Save`

### Types
- `Vector2`, `Vector3`, `Color`, `Team`, `EntityType`
- `Bone` namespace (actual game skeleton indices)
- `HeroId` namespace (all heroes through Jetpack Cat)
- `VK` key constants, `OutlineType`, `PluginFlags`

### Build
- `build.bat` — single-file and batch compilation
- `--library` flag for library plugins
- Dependency system with `XENON_PLUGIN_INFO_DEPS`
