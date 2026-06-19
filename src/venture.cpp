// venture.cpp
// Bone aim + tap-trigger combo (LMB -> Drill Dash -> LMB) + auto-melee

#include <xenon/SDK.hpp>
using namespace xenon;

// ── Config ────────────────────────────────────────────────
static bool  g_enabled        = true;

static float g_stiffness      = 30.f;
static float g_fovRadius      = 200.f;
static bool  g_fovAtTarget    = false;  // draw FOV circle around target instead of crosshair
static float g_switchHysteresis = 2.f;  // a new target must be this much closer (meters) to steal lock
static int   g_targetMode     = 0;      // 0 = closest to crosshair (FOV), 1 = closest in 3D, 2 = lowest HP
static bool  g_priorityOneShot = true;  // override target priority to any enemy at/below shotKillHp
static bool  g_predictMovement = true;  // lead the aim based on target velocity
static float g_predictionScale = 1.0f;  // 0 = no lead, 1 = full lead, >1 = over-lead

// Reaction-turn: when our HP drops, snap aim toward the enemy most likely
// to have shot us (the one whose forward vector points at us most directly).
static bool  g_reactionTurn      = false;
static float g_reactionMinDmg    = 10.f;  // ignore chip damage below this
static float g_reactionDuration  = 0.40f; // how long to lock onto the shooter
static float g_reactionDotMin    = 0.85f; // require they're facing us within ~32°
static float g_reactionPrevHp    = 0.f;
static int32_t g_reactionTargetIdx = -1;
static float g_reactionTimer     = 0.f;
static Hotkey g_triggerKey(18);  // default Left Alt; click-to-bind in menu

static bool  g_autoMelee      = true;
static float g_meleeRange     = 3.f;
static float g_meleeHold      = 0.f;
static float g_meleeHoldSet   = 0.10f;

static float g_dashRange      = 10.f;   // do the combo (LMB-Dash-LMB) within this range
static float g_comboMaxRange  = 18.f;   // hold LMB out to this range (Smart Excavator falloff)
static float g_targetMaxRange = 30.f;   // ignore targets past this
static float g_meleeKillHp    = 40.f;   // if target hp <= this AND in melee range, melee instead of combo
static float g_shotKillHp     = 100.f;  // if target hp <= this, skip the dash and just shoot
static float g_comboMaxHp     = 999.f;  // only combo when target HP is at or below this

// Burrow helper
static bool  g_burrowEscape    = true;
static float g_burrowEscapeHp  = 80.f;  // auto-burrow when own HP <= this
static bool  g_burrowOnReload  = true;  // auto-burrow whenever we're reloading
static bool  g_autoUnburrow    = true;
static float g_unburrowRange   = 4.f;   // radius around our underground pos to count enemies
static float g_unburrowCharge  = 1.0f;  // hold Skill1 this long when emerging (max charge damage)
static int   g_unburrowMinEnemies = 1;  // emerge when AT LEAST this many enemies are in range
static int   g_unburrowMaxEnemies = 1;  // skip emerge if MORE than this many in range (don't 1vX)
static float g_unburrowGraceTime = 1.0f;// stay burrowed at least this long before auto-emerging
static bool  g_comboBurrow     = false; // combo ends with burrow instead of second LMB

// Updated by on_render every frame
static int   g_enemiesInUnburrowRange = 0;
// Tracked in on_frame
static bool  g_prevSkill1Active = false;
static float g_burrowGrace      = 0.f;

static uint64_t g_heroId      = 0;
static bool     g_heroLock    = false;

static const int   kAimBones[]  = {
    Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::BodyBot, Bone::Pelvis,
    Bone::LShoulder, Bone::RShoulder, Bone::LElbow, Bone::RElbow,
    Bone::LHand,     Bone::RHand,     Bone::LPelvis, Bone::RPelvis
};
static const char* kBoneNames[] = {
    "Head", "Neck", "Chest", "Body", "BodyBot", "Pelvis",
    "L Shoulder", "R Shoulder", "L Elbow", "R Elbow",
    "L Hand",     "R Hand",     "L Pelvis", "R Pelvis"
};
static const int   kBoneCount   = 14;
static bool g_boneEnabled[14]   = {
    true, true, true, true, false, true,
    false, false, false, false,
    false, false, false, false
};

// Cached from on_render
static int32_t g_cachedTarget      = -1;
static bool    g_targetValid       = false;
static float   g_targetHp          = 0.f;
static float   g_targetDist        = 9999.f;
static int     g_bestBone          = Bone::Chest;
static int     g_lockMissingFrames = 0;
static float   g_switchCooldown    = 0.f;

// Combo state — just a single timer.
//   timer < 0      → idle
//   timer >= 0     → running, value = seconds since combo started
static float    g_comboTimer       = -1.f;
static float    g_comboCooldown    = 0.f;   // gap between combos
static bool     g_comboMeleeStart  = false; // true: melee→dash→shoot; false: shoot→dash→shoot
static uint32_t g_comboFinisher    = GameButton::LMouse; // last step button (LMouse or Skill1)

// Skill1 pulse manager — sends a clean press+release sequence to trigger burrow/unburrow.
// holdTime controls how long the Skill1 bit stays held; longer hold charges the emerge.
static float g_skill1PulseT   = -1.f;
static float g_skill1HoldTime = 0.18f;
static float g_skill1Cooldown = 0.f;

static void RequestSkill1Pulse(float holdTime = 0.18f)
{
    if (g_skill1PulseT < 0.f && g_skill1Cooldown <= 0.f)
    {
        g_skill1PulseT   = 0.f;
        g_skill1HoldTime = holdTime;
        g_skill1Cooldown = holdTime + 0.25f;  // throttle past the press itself
    }
}

// ── Helpers ───────────────────────────────────────────────
static float Sqrt(float v)
{
    if (v <= 0.f) return 0.f;
    float x = v * 0.5f;
    for (int i = 0; i < 8; i++) x = (x + v / x) * 0.5f;
    return x;
}
static float ScreenDist(Vector2 a, Vector2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return Sqrt(dx*dx + dy*dy);
}
static float WorldDist(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return Sqrt(dx*dx + dy*dy + dz*dz);
}

// ── Plugin Info ───────────────────────────────────────────
XENON_PLUGIN_INFO(
    "venture", "Venture", "Xenon",
    "Bone aim + tap-trigger LMB->Dash->LMB combo + auto-melee.",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ── Lifecycle ─────────────────────────────────────────────
extern "C" void on_load()
{
    g_enabled        = Config::GetBool("enabled",      true);
    g_stiffness      = Config::GetFloat("stiffness",   30.f);
    g_fovRadius      = Config::GetFloat("fovRadius",   200.f);
    g_fovAtTarget    = Config::GetBool("fovAtTarget",  false);
    g_switchHysteresis = Config::GetFloat("switchHysteresis", 2.f);
    g_targetMode     = Config::GetInt("targetMode", 0);
    g_priorityOneShot = Config::GetBool("priorityOneShot", true);
    g_predictMovement = Config::GetBool("predictMovement", true);
    g_predictionScale = Config::GetFloat("predictionScale", 1.0f);
    g_reactionTurn    = Config::GetBool("reactionTurn",   false);
    g_reactionMinDmg  = Config::GetFloat("reactionMinDmg", 10.f);
    g_reactionDuration = Config::GetFloat("reactionDuration", 0.40f);
    g_reactionDotMin  = Config::GetFloat("reactionDotMin",  0.85f);
    g_triggerKey.Load("triggerKey");
    g_autoMelee      = Config::GetBool("autoMelee",     true);
    g_meleeRange     = Config::GetFloat("meleeRange",   3.f);
    g_meleeHoldSet   = Config::GetFloat("meleeHold",    0.10f);
    g_dashRange      = Config::GetFloat("dashRange",    10.f);
    g_comboMaxRange  = Config::GetFloat("comboMaxRange", 18.f);
    g_targetMaxRange = Config::GetFloat("targetMaxRange", 30.f);
    g_meleeKillHp    = Config::GetFloat("meleeKillHp",  40.f);
    g_shotKillHp     = Config::GetFloat("shotKillHp",   100.f);
    g_comboMaxHp     = Config::GetFloat("comboMaxHp",   999.f);
    g_burrowEscape   = Config::GetBool("burrowEscape",   true);
    g_burrowEscapeHp = Config::GetFloat("burrowEscapeHp", 80.f);
    g_burrowOnReload = Config::GetBool("burrowOnReload",  true);
    g_autoUnburrow   = Config::GetBool("autoUnburrow",   true);
    g_unburrowRange  = Config::GetFloat("unburrowRange",  4.f);
    g_unburrowCharge = Config::GetFloat("unburrowCharge", 1.0f);
    g_unburrowMinEnemies = Config::GetInt("unburrowMinEnemies", 1);
    g_unburrowMaxEnemies = Config::GetInt("unburrowMaxEnemies", 1);
    g_unburrowGraceTime  = Config::GetFloat("unburrowGraceTime", 1.0f);
    g_comboBurrow    = Config::GetBool("comboBurrow",    false);
    g_boneEnabled[0]  = Config::GetBool("boneHead",      true);
    g_boneEnabled[1]  = Config::GetBool("boneNeck",      true);
    g_boneEnabled[2]  = Config::GetBool("boneChest",     true);
    g_boneEnabled[3]  = Config::GetBool("boneBody",      true);
    g_boneEnabled[4]  = Config::GetBool("boneBodyBot",   false);
    g_boneEnabled[5]  = Config::GetBool("bonePelvis",    true);
    g_boneEnabled[6]  = Config::GetBool("boneLShoulder", false);
    g_boneEnabled[7]  = Config::GetBool("boneRShoulder", false);
    g_boneEnabled[8]  = Config::GetBool("boneLElbow",    false);
    g_boneEnabled[9]  = Config::GetBool("boneRElbow",    false);
    g_boneEnabled[10] = Config::GetBool("boneLHand",     false);
    g_boneEnabled[11] = Config::GetBool("boneRHand",     false);
    g_boneEnabled[12] = Config::GetBool("boneLPelvis",   false);
    g_boneEnabled[13] = Config::GetBool("boneRPelvis",   false);
    uint32_t lo      = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t hi      = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId         = ((uint64_t)hi << 32) | lo;
    g_heroLock       = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",       g_enabled);
    Config::SetFloat("stiffness",    g_stiffness);
    Config::SetFloat("fovRadius",    g_fovRadius);
    Config::SetBool("fovAtTarget",   g_fovAtTarget);
    Config::SetFloat("switchHysteresis", g_switchHysteresis);
    Config::SetInt("targetMode",     g_targetMode);
    Config::SetBool("priorityOneShot", g_priorityOneShot);
    Config::SetBool("predictMovement", g_predictMovement);
    Config::SetFloat("predictionScale", g_predictionScale);
    Config::SetBool("reactionTurn",   g_reactionTurn);
    Config::SetFloat("reactionMinDmg", g_reactionMinDmg);
    Config::SetFloat("reactionDuration", g_reactionDuration);
    Config::SetFloat("reactionDotMin",  g_reactionDotMin);
    g_triggerKey.Save("triggerKey");
    Config::SetBool("autoMelee",     g_autoMelee);
    Config::SetFloat("meleeRange",   g_meleeRange);
    Config::SetFloat("meleeHold",    g_meleeHoldSet);
    Config::SetFloat("dashRange",    g_dashRange);
    Config::SetFloat("comboMaxRange", g_comboMaxRange);
    Config::SetFloat("targetMaxRange", g_targetMaxRange);
    Config::SetFloat("meleeKillHp",  g_meleeKillHp);
    Config::SetFloat("shotKillHp",   g_shotKillHp);
    Config::SetFloat("comboMaxHp",   g_comboMaxHp);
    Config::SetBool("burrowEscape",  g_burrowEscape);
    Config::SetFloat("burrowEscapeHp", g_burrowEscapeHp);
    Config::SetBool("burrowOnReload", g_burrowOnReload);
    Config::SetBool("autoUnburrow",  g_autoUnburrow);
    Config::SetFloat("unburrowRange", g_unburrowRange);
    Config::SetFloat("unburrowCharge", g_unburrowCharge);
    Config::SetInt("unburrowMinEnemies", g_unburrowMinEnemies);
    Config::SetInt("unburrowMaxEnemies", g_unburrowMaxEnemies);
    Config::SetFloat("unburrowGraceTime", g_unburrowGraceTime);
    Config::SetBool("comboBurrow",   g_comboBurrow);
    Config::SetBool("boneHead",      g_boneEnabled[0]);
    Config::SetBool("boneNeck",      g_boneEnabled[1]);
    Config::SetBool("boneChest",     g_boneEnabled[2]);
    Config::SetBool("boneBody",      g_boneEnabled[3]);
    Config::SetBool("boneBodyBot",   g_boneEnabled[4]);
    Config::SetBool("bonePelvis",    g_boneEnabled[5]);
    Config::SetBool("boneLShoulder", g_boneEnabled[6]);
    Config::SetBool("boneRShoulder", g_boneEnabled[7]);
    Config::SetBool("boneLElbow",    g_boneEnabled[8]);
    Config::SetBool("boneRElbow",    g_boneEnabled[9]);
    Config::SetBool("boneLHand",     g_boneEnabled[10]);
    Config::SetBool("boneRHand",     g_boneEnabled[11]);
    Config::SetBool("boneLPelvis",   g_boneEnabled[12]);
    Config::SetBool("boneRPelvis",   g_boneEnabled[13]);
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; AimResetSmoothing(); }

// ── Frame Logic ───────────────────────────────────────────
extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Venture) return; }  // dormant on any other hero

    g_triggerKey.Update();  // refresh hotkey state once per frame before any key is read

    // ── Skill1 (burrow) pulse manager ─────────────────────
    // Runs every frame so escape-burrow works even when trigger isn't held.
    if (g_skill1Cooldown > 0.f) g_skill1Cooldown -= dt;
    if (g_skill1PulseT >= 0.f)
    {
        g_skill1PulseT += dt;
        if (g_skill1PulseT < g_skill1HoldTime) PressGameButton(GameButton::Skill1);
        else { ReleaseGameButton(GameButton::Skill1); g_skill1PulseT = -1.f; }
    }

    // Detect burrow entry (manual or auto) → arm a grace period so we don't
    // immediately pop the player back up.
    {
        bool s1 = IsSkill1Active();
        if (s1 && !g_prevSkill1Active) g_burrowGrace = g_unburrowGraceTime;
        g_prevSkill1Active = s1;
        if (g_burrowGrace > 0.f) g_burrowGrace -= dt;
    }

    // ── Auto-burrow (low HP escape, or while reloading) ───
    // Don't fire mid-combo — the combo's own button presses would conflict
    // and you'd burrow while trying to shoot/dash.
    if (!IsSkill1Active() && g_comboTimer < 0.f)
    {
        Entity me = LocalPlayer();
        if (me.IsValid() && me.GetHealth() > 0.f)
        {
            bool lowHp    = g_burrowEscape   && me.GetHealth() <= g_burrowEscapeHp;
            bool reloading = g_burrowOnReload && me.IsReloading();
            if (lowHp || reloading) RequestSkill1Pulse();
        }
    }

    bool held = g_triggerKey.IsDown();

    if (g_comboCooldown > 0.f) g_comboCooldown -= dt;

    bool comboActive = (g_comboTimer >= 0.f);
    bool hasTarget   = (held || comboActive) && g_targetValid && g_cachedTarget >= 0;
    bool meleeCanKill = hasTarget && (g_targetDist <= g_meleeRange) && (g_targetHp <= g_meleeKillHp);
    bool shotCanKill  = hasTarget && (g_targetHp <= g_shotKillHp);

    // Nothing pressed and no combo running → release everything and bail.
    if (!held && !comboActive)
    {
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        ReleaseGameButton(GameButton::Melee);
        g_meleeHold = 0.f;
        if (!g_targetValid) AimResetSmoothing();
        // Still update reaction-turn HP tracking even when idle
        Entity meIdle = LocalPlayer();
        if (meIdle.IsValid()) g_reactionPrevHp = meIdle.GetHealth();
        return;
    }

    // ── Reaction-turn: detect incoming damage, find likely shooter ────
    if (g_reactionTurn)
    {
        Entity meR = LocalPlayer();
        if (meR.IsValid())
        {
            float curHp = meR.GetHealth();
            if (g_reactionPrevHp - curHp >= g_reactionMinDmg && curHp > 0.f)
            {
                // We took meaningful damage. Find the enemy whose forward vector
                // points most directly at us — that's our best guess for the shooter.
                Vector3 myPos = meR.GetPosition();
                int   bestIdx = -1;
                float bestDot = g_reactionDotMin;
                for (Entity p : Players())
                {
                    if (p.IsLocal())  continue;
                    if (!p.IsAlive()) continue;
                    if (!p.IsEnemy()) continue;
                    Vector3 ePos = p.GetPosition();
                    Vector3 eFwd = p.GetForward();
                    float dx = myPos.x - ePos.x, dy = myPos.y - ePos.y, dz = myPos.z - ePos.z;
                    float len = Sqrt(dx*dx + dy*dy + dz*dz);
                    if (len <= 0.001f) continue;
                    float dot = (eFwd.x * dx + eFwd.y * dy + eFwd.z * dz) / len;
                    if (dot > bestDot) { bestDot = dot; bestIdx = p.Index(); }
                }
                if (bestIdx >= 0)
                {
                    g_reactionTargetIdx = bestIdx;
                    g_reactionTimer     = g_reactionDuration;
                    AimResetSmoothing();
                }
            }
            g_reactionPrevHp = curHp;
        }
    }

    // While the reaction window is open, force our target/aim onto the shooter.
    if (g_reactionTimer > 0.f)
    {
        g_reactionTimer -= dt;
        Entity rt(g_reactionTargetIdx);
        if (rt.IsValid() && rt.IsAlive())
        {
            if (g_cachedTarget != g_reactionTargetIdx) AimResetSmoothing();
            g_cachedTarget = g_reactionTargetIdx;
            g_targetValid  = true;
            g_targetDist   = WorldDist(LocalPlayer().GetPosition(), rt.GetPosition());
            g_targetHp     = rt.GetHealth();
            // Recompute combo-decision flags now that target changed
            hasTarget    = true;
            meleeCanKill = (g_targetDist <= g_meleeRange) && (g_targetHp <= g_meleeKillHp);
            shotCanKill  = (g_targetHp <= g_shotKillHp);
        }
        else
        {
            g_reactionTimer = 0.f;
        }
    }

    // Aim assist while a target is locked. Predict where the target will be when
    // our projectile arrives (Smart Excavator has projectile travel time).
    if (hasTarget)
    {
        bool aimed = false;
        if (g_predictMovement)
        {
            Entity tgtE(g_cachedTarget);
            WeaponInfo wi{};
            if (tgtE.IsValid()
                && GetWeaponInfo(InputFlag::PrimaryFire, wi)
                && wi.projectileSpeed > 0.f)
            {
                float travel = (g_targetDist / wi.projectileSpeed) * g_predictionScale;
                Vector3 entPos = tgtE.GetPosition();
                Vector3 bone   = tgtE.GetBonePos(g_bestBone);
                Vector3 pred   = tgtE.PredictPosition(travel);
                Vector3 aim;
                aim.x = pred.x + (bone.x - entPos.x);
                aim.y = pred.y + (bone.y - entPos.y);
                aim.z = pred.z + (bone.z - entPos.z);
                AimAtPosition(aim, g_stiffness);
                aimed = true;
            }
        }
        if (!aimed) AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
    }

    // ── Burrowed: skip all firing, optionally auto-unburrow ──
    if (IsSkill1Active())
    {
        if (comboActive) { g_comboTimer = -1.f; comboActive = false; }
        // Emerge when enemy count is in the [min, max] band, AND grace elapsed.
        // Max gate prevents us from auto-emerging into a 1v3 we'd lose.
        bool inBand = (g_enemiesInUnburrowRange >= g_unburrowMinEnemies)
                   && (g_enemiesInUnburrowRange <= g_unburrowMaxEnemies);
        bool emergeNow = inBand && (g_burrowGrace <= 0.f);
        if (g_autoUnburrow && held && emergeNow)
            RequestSkill1Pulse(g_unburrowCharge);  // hold for charge-up damage
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        ReleaseGameButton(GameButton::Melee);
        return;
    }

    // ── Combo start ───────────────────────────────────────
    // Skip the combo entirely (save the drill) when one LMB shot can finish them.
    // In melee range with a tankier target, start with melee instead of LMB.
    if (held && hasTarget && !comboActive && g_comboCooldown <= 0.f
        && !meleeCanKill
        && !shotCanKill
        && g_targetDist <= g_dashRange
        && g_targetHp  <= g_comboMaxHp)
    {
        g_comboTimer       = 0.f;
        g_comboCooldown    = 0.70f;
        g_comboMeleeStart  = (g_targetDist <= g_meleeRange);
        g_comboFinisher    = g_comboBurrow ? (uint32_t)GameButton::Skill1
                                           : (uint32_t)GameButton::LMouse;
        comboActive        = true;
    }

    // ── Combo run ─────────────────────────────────────────
    // Two variants chosen at combo start:
    //   Regular (shoot→dash→shoot):
    //     0.00..0.20 LMB, gap, 0.22..0.37 RMB, gap, 0.41..0.61 LMB
    //   Melee start (melee→dash→shoot) when target was point-blank:
    //     0.00..0.10 Melee, gap, 0.13..0.28 RMB, gap, 0.32..0.52 LMB
    if (comboActive)
    {
        g_comboTimer += dt;
        float t = g_comboTimer;

        if (g_comboMeleeStart)
        {
            if      (t < 0.10f) { PressGameButton(GameButton::Melee); }
            else if (t < 0.13f) { ReleaseGameButton(GameButton::Melee); }
            else if (t < 0.28f) { PressGameButton(GameButton::RMouse); }
            else if (t < 0.32f) { ReleaseGameButton(GameButton::RMouse); }
            else if (t < 0.52f) { PressGameButton(g_comboFinisher); }
            else                { ReleaseGameButton(g_comboFinisher); g_comboTimer = -1.f; }
        }
        else
        {
            if      (t < 0.20f) { PressGameButton(GameButton::LMouse); }
            else if (t < 0.22f) { ReleaseGameButton(GameButton::LMouse); }
            else if (t < 0.37f) { PressGameButton(GameButton::RMouse); }
            else if (t < 0.41f) { ReleaseGameButton(GameButton::RMouse); }
            else if (t < 0.61f) { PressGameButton(g_comboFinisher); }
            else                { ReleaseGameButton(g_comboFinisher); g_comboTimer = -1.f; }
        }
        return;
    }

    // ── Outside combo: continuous LMB fire ────────────────
    // When drill is on cooldown we just hold LMB so the gun keeps shooting.
    if (held && hasTarget && !meleeCanKill && g_targetDist <= g_comboMaxRange)
        PressGameButton(GameButton::LMouse);
    else
        ReleaseGameButton(GameButton::LMouse);
    ReleaseGameButton(GameButton::RMouse);

    // ── Auto-melee ONLY when it can secure the kill ───────
    // (otherwise combo takes priority above)
    if (g_autoMelee && meleeCanKill)
    {
        if (g_meleeHold <= 0.f) g_meleeHold = g_meleeHoldSet;
        if (g_meleeHold > 0.f) { PressGameButton(GameButton::Melee); g_meleeHold -= dt; }
        else                     ReleaseGameButton(GameButton::Melee);
    }
    else { ReleaseGameButton(GameButton::Melee); g_meleeHold = 0.f; }
}

// ── Render (target lock + HUD) ────────────────────────────
extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Venture) return; }  // dormant on any other hero

    // Always-on skill state debug — confirms SDK signals reflect the game state.
    // Burrow Shift, fire LMB until empty (reload), etc. and watch S1 / Reload flip.
    {
        bool s1 = IsSkill1Active();
        bool s2 = IsSkill2Active();
        Entity me = LocalPlayer();
        bool reloading = me.IsValid() && me.IsReloading();
        float hp = me.IsValid() ? me.GetHealth() : 0.f;
        TextBuilder<96> tb;
        tb.put("S1=").put(s1 ? "Y" : "N")
          .put(" S2=").put(s2 ? "Y" : "N")
          .put(" Reload=").put(reloading ? "Y" : "N")
          .put(" HP=").putFloat(hp, 0);
        Color c = s1 ? Color::Green() : (reloading ? Color::Cyan() : Color::Yellow());
        Draw::TextShadow(10.f, 185.f, c, tb.c_str(), 11.f);
    }

    // ── Burrow tactical scan ──────────────────────────────
    // While underground, count enemies in AoE radius around us and check if we're
    // behind our locked target. on_frame uses these to pick a good emerge moment.
    if (IsSkill1Active())
    {
        Entity me0 = LocalPlayer();
        if (me0.IsValid())
        {
            Vector3 lPos = me0.GetPosition();
            int count = 0;
            for (Entity p : Players())
            {
                if (p.IsLocal()) continue;
                if (!p.IsAlive()) continue;
                if (!p.IsEnemy()) continue;
                Vector3 pPos = p.GetPosition();
                float dx = lPos.x - pPos.x, dy = lPos.y - pPos.y, dz = lPos.z - pPos.z;
                float d  = Sqrt(dx*dx + dy*dy + dz*dz);
                if (d <= g_unburrowRange) count++;
            }
            g_enemiesInUnburrowRange = count;
        }
    }
    else
    {
        g_enemiesInUnburrowRange = 0;
    }

    bool held = g_triggerKey.IsDown();
    bool comboActive = (g_comboTimer >= 0.f);

    if (!held && !comboActive)
    {
        g_targetValid       = false;
        g_cachedTarget      = -1;
        g_switchCooldown    = 0.f;
        g_lockMissingFrames = 0;
        return;
    }

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    // ── Target lock — manual loop matching junker_queen's reliable pattern ──
    // Sticky: the cached target auto-wins if it's still alive/visible/in-FOV.
    // Otherwise pick the best NEW candidate by the configured priority mode.
    int32_t bestIdx     = -1;
    float   bestScore   = 99999.f;
    float   bestHp      = 0.f;
    float   bestDist    = 9999.f;
    Entity  bestEnt;
    bool    lockedAlive = false;
    Entity  lockedEnt;

    for (Entity p : Players())
    {
        if (p.IsLocal())    continue;
        if (!p.IsAlive())   continue;
        if (!p.IsEnemy())   continue;
        if (!p.IsVisible()) continue;

        // Use the HOST's own targeting math: closest bone within FOV + its
        // pixel distance to crosshair. Same numbers the game's aim system uses.
        int boneId = p.GetClosestBoneInFov(g_fovRadius);
        if (boneId < 0) continue;       // not in FOV
        float crossSd = p.GetFovTo(boneId);

        float dist = WorldDist(local.GetPosition(), p.GetPosition());
        if (dist > g_targetMaxRange) continue;

        float hp = p.GetHealth();
        // NOTE: no hp cap here on purpose — tanks can still be a valid target,
        // we just won't run the combo on them (combo gate enforces comboMaxHp).

        float score;
        if      (g_targetMode == 1) score = dist;     // closest to me
        else if (g_targetMode == 2) score = hp;       // lowest HP
        else                        score = crossSd;  // closest to crosshair (default)

        // Override: any enemy at/below shot-kill HP gets pushed to the front.
        // Tie-break among one-shot candidates by HP (lowest first) so we always
        // secure the easiest kill.
        if (g_priorityOneShot && hp <= g_shotKillHp)
            score = -100000.f + hp;

        if (score < bestScore)
        {
            bestScore = score;
            bestIdx   = p.Index();
            bestHp    = hp;
            bestDist  = dist;
            bestEnt   = p;
        }
    }

    Entity tgt;
    if (bestIdx >= 0) tgt = bestEnt;
    (void)lockedAlive; (void)lockedEnt;  // (unused — kept for parity with JQ pattern)

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) AimResetSmoothing();
        g_targetValid  = true;
        g_targetHp     = bestHp;
        g_targetDist   = bestDist;
        g_cachedTarget = bestIdx;

        float closestBone = 99999.f;
        g_bestBone = Bone::Chest;
        for (int b = 0; b < kBoneCount; b++)
        {
            if (!g_boneEnabled[b]) continue;
            Vector3 bw = tgt.GetBonePos(kAimBones[b]);
            if (!bw.IsValid()) continue;
            Vector2 bs;
            if (!WorldToScreen(bw, bs)) continue;
            float d = ScreenDist(bs, center);
            if (d < closestBone) { closestBone = d; g_bestBone = kAimBones[b]; }
        }
    }
    else
    {
        if (g_targetValid) AimResetSmoothing();
        g_targetValid  = false;
        g_cachedTarget = -1;
    }

    // ── Draw FOV circle (around crosshair or target) ──────
    Vector2 fovCenter = center;
    if (g_fovAtTarget && g_targetValid && tgt.IsValid())
    {
        Vector3 headW = tgt.GetBonePos(Bone::Head);
        Vector2 headS;
        if (headW.IsValid() && WorldToScreen(headW, headS))
            fovCenter = headS;
    }
    Draw::Circle(fovCenter, g_fovRadius, Color::White(), 1.f);

    // ── Draw target marker ────────────────────────────────
    if (g_targetValid && tgt.IsValid())
    {
        Vector3 headW = tgt.GetBonePos(Bone::Head);
        Vector2 headS;
        if (headW.IsValid() && WorldToScreen(headW, headS))
        {
            Draw::Circle(headS, 10.f, Color::Cyan(), 1.5f);
            Draw::Line(headS.x - 14.f, headS.y, headS.x + 14.f, headS.y, Color::Cyan(), 1.f);
            Draw::Line(headS.x, headS.y - 14.f, headS.x, headS.y + 14.f, Color::Cyan(), 1.f);
        }
    }

    // ── Debug: show every candidate the plugin considers ──
    // Green = winning, Yellow = passed filters but lost. sd = host-computed FOV.
    for (Entity p : Players())
    {
        if (p.IsLocal())    continue;
        if (!p.IsAlive())   continue;
        if (!p.IsEnemy())   continue;
        if (!p.IsVisible()) continue;

        int boneId = p.GetClosestBoneInFov(g_fovRadius);
        if (boneId < 0) continue;
        float crossSd = p.GetFovTo(boneId);
        float dist = WorldDist(local.GetPosition(), p.GetPosition());
        if (dist > g_targetMaxRange) continue;
        float hp = p.GetHealth();

        Vector3 headW = p.GetBonePos(Bone::Head);
        Vector2 headS;
        if (!headW.IsValid() || !WorldToScreen(headW, headS)) continue;

        bool winner = (p.Index() == g_cachedTarget);
        Color c = winner ? Color::Green() : Color::Yellow();
        Draw::Circle(headS, 4.f, c, 1.f);
        TextBuilder<48> tb;
        tb.put("sd=").putFloat(crossSd, 0)
          .put(" d=").putFloat(dist, 1)
          .put(" hp=").putFloat(hp, 0);
        Draw::TextShadow(headS.x + 8.f, headS.y - 6.f, c, tb.c_str(), 9.f);
    }

    // ── HUD ───────────────────────────────────────────────
    float x = 10.f, y = 200.f;
    const float F = 11.f, L = 14.f;
    {
        const char* modeStr = (g_targetMode == 1) ? "CLOSEST" : (g_targetMode == 2) ? "LOW HP" : "CROSSHAIR";
        TextBuilder<48> tb;
        tb.put("-- VENTURE -- mode=").put(modeStr);
        Draw::TextShadow(x, y, Color::Yellow(), tb.c_str(), F); y += L;
    }

    // Combo gate debug — read each condition; the one failing is your problem.
    {
        bool h = g_triggerKey.IsDown();
        bool t = g_targetValid && g_cachedTarget >= 0;
        bool b = IsSkill1Active();
        bool mck = t && (g_targetDist <= g_meleeRange) && (g_targetHp <= g_meleeKillHp);
        bool sck = t && (g_targetHp <= g_shotKillHp);
        bool drng = t && (g_targetDist <= g_dashRange);
        bool hpok = t && (g_targetHp <= g_comboMaxHp);
        bool wouldFire = h && t && !b && (g_comboCooldown <= 0.f) && !mck && !sck && drng && hpok;
        TextBuilder<96> tb;
        tb.put("combo: held=").put(h?"1":"0")
          .put(" tgt=").put(t?"1":"0")
          .put(" burrow=").put(b?"1":"0")
          .put(" cd=").putFloat(g_comboCooldown, 1)
          .put(" mck=").put(mck?"1":"0")
          .put(" sck=").put(sck?"1":"0")
          .put(" rng=").put(drng?"1":"0")
          .put(" hp=").put(hpok?"1":"0");
        Draw::TextShadow(x, y, wouldFire ? Color::Green() : Color::Red(), tb.c_str(), F); y += L;
    }

    if (g_targetValid)
    {
        TextBuilder<64> tb;
        tb.put("TARGET hp=").putFloat(g_targetHp, 0).put(" dist=").putFloat(g_targetDist, 1);
        Draw::TextShadow(x, y, Color::Green(), tb.c_str(), F); y += L;

        const char* state = comboActive ? "COMBO" : "IDLE";
        tb.clear(); tb.put(state).put(" cd=").putFloat(g_comboCooldown, 2);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        // Burrow debug — confirms IsSkill1Active and shows when auto-unburrow would fire
        bool burrowed = IsSkill1Active();
        tb.clear(); tb.put("burrow=").put(burrowed ? "YES" : "no")
            .put(" enemies=").putInt(g_enemiesInUnburrowRange)
            .put(" band=").putInt(g_unburrowMinEnemies).put("-").putInt(g_unburrowMaxEnemies)
            .put(" grace=").putFloat(g_burrowGrace, 2);
        Color bc = burrowed ? Color::Cyan() : Color::White();
        Draw::TextShadow(x, y, bc, tb.c_str(), F); y += L;
    }
}

// ── Menu ──────────────────────────────────────────────────
extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled", &g_enabled);
    if (!g_enabled) return;
    ImGui::Separator();

    g_triggerKey.Render("Trigger Key");
    ImGui::SliderFloat("Smoothing",      &g_stiffness, 0.f, 5000.f);
    ImGui::SliderFloat("FOV Radius",     &g_fovRadius, 10.f, 500.f);
    ImGui::Checkbox("FOV at Target (not crosshair)", &g_fovAtTarget);
    ImGui::SliderFloat("Target Switch Hysteresis (m)", &g_switchHysteresis, 0.f, 10.f);
    ImGui::Combo("Target Priority", &g_targetMode,
                 "Closest to Crosshair\0Closest to Me (3D)\0Lowest HP\0");
    ImGui::Checkbox("Prioritize One-Shot Targets", &g_priorityOneShot);
    ImGui::Checkbox("Predict Movement (lead aim)", &g_predictMovement);
    if (g_predictMovement)
        ImGui::SliderFloat("Prediction Scale", &g_predictionScale, 0.f, 2.f);
    ImGui::Checkbox("Reaction Turn (face shooter when hit)", &g_reactionTurn);
    if (g_reactionTurn)
    {
        ImGui::SliderFloat("Reaction Min Damage",  &g_reactionMinDmg,   1.f, 100.f);
        ImGui::SliderFloat("Reaction Duration (s)", &g_reactionDuration, 0.1f, 2.f);
        ImGui::SliderFloat("Reaction Facing Strictness", &g_reactionDotMin, 0.3f, 0.99f);
    }
    ImGui::SliderFloat("Target Max Range (m)", &g_targetMaxRange, 5.f, 60.f);
    ImGui::Separator();

    ImGui::SliderFloat("Primary Fire Range (m)", &g_comboMaxRange, 1.f, 25.f);
    ImGui::SliderFloat("Dash Range (m)",         &g_dashRange,     1.f, 15.f);
    ImGui::SliderFloat("Combo Max HP",           &g_comboMaxHp,    0.f, 1000.f);
    ImGui::SliderFloat("Shot Kill HP (skip combo if hp <=)", &g_shotKillHp, 0.f, 300.f);
    ImGui::Separator();

    ImGui::Checkbox("Auto Melee (only when it can kill)", &g_autoMelee);
    if (g_autoMelee)
    {
        ImGui::SliderFloat("Melee Range (m)",      &g_meleeRange,   0.5f, 6.f);
        ImGui::SliderFloat("Melee Kill HP (max)",  &g_meleeKillHp,  0.f, 200.f);
    }
    ImGui::Separator();

    ImGui::Text("Burrow Helper");
    ImGui::Checkbox("Burrow at low HP (escape)", &g_burrowEscape);
    if (g_burrowEscape)
        ImGui::SliderFloat("Burrow Escape HP", &g_burrowEscapeHp, 0.f, 300.f);
    ImGui::Checkbox("Burrow while reloading", &g_burrowOnReload);
    ImGui::Checkbox("Auto unburrow", &g_autoUnburrow);
    if (g_autoUnburrow)
    {
        ImGui::SliderFloat("Unburrow Range (m)",        &g_unburrowRange,  1.f, 10.f);
        ImGui::SliderFloat("Unburrow Charge Hold (s)",  &g_unburrowCharge, 0.1f, 4.0f);
        ImGui::SliderInt("Min Enemies in Range",        &g_unburrowMinEnemies, 1, 5);
        ImGui::SliderInt("Max Enemies in Range",        &g_unburrowMaxEnemies, 1, 5);
        ImGui::SliderFloat("Burrow Grace Period (s)",   &g_unburrowGraceTime, 0.f, 5.f);
    }
    ImGui::Checkbox("Combo ends in burrow (offensive)", &g_comboBurrow);
    ImGui::Separator();

    ImGui::Text("Aim Bones:");
    for (int b = 0; b < kBoneCount; b++)
        ImGui::Checkbox(kBoneNames[b], &g_boneEnabled[b]);
    ImGui::Separator();

    TextBuilder<48> hb;
    hb.put("Hero ID: ").putInt((int)g_heroId).put(g_heroId == 0 ? "  (any)" : "  (locked)");
    ImGui::Text(hb.c_str());
    bool prevLock = g_heroLock;
    ImGui::Checkbox("Lock to current hero", &g_heroLock);
    if (g_heroLock != prevLock)
    {
        if (g_heroLock) g_heroId = GetCurrentHero();
        else            g_heroId = 0;
    }
}
