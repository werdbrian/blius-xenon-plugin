// vendetta.cpp
// Hold trigger key: aim assist + auto melee (LMouse)
// Auto Soaring Slice (E) when target in gap-close range
// Auto Whirlwind Dash (Shift) at melee range
// Auto Warding Stance (RMouse) when HP drops — also fires Projected Edge if LMouse held

#include <xenon/SDK.hpp>
using namespace xenon;

static bool    g_enabled      = true;
static Hotkey  g_triggerKey(18);  // default Left Alt; click-to-bind in menu
static float   g_stiffness    = 30.f;
static float   g_eArcComp     = 0.05f;  // height per meter inside max range (tunes sword arc landing)
static float   g_fovRadius    = 200.f;
static int     g_targetMode   = 0;    // 0=closest crosshair, 1=lowest HP
static float   g_hitboxScale  = 1.0f;
static bool    g_autoShoot    = true;

// Soaring Slice (E / Skill2) — gap closer
static bool    g_autoSlice    = true;
static float   g_sliceMinDist = 6.f;
static float   g_sliceMaxDist    = 14.f;  // Soaring Slice max range is 15m
static float   g_sliceVertThresh = 15.f; // below this distance, aim straight up instead of arc

// Whirlwind Dash (Shift / Skill1) — fires on kill confirmation
static bool    g_autoDash          = true;
static float   g_postDashDelay     = 2.0f;  // suppress all inputs for this long after dash
static float   g_postDashSuppressEnd = 0.f;

// Kill thresholds — abilities only fire when target HP is low enough to secure the kill
static float   g_abilityKillHp = 400.f;  // Soaring Slice (leads to 120 overhead crit)
static float   g_dashKillHp    = 70.f;   // Whirlwind Dash max damage is 70 at center

// Auto melee range — engage LMouse + RMouse without trigger key when this close
static bool    g_autoMeleeRange_en = true;
static float   g_meleeAutoRange    = 5.f;

// Auto block (Warding Stance / RMouse)
static bool    g_autoBlock       = true;
static float   g_blockHpDrop     = 5.f;   // HP drop per frame to trigger block
static float   g_blockDuration   = 0.5f;  // how long to keep block active after hit detected
static float   g_blockHoldEnd    = 0.f;
static bool    g_blockOn         = false; // current toggle state (on/off)
static float   g_blockPressEnd   = 0.f;  // brief press timer for toggle
static float   g_prevHp          = -1.f;

// Hold timers — press+hold pattern for reliable SDK input
static float   g_sliceHoldEnd     = 0.f;
static float   g_dashHoldEnd      = 0.f;
static float   g_lastSliceAt      = 0.f;   // when Soaring Slice last fired
static float   g_downstrikeRange  = 5.f;   // max dist to target for downstrike to fire
static float   g_downstrikeMinHeight = 0.3f; // how many meters above target feet to be considered airborne
static bool    g_downstrikeReady  = false; // set by on_render: above + in range + facing
static constexpr float kHoldDur        = 0.12f;
static constexpr float kOverheadWindow = 1.5f;

// Continuous beam spam below HP threshold (no cooldown, just hold duration gap)
static bool    g_beamSpam      = true;
static float   g_beamSpamHp    = 150.f;        // spam beam when target HP <= this
static float   g_beamSpamMin   = 6.f;          // beam spam min range (m)
static float   g_beamSpamMax   = 20.f;         // beam spam max range (m)

// Simulated PE charge tracker (SDK doesn't expose PE charges)
static int     g_peMaxCharges    = 3;
static float   g_peRechargeTime  = 2.5f;        // seconds per charge recharge
static int     g_peCharges       = 3;            // current simulated charge count
static float   g_peRechargeAt[4] = {};           // when each spent charge finishes recharging
static float   g_peBlockPauseAt  = 0.f;          // when Warding Stance turned on (pauses recharge)
static int     g_peReserve       = 2;             // minimum charges to keep in reserve
static float   g_peRechargeDelay = 0.75f;         // seconds after use before regen starts
static bool    g_prevPeCombo     = false;          // rising-edge detection for user-fired beams
static float   g_lastBeamSpamAt  = -99.f;

// Opener combo: beam (Projected Edge = RMouse+LMouse) → Soaring Slice (E)
static bool    g_autoOpener    = true;
static float   g_beamKillHp    = 150.f;        // also beam when target HP is this low (poke/kill)
static float   g_beamHoldEnd   = 0.f;
static float   g_lastBeamAt    = -99.f;
static constexpr float kBeamHold        = 0.12f;
static constexpr float kBeamToSlice     = 0.15f;  // delay between beam and E
static constexpr float kBeamCooldown    = 3.0f;   // opener cooldown
static constexpr float kPokeCooldown    = 1.0f;   // poke/kill beam cooldown
static constexpr float kOverheadStrikeAt  = 0.1f;  // start pressing LMB shortly after E fires
static constexpr float kPostOverheadMelee = 0.5f;  // auto-melee window after overhead ends

// Quick melee follow-up after overhead swing
static float g_quickMeleeEnd = 0.f;

// Hero lock
static uint64_t g_heroId   = 0;
static bool     g_heroLock = false;

// Aim bones
static const int   kAimBones[]  = { Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis };
static const char* kBoneNames[] = { "Head", "Neck", "Chest", "Body", "Pelvis" };
static const int   kBoneCount   = 5;
static bool        g_boneEnabled[5] = { true, true, true, true, true };

// Cached from on_render
static int32_t g_cachedTarget       = -1;
static bool    g_targetValid        = false;
static float   g_targetHp          = 0.f;
static float   g_targetDist        = 9999.f;
static Vector3 g_targetPos         = {};
static int     g_cachedHitbox      = -1;
static bool    g_eOnCooldown       = false;
static int     g_bestBone          = Bone::Chest;
static bool    g_prevTargetAlive   = false;
static bool    g_killDetected      = false;
static bool    g_enemyInMeleeRange = false; // any enemy within melee range, regardless of FOV

// Group safety — don't engage if too many enemies near target
static bool    g_groupSafetyEn  = false;
static int     g_groupMin       = 2;      // suppress if >= this many enemies nearby
static float   g_groupRange     = 5.f;   // radius around target (m) to count
static int     g_nearbyCount    = 0;     // updated each on_render

// Melee debug snapshot — captured in on_frame, drawn in on_render
static bool    g_showMeleeDebug      = false;
static bool    g_dbgHeld             = false;
static bool    g_dbgBlockPressing    = false;
static bool    g_dbgBeamPressing     = false;
static bool    g_dbgInMeleeRange     = false;
static bool    g_dbgEnemyInMelee     = false;
static bool    g_dbgOverheadPending  = false;
static bool    g_dbgPostOverhead     = false;
static bool    g_dbgDownstrikeReady  = false;
static bool    g_dbgShouldSwing      = false;

// ── helpers ───────────────────────────────────────────────────────────────────

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

// ── plugin info ───────────────────────────────────────────────────────────────

XENON_PLUGIN_INFO(
    "vendetta", "Vendetta", "Xenon",
    "Aim assist, auto melee, auto Soaring Slice (E), auto Whirlwind Dash (Shift).",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ── lifecycle ─────────────────────────────────────────────────────────────────

extern "C" void on_load()
{
    g_enabled       = Config::GetBool("enabled",        true);
    g_triggerKey.Load("triggerKey");
    g_stiffness     = Config::GetFloat("stiffness",      30.f);
    g_eArcComp      = Config::GetFloat("eArcComp",       0.05f);
    g_fovRadius     = Config::GetFloat("fovRadius",      200.f);
    g_targetMode    = Config::GetInt("targetMode",       0);
    g_hitboxScale   = Config::GetFloat("hitboxScale",    1.0f);
    g_autoShoot     = Config::GetBool("autoShoot",       true);
    g_autoSlice     = Config::GetBool("autoSlice",       true);
    g_sliceMinDist  = Config::GetFloat("sliceMinDist",   6.f);
    g_sliceMaxDist    = Config::GetFloat("sliceMaxDist",     14.f);
    g_sliceVertThresh = Config::GetFloat("sliceVertThresh",  15.f);
    g_groupSafetyEn = Config::GetBool("groupSafetyEn",    true);
    g_groupMin      = Config::GetInt("groupMin",           2);
    g_groupRange    = Config::GetFloat("groupRange",       5.f);
    g_beamSpam         = Config::GetBool("beamSpam",         true);
    g_beamSpamHp       = Config::GetFloat("beamSpamHp",     150.f);
    g_beamSpamMin      = Config::GetFloat("beamSpamMin",     6.f);
    g_beamSpamMax       = Config::GetFloat("beamSpamMax",      20.f);
    g_peMaxCharges      = Config::GetInt("peMaxCharges",        3);
    g_peRechargeTime    = Config::GetFloat("peRechargeTime",    3.5f);
    g_peRechargeDelay   = Config::GetFloat("peRechargeDelay",   0.75f);
    g_peReserve         = Config::GetInt("peReserve",           2);
    g_peCharges         = g_peMaxCharges;
    g_autoOpener    = Config::GetBool("autoOpener",      true);
    g_beamKillHp    = Config::GetFloat("beamKillHp",     150.f);
    g_autoDash          = Config::GetBool("autoDash",            true);
    g_abilityKillHp     = Config::GetFloat("abilityKillHp",     400.f);
    g_dashKillHp        = Config::GetFloat("dashKillHp",        70.f);
    g_postDashDelay     = Config::GetFloat("postDashDelay",     2.0f);
    g_downstrikeRange      = Config::GetFloat("downstrikeRange",      5.f);
    g_downstrikeMinHeight  = Config::GetFloat("downstrikeMinHeight",  0.3f);
    g_autoMeleeRange_en = Config::GetBool("autoMeleeRangeEn",  true);
    g_meleeAutoRange    = Config::GetFloat("meleeAutoRange",    5.f);
    g_autoBlock         = Config::GetBool("autoBlock",          true);
    g_blockHpDrop       = Config::GetFloat("blockHpDrop",       5.f);
    g_blockDuration     = Config::GetFloat("blockDuration",     0.5f);
    g_boneEnabled[0] = Config::GetBool("boneHead",       true);
    g_boneEnabled[1] = Config::GetBool("boneNeck",       true);
    g_boneEnabled[2] = Config::GetBool("boneChest",      true);
    g_boneEnabled[3] = Config::GetBool("boneBody",       true);
    g_boneEnabled[4] = Config::GetBool("bonePelvis",     true);
    uint32_t lo      = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t hi      = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId         = ((uint64_t)hi << 32) | lo;
    g_heroLock       = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",        g_enabled);
    g_triggerKey.Save("triggerKey");
    Config::SetFloat("stiffness",     g_stiffness);
    Config::SetFloat("eArcComp",      g_eArcComp);
    Config::SetFloat("fovRadius",     g_fovRadius);
    Config::SetInt("targetMode",      g_targetMode);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetBool("autoShoot",      g_autoShoot);
    Config::SetBool("autoSlice",      g_autoSlice);
    Config::SetFloat("sliceMinDist",  g_sliceMinDist);
    Config::SetFloat("sliceMaxDist",     g_sliceMaxDist);
    Config::SetFloat("sliceVertThresh",  g_sliceVertThresh);
    Config::SetBool("groupSafetyEn",   g_groupSafetyEn);
    Config::SetInt("groupMin",         g_groupMin);
    Config::SetFloat("groupRange",     g_groupRange);
    Config::SetBool("beamSpam",        g_beamSpam);
    Config::SetFloat("beamSpamHp",     g_beamSpamHp);
    Config::SetFloat("beamSpamMin",    g_beamSpamMin);
    Config::SetFloat("beamSpamMax",      g_beamSpamMax);
    Config::SetInt("peMaxCharges",       g_peMaxCharges);
    Config::SetFloat("peRechargeTime",   g_peRechargeTime);
    Config::SetFloat("peRechargeDelay",  g_peRechargeDelay);
    Config::SetInt("peReserve",          g_peReserve);
    Config::SetBool("autoOpener",        g_autoOpener);
    Config::SetFloat("beamKillHp",     g_beamKillHp);
    Config::SetBool("autoDash",            g_autoDash);
    Config::SetFloat("abilityKillHp",      g_abilityKillHp);
    Config::SetFloat("dashKillHp",         g_dashKillHp);
    Config::SetFloat("postDashDelay",      g_postDashDelay);
    Config::SetFloat("downstrikeRange",     g_downstrikeRange);
    Config::SetFloat("downstrikeMinHeight", g_downstrikeMinHeight);
    Config::SetBool("autoMeleeRangeEn",  g_autoMeleeRange_en);
    Config::SetFloat("meleeAutoRange",   g_meleeAutoRange);
    Config::SetBool("autoBlock",         g_autoBlock);
    Config::SetFloat("blockHpDrop",      g_blockHpDrop);
    Config::SetFloat("blockDuration",    g_blockDuration);
    Config::SetBool("boneHead",       g_boneEnabled[0]);
    Config::SetBool("boneNeck",       g_boneEnabled[1]);
    Config::SetBool("boneChest",      g_boneEnabled[2]);
    Config::SetBool("boneBody",       g_boneEnabled[3]);
    Config::SetBool("bonePelvis",     g_boneEnabled[4]);
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; g_prevHp = -1.f; g_lastSliceAt = 0.f; g_lastBeamAt = -99.f; g_beamHoldEnd = 0.f; g_quickMeleeEnd = 0.f; g_blockOn = false; g_blockPressEnd = 0.f; AimResetSmoothing(); }

// ── frame logic ───────────────────────────────────────────────────────────────

extern "C" void on_frame(float dt)
{
    (void)dt;
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Vendetta) return; }  // dormant on any other hero

    g_triggerKey.Update();

    float  now   = GetTime();
    Entity local = LocalPlayer();

    // HP tracking — for auto block
    if (local.IsValid())
    {
        float hp = local.GetHealth();
        if (g_prevHp < 0.f) g_prevHp = hp;
        float drop = g_prevHp - hp;
        if (g_autoBlock && drop >= g_blockHpDrop)
        {
            float newEnd = now + g_blockDuration;
            if (newEnd > g_blockHoldEnd) g_blockHoldEnd = newEnd;
        }
        g_prevHp = hp;
    }

    bool held         = g_triggerKey.IsDown();
    bool overheadDone = (now - g_lastSliceAt) > kOverheadWindow;

    float timeSinceSlice = (g_lastSliceAt > 0.f) ? (now - g_lastSliceAt) : 99.f;
    bool overheadPending = !overheadDone;

    bool groupSafe     = !g_groupSafetyEn || g_nearbyCount < g_groupMin;
    bool abilityActive = (now < g_sliceHoldEnd) || (now < g_dashHoldEnd);

    bool postOverheadMelee = g_lastSliceAt > 0.f
                             && timeSinceSlice >= kOverheadWindow
                             && timeSinceSlice < kOverheadWindow + kPostOverheadMelee;
    bool inMeleeRange      = g_targetValid && g_targetDist <= g_meleeAutoRange;

    // Suppress RMouse during entire E sequence (overhead + follow-up melee) to avoid Projected Edge
    bool eSuppressR  = overheadPending || postOverheadMelee;
    bool wantBlock   = !eSuppressR && g_autoBlock && now < g_blockHoldEnd;

    // Edge-triggered toggle — only press RMouse when state changes, never re-press while on
    if (wantBlock && !g_blockOn)
    {
        g_blockOn       = true;
        g_blockPressEnd = now + kHoldDur;
    }
    else if (!wantBlock && g_blockOn)
    {
        g_blockOn       = false;
        g_blockPressEnd = now + kHoldDur;
    }

    if (now < g_blockPressEnd)
        PressGameButton(GameButton::RMouse);
    else
        ReleaseGameButton(GameButton::RMouse);

    // LMouse: overhead slash → auto follow-up melee after landing → auto-melee in range
    // Suppress during block toggle or active beam press (RMouse held = PE beam, not melee)
    bool blockPressing  = now < g_blockPressEnd;
    bool beamPressing   = now < g_beamHoldEnd;
    // Melee only when the actual target is in range; fall back to the proximity
    // scan ONLY when there's no valid target (e.g. looking away from a point-blank enemy)
    bool meleeContact = inMeleeRange || (!g_targetValid && g_enemyInMeleeRange);
    bool shouldSwing = !blockPressing && !beamPressing
                       && ((g_downstrikeReady)
                           || (postOverheadMelee && meleeContact)
                           || (held && g_autoMeleeRange_en && meleeContact && !overheadPending));
    if (shouldSwing)
        PressGameButton(GameButton::LMouse);
    else
        ReleaseGameButton(GameButton::LMouse);

    // Capture melee condition snapshot for debug overlay
    g_dbgHeld            = held;
    g_dbgBlockPressing   = blockPressing;
    g_dbgBeamPressing    = beamPressing;
    g_dbgInMeleeRange    = inMeleeRange;
    g_dbgEnemyInMelee    = g_enemyInMeleeRange;
    g_dbgOverheadPending = overheadPending;
    g_dbgPostOverhead    = postOverheadMelee;
    g_dbgDownstrikeReady = g_downstrikeReady;
    g_dbgShouldSwing     = shouldSwing;

    // Whirlwind Dash button
    if (now < g_dashHoldEnd)
        PressGameButton(GameButton::Skill1);
    else
    {
        ReleaseGameButton(GameButton::Skill1);
        g_dashHoldEnd = 0.f;
    }

    // Post-dash suppression — release everything and wait for animation to complete
    if (now < g_postDashSuppressEnd && now >= g_dashHoldEnd)
    {
        ReleaseGameButton(GameButton::Skill2);
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        g_sliceHoldEnd = 0.f;
        return;
    }

    if (!held)
    {
        ReleaseGameButton(GameButton::Skill2);
        g_sliceHoldEnd = 0.f;
        return;
    }

    if (!g_targetValid || g_cachedTarget < 0)
    {
        ReleaseGameButton(GameButton::Skill2);
        return;
    }

    if (!local.IsValid()) return;

    bool canDash     = g_targetHp > 0.f && g_targetHp <= g_dashKillHp;
    bool inSliceRange = g_targetDist >= g_sliceMinDist && g_targetDist <= g_sliceMaxDist;
    bool beamRecent   = (now - g_lastBeamAt) < kBeamCooldown;

    // Simulated PE charge recharge — pauses while Warding Stance is active
    {
        int cap = g_peMaxCharges < 4 ? g_peMaxCharges : 4;
        if (g_blockOn)
        {
            // Track when block started so we can shift timers when it ends
            if (g_peBlockPauseAt <= 0.f) g_peBlockPauseAt = now;
        }
        else if (g_peBlockPauseAt > 0.f)
        {
            // Block just ended — shift all pending recharge timers forward by pause duration
            float pauseDur = now - g_peBlockPauseAt;
            for (int i = 0; i < cap; i++)
                if (g_peRechargeAt[i] > 0.f) g_peRechargeAt[i] += pauseDur;
            g_peBlockPauseAt = 0.f;
        }

        // Only tick recharges when not blocking
        if (!g_blockOn)
        {
            for (int i = 0; i < cap; i++)
            {
                if (g_peCharges < cap && g_peRechargeAt[i] > 0.f && now >= g_peRechargeAt[i])
                {
                    g_peCharges++;
                    g_peRechargeAt[i] = 0.f;
                }
            }
        }
        if (g_peCharges > cap) g_peCharges = cap;
    }
    bool hasPeCharge = g_peCharges > g_peReserve;

    // Helper: consume a PE charge and schedule recharge
    auto consumePe = [&]() {
        if (g_peCharges > 0)
        {
            g_peCharges--;
            int cap = g_peMaxCharges < 4 ? g_peMaxCharges : 4;
            for (int i = 0; i < cap; i++)
            {
                if (g_peRechargeAt[i] <= 0.f || now >= g_peRechargeAt[i])
                { g_peRechargeAt[i] = now + g_peRechargeDelay + g_peRechargeTime; break; }
            }
        }
    };

    // Detect user-initiated beam fires (RMouse+LMouse rising edge while plugin isn't pressing them)
    {
        bool peCombo = IsKeyDown(GameButton::RMouse) && IsKeyDown(GameButton::LMouse);
        if (peCombo && !g_prevPeCombo && g_beamHoldEnd <= 0.f)
            consumePe();
        g_prevPeCombo = peCombo;
    }

    // Continuous beam spam — own range independent of slice range so it works as a poke
    bool inBeamSpamRange = g_targetDist >= g_beamSpamMin && g_targetDist <= g_beamSpamMax;
    if (!overheadPending && g_beamSpam && hasPeCharge && g_targetHp > 0.f && g_targetHp <= g_beamSpamHp
        && inBeamSpamRange && g_beamHoldEnd <= 0.f)
    {
        g_beamHoldEnd = now + kBeamHold;
        g_lastBeamAt  = now;
        consumePe();
    }

    // Arm beam: opener (entering range) OR poke/kill (target low HP)
    bool canPoke   = g_targetHp > 0.f && g_targetHp <= g_beamKillHp;
    bool pokeReady = (now - g_lastBeamAt) >= kPokeCooldown;
    if (!overheadPending && g_autoOpener && hasPeCharge && inSliceRange && g_beamHoldEnd <= 0.f
        && (!beamRecent || (canPoke && pokeReady)))
    {
        g_beamHoldEnd = now + kBeamHold;
        g_lastBeamAt  = now;
        consumePe();
    }

    // Soaring Slice (E): opener follow-up after beam, OR kill confirm
    // Also allow E at close range (<sliceVertThresh) using vertical throw
    // Slice Min/Max are the only hard range gates. Vert Throw only affects aim style.
    bool inERange    = inSliceRange;
    bool canSlice  = g_targetHp > 0.f && g_targetHp <= g_abilityKillHp;
    bool openerE   = g_autoOpener && inERange && beamRecent
                     && (now - g_lastBeamAt) >= kBeamToSlice;
    bool killSlice = g_autoSlice && canSlice && inERange
                     && (now - g_lastBeamAt) >= kBeamToSlice;
    if (!overheadPending && (openerE || killSlice) && g_sliceHoldEnd <= 0.f)
    {
        g_sliceHoldEnd    = now + kHoldDur;
        g_lastSliceAt     = now;
        g_downstrikeReady = false;
    }

    // Arm dash — suppressed during E overhead to avoid interrupting the downward slam
    if (!overheadPending && g_autoDash && canDash && groupSafe && g_dashHoldEnd <= 0.f && g_postDashSuppressEnd <= now)
    {
        SkillCooldown s1 = local.GetSkill1Cooldown();
        if (!s1.IsOnCooldown())
        {
            g_dashHoldEnd         = now + kHoldDur;
            g_postDashSuppressEnd = now + kHoldDur + g_postDashDelay;
        }
    }
    g_killDetected = false;

    // Beam button: RMouse + LMouse = Projected Edge
    // Suppressed only during overhead window; beam cancels block naturally (RMouse re-press)
    if (now < g_beamHoldEnd)
    {
        if (!overheadPending)
        {
            g_blockOn = false;  // beam overrides block — RMouse press will toggle it off
            PressGameButton(GameButton::RMouse);
            PressGameButton(GameButton::LMouse);
        }
    }
    else
        g_beamHoldEnd = 0.f;

    // Soaring Slice button
    if (now < g_sliceHoldEnd)
        PressGameButton(GameButton::Skill2);
    else
    {
        ReleaseGameButton(GameButton::Skill2);
        g_sliceHoldEnd = 0.f;
    }

    // Whirlwind Dash button handled above (before trigger gate)
}

static bool  g_showEnergyDebug = false;
static float g_peakLookup[0x2000] = {};  // peak value ever seen per lookup ID, for charge detection

static void DrawEnergyBar(float x, float y, const char* label, float val, float maxVal, Color fillCol)
{
    const float LW = 88.f, BW = 100.f, BH = 10.f;
    Draw::TextShadow(x, y, Color(200, 200, 200), label, 11);
    float bx = x + LW;
    Draw::RectFilled(bx, y, BW, BH, Color(30, 30, 30, 200));
    float fill = (maxVal > 0.f && val > 0.f) ? val / maxVal : 0.f;
    if (fill > 1.f) fill = 1.f;
    if (fill > 0.f) Draw::RectFilled(bx, y, BW * fill, BH, fillCol);
    Draw::Rect(bx, y, BW, BH, Color(120, 120, 120), 1.f);
    TextBuilder<16> tb; tb.putFloat(val, 3);
    Draw::TextShadow(bx + BW + 4.f, y, Color::White(), tb.c_str(), 11);
}

// ── render (target cache + HUD) ───────────────────────────────────────────────

extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Vendetta) return; }  // dormant on any other hero

    bool held = g_triggerKey.IsDown();

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    // Kill detection — check before scan overwrites target state
    if (g_cachedTarget >= 0 && g_prevTargetAlive)
    {
        Entity check(g_cachedTarget);
        if (!check.IsAlive())
            g_killDetected = true;
    }

    float now2            = GetTime();
    float timeSinceSlice2 = (g_lastSliceAt > 0.f) ? (now2 - g_lastSliceAt) : 99.f;
    bool  inOverheadWindow = timeSinceSlice2 < kOverheadWindow;

    bool  eFiring      = now2 < g_sliceHoldEnd;
    float timeSinceBeam2 = (g_lastBeamAt > -90.f) ? (now2 - g_lastBeamAt) : 99.f;
    bool  preESnap     = !inOverheadWindow && timeSinceBeam2 >= 0.f && timeSinceBeam2 < kBeamCooldown;

    int32_t bestIdx   = -1;
    float   bestScore = 99999.f;
    float   bestHp    = 0.f;
    float   bestDist  = 9999.f;
    Entity  bestEnt;
    bool    lockedAlive = false;
    Entity  lockedEnt;

    for (Entity p : Players())
    {
        if (!p.IsAlive() || p.IsLocal()) continue;

        // Resolve locked-target early — before any visibility/bone/FOV checks that can
        // fail when we're looking away (e.g. during vertical E aim).
        bool isLockedTarget = (inOverheadWindow || preESnap || eFiring) && p.Index() == g_cachedTarget;
        float dist = WorldDist(local.GetPosition(), p.GetPosition());
        if (isLockedTarget)
        {
            lockedAlive = true;
            bestHp      = p.GetHealth();
            bestDist    = dist;
            lockedEnt   = p;
            continue;
        }

        if (!p.IsVisible()) continue;

        Vector3 headWorld = p.GetBonePos(Bone::Head);
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;

        if (p.Index() == g_cachedTarget)
        {
            lockedAlive = true;
            bestHp      = p.GetHealth();
            bestDist    = dist;
            lockedEnt   = p;
            continue;
        }

        float score = (g_targetMode == 1) ? p.GetHealth() : sd;
        if (score < bestScore) { bestScore = score; bestIdx = p.Index(); bestHp = p.GetHealth(); bestDist = dist; bestEnt = p; }
    }

    Entity tgt;
    if (lockedAlive)       { bestIdx = g_cachedTarget; tgt = lockedEnt; }
    else if (bestIdx >= 0) { tgt = bestEnt; }

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) AimResetSmoothing();
        g_targetValid     = true;
        g_prevTargetAlive = true;
        g_targetHp        = bestHp;
        g_targetDist      = bestDist;
        g_cachedTarget    = bestIdx;
        g_targetPos       = tgt.GetPosition();

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

        // eFiring / preESnap hoisted above scan loop (needed for isLockedTarget FOV bypass)

        // During E overhead: full snap regardless of trigger key (player is airborne)
        // Aim at Head — Vendetta is above the target, aiming at head creates downward angle into body
        if (inOverheadWindow)
        {
            AimAtBone(g_cachedTarget, Bone::Head, 2000.f);
        }
        else if (eFiring || preESnap)
        {
            bool beamStillFiring = now2 < g_beamHoldEnd;
            // Only do the straight-up throw when E will actually fire (target inside
            // the Slice Min/Max range). Prevents yanking the view up with no E click.
            bool inSliceRangeR = g_targetDist >= g_sliceMinDist && g_targetDist <= g_sliceMaxDist;
            bool wantVert = inSliceRangeR && g_targetDist < g_sliceVertThresh
                            && !(beamStillFiring && g_targetDist >= g_sliceVertThresh);
            if (wantVert)
            {
                // Close range + E ready: aim straight up so 15m E arc lands on target
                Vector3 vPos = local.GetPosition();
                if (vPos.IsValid())
                    AimAtPosition({ vPos.x, vPos.y + 100.f, vPos.z }, 2000.f);
                else
                    AimAtBone(g_cachedTarget, Bone::Body, 2000.f);
            }
            else
            {
                // Far range, E on cooldown, or beam still firing: aim at body
                AimAtBone(g_cachedTarget, Bone::Body, 2000.f);
            }
        }
        else if (held)
        {
            // Normal aim — when in slice range, raise aim so sword arc lands on target
            bool inSliceRangeNow = g_targetDist >= g_sliceMinDist && g_targetDist <= g_sliceMaxDist;
            if (g_eArcComp > 0.f && inSliceRangeNow)
            {
                Vector3 bonePos = tgt.GetBonePos(g_bestBone);
                if (bonePos.IsValid())
                {
                    bonePos.y += g_eArcComp * (g_sliceMaxDist - g_targetDist);
                    AimAtPosition(bonePos, g_stiffness);
                }
                else AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
            }
            else AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
        }
        g_cachedHitbox = AimHitsHitbox(g_cachedTarget, g_hitboxScale);

        // Spatial downstrike: above target + close enough + facing them
        if (inOverheadWindow)
        {
            Vector3 myPos = local.GetPosition();
            if (myPos.IsValid() && g_targetPos.IsValid())
            {
                float heightAbove = myPos.y - g_targetPos.y;
                bool aboveTarget  = heightAbove >= g_downstrikeMinHeight;
                bool inRange      = g_targetDist <= g_downstrikeRange;
                bool facing       = g_cachedHitbox >= 0;
                g_downstrikeReady = aboveTarget && inRange && facing;
            }
        }
        else
        {
            g_downstrikeReady = false;
        }
    }
    else if (!inOverheadWindow && !preESnap && !eFiring)
    {
        // Only clear when not in a locked window — vertical aim causes Players() to miss
        // the target, but cached values must persist so on_frame can still fire E.
        g_targetValid     = false;
        g_prevTargetAlive = false;
        g_cachedTarget    = -1;
        g_cachedHitbox    = -1;
        g_targetPos       = {};
        g_downstrikeReady = false;
    }

    // Proximity melee scan — any enemy within melee range regardless of FOV
    g_enemyInMeleeRange = false;
    Vector3 myPos = local.GetPosition();
    if (myPos.IsValid())
    {
        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
            if (WorldDist(myPos, p.GetPosition()) <= g_meleeAutoRange)
            { g_enemyInMeleeRange = true; break; }
        }
    }

    // Count enemies near target for group safety check
    g_nearbyCount = 0;
    if (g_groupSafetyEn && g_targetValid && g_targetPos.IsValid())
    {
        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
            if (WorldDist(p.GetPosition(), g_targetPos) <= g_groupRange)
                g_nearbyCount++;
        }
    }

    // Energy debug panel — right side of screen to avoid OW2 UI overlap
    if (g_showEnergyDebug)
    {
        Vector2 ssz = ScreenSize();
        float ex = ssz.x > 0 ? ssz.x - 250.f : 1400.f;
        float ey = 60.f;
        const float EL = 13.f;
        Draw::TextShadow(ex, ey, Color::Cyan(), "ENERGY DEBUG", 11); ey += EL;
        Draw::TextShadow(ex, ey, Color::Yellow(), "fire PE -> watch red bars", 10); ey += EL;

        // Skill cooldowns (all 3)
        SkillCooldown c1 = local.GetSkill1Cooldown();
        SkillCooldown c2 = local.GetSkill2Cooldown();
        SkillCooldown c3 = local.GetSkill3Cooldown();
        DrawEnergyBar(ex, ey, "s1Cd:", c1.current, c1.max > 0 ? c1.max : 8.f, Color(120,180,255)); ey += EL;
        DrawEnergyBar(ex, ey, "s2Cd:", c2.current, c2.max > 0 ? c2.max : 8.f, Color(120,180,255)); ey += EL;
        DrawEnergyBar(ex, ey, "s3Cd:", c3.current, c3.max > 0 ? c3.max : 8.f, Color::Cyan());      ey += EL;

        // Skill durations
        SkillCooldown d1 = local.GetSkill1Duration();
        SkillCooldown d2 = local.GetSkill2Duration();
        SkillCooldown d3 = local.GetSkill3Duration();
        DrawEnergyBar(ex, ey, "s1Dur:", d1.current, d1.max > 0 ? d1.max : 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2Dur:", d2.current, d2.max > 0 ? d2.max : 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s3Dur:", d3.current, d3.max > 0 ? d3.max : 3.f, Color(180,180,180)); ey += EL;

        // Raw PluginEntity — every field
        PluginEntity ld{};
        xn_get_local_player(&ld);

        // skillActive — .x is bool/count, .y unknown
        DrawEnergyBar(ex, ey, "s1A.x:", ld.skill1Active.x, 4.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s1A.y:", ld.skill1Active.y, 4.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2A.x:", ld.skill2Active.x, 4.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2A.y:", ld.skill2Active.y, 4.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s3A.x:", ld.skill3Active.x, 4.f, Color::Cyan());      ey += EL;
        DrawEnergyBar(ex, ey, "s3A.y:", ld.skill3Active.y, 4.f, Color::Cyan());      ey += EL;

        // HP / resource fields
        DrawEnergyBar(ex, ey, "hp:",      ld.health,      ld.maxHealth > 0 ? ld.maxHealth : 300.f, Color(80,255,80));   ey += EL;
        DrawEnergyBar(ex, ey, "armor:",   ld.armor,       300.f, Color(180,140,40));  ey += EL;
        DrawEnergyBar(ex, ey, "barrier:", ld.barrier,     300.f, Color(80,140,255));  ey += EL;
        DrawEnergyBar(ex, ey, "ovrhp:",   ld.overhealth,  300.f, Color(120,255,120)); ey += EL;
        DrawEnergyBar(ex, ey, "ultChg:", GetUltCharge(), 100.f, Color(220,200,0)); ey += EL;

        // Flags
        {
            TextBuilder<64> tb;
            tb.put("isRld=").putInt(ld.isReloading)
              .put(" isTarget=").putInt(ld.isTargetable)
              .put(" invuln=").putInt(ld.isInvulnerable)
              .put(" ultAct=").putInt(ld.ultActive);
            Color fc = ld.isReloading ? Color::Red() : Color(180,180,180);
            Draw::TextShadow(ex, ey, fc, tb.c_str(), 10); ey += EL;
        }

        // delta1/delta2 — documented as AABB but may encode other data
        {
            TextBuilder<64> tb;
            tb.put("d1=(").putFloat(ld.delta1.x,1).put(",").putFloat(ld.delta1.y,1).put(",").putFloat(ld.delta1.z,1).put(")");
            Draw::TextShadow(ex, ey, Color(160,160,160), tb.c_str(), 10); ey += EL;
            tb.clear();
            tb.put("d2=(").putFloat(ld.delta2.x,1).put(",").putFloat(ld.delta2.y,1).put(",").putFloat(ld.delta2.z,1).put(")");
            Draw::TextShadow(ex, ey, Color(160,160,160), tb.c_str(), 10); ey += EL;
        }

        // velocity magnitude
        {
            float spd = sqrtf(ld.velocity.x*ld.velocity.x + ld.velocity.y*ld.velocity.y + ld.velocity.z*ld.velocity.z);
            DrawEnergyBar(ex, ey, "speed:", spd, 20.f, Color(200,200,100)); ey += EL;
            DrawEnergyBar(ex, ey, "velY:",  ld.velocity.y, 20.f, Color(200,200,100)); ey += EL;
        }

        // nametagYPos — might encode height/state data
        DrawEnergyBar(ex, ey, "ntag:", ld.nametagYPos, 5.f, Color(160,160,160)); ey += EL;

        // Ult cooldown/duration
        SkillCooldown uc = local.GetUltCooldown();
        SkillCooldown ud = local.GetUltDuration();
        DrawEnergyBar(ex, ey, "ultCd:",  uc.current, uc.max > 0 ? uc.max : 8.f, Color(220,200,0)); ey += EL;
        DrawEnergyBar(ex, ey, "ultDur:", ud.current, ud.max > 0 ? ud.max : 8.f, Color(220,200,0)); ey += EL;

        // WeaponInfo probes — PE = RMouse+LMouse = 0x0003
        // useable/shootable go false when charges depleted
        {
            WeaponInfo wi{};
            auto showWI = [&](const char* label, int32_t flag) {
                bool ok = GetWeaponInfo(flag, wi);
                TextBuilder<48> tb;
                tb.put(label)
                  .put(ok ? "" : "(invalid)")
                  .put(" use=").putInt(wi.useable)
                  .put(" shoot=").putInt(wi.shootable)
                  .put(" rld=").putInt(wi.reloading)
                  .put(" blk=").putInt(wi.skillBlocked);
                Color c = (!wi.shootable || !wi.useable) ? Color::Red() : Color(180,255,180);
                Draw::TextShadow(ex, ey, c, tb.c_str(), 10); ey += EL;
                if (wi.maxRange > 0.f) {
                    TextBuilder<24> tb2; tb2.put("  range=").putFloat(wi.maxRange, 1).put("m spd=").putFloat(wi.projectileSpeed, 0);
                    Draw::TextShadow(ex, ey, Color(160,160,160), tb2.c_str(), 10); ey += EL;
                }
            };
            showWI("LMouse(0x1):", InputFlag::PrimaryFire);
            showWI("RMouse(0x2):", InputFlag::SecondaryFire);
            showWI("LR combo(0x3):", InputFlag::ScopedShoot);
            showWI("Skill1(0x8):", InputFlag::Skill1);
            showWI("Skill2(0x10):", InputFlag::Skill2);
        }

        // Broad lookup scan 0x0000-0x1FFF with peak tracking
        // Bars appear only when a value has been nonzero; turn red when depleted
        for (int pid = 0; pid < 0x2000 && pid < (int)(sizeof(g_peakLookup)/sizeof(g_peakLookup[0])); pid++)
        {
            float v = GetLookupSkill((uint16_t)pid);
            if (v > g_peakLookup[pid]) g_peakLookup[pid] = v;
            if (g_peakLookup[pid] <= 0.f) continue;
            bool depleted = v < g_peakLookup[pid] * 0.99f;
            Color col = depleted ? Color::Red() : Color(255,180,255);
            TextBuilder<24> lb; lb.put("lk").putInt(pid).put(":");
            DrawEnergyBar(ex, ey, lb.c_str(), v, g_peakLookup[pid], col); ey += EL;
            if (ey > ssz.y - 20.f) break;
        }
    }

    // Melee debug overlay — always visible when toggled, shows why swing is/isn't firing
    if (g_showMeleeDebug)
    {
        float mx = 10.f, my = 360.f;
        const float ML = 14.f, MF = 12.f;
        Color colSwing = g_dbgShouldSwing ? Color::Green() : Color::Red();
        Draw::TextShadow(mx, my, colSwing,
            g_dbgShouldSwing ? "MELEE: SWINGING" : "MELEE: idle", 13); my += ML + 2.f;

        // good=true means this condition currently allows the swing
        auto line = [&](const char* label, bool val, bool good) {
            TextBuilder<48> tb; tb.put(label).put(val ? "YES" : "NO");
            Draw::TextShadow(mx, my, (val == good) ? Color::Green() : Color::Red(), tb.c_str(), MF);
            my += ML;
        };
        line("held (trigger): ",   g_dbgHeld,            true);
        line("autoMeleeEn: ",      g_autoMeleeRange_en,  true);
        line("inMeleeRange: ",     g_dbgInMeleeRange,    true);
        line("enemyInMelee: ",     g_dbgEnemyInMelee,    true);
        line("!overheadPending: ", !g_dbgOverheadPending, true);
        line("!blockPressing: ",   !g_dbgBlockPressing,  true);
        line("!beamPressing: ",    !g_dbgBeamPressing,   true);
        line("downstrikeReady: ",  g_dbgDownstrikeReady, true);
        line("postOverhead: ",     g_dbgPostOverhead,    true);

        TextBuilder<48> tb;
        tb.put("dist=").putFloat(g_targetDist, 1).put("m  range<=").putFloat(g_meleeAutoRange, 1);
        Draw::TextShadow(mx, my, Color::White(), tb.c_str(), MF); my += ML;
    }

    // HUD — show when trigger held or when in auto-melee range
    bool inRange = g_targetValid && g_targetDist <= g_meleeAutoRange;
    if (!held && !inRange) return;

    float x = 10.f, y = 200.f;
    const float F = 11.f, L = 14.f;
    float now = GetTime();

    if (g_targetValid)
    {
        TextBuilder<64> tb;
        tb.put("TARGET hp=").putFloat(g_targetHp, 0).put(" dist=").putFloat(g_targetDist, 1);
        Draw::TextShadow(x, y, Color::Green(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("hitbox=").putInt(g_cachedHitbox);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;
    }

    Entity lp = LocalPlayer();
    if (lp.IsValid())
    {
        TextBuilder<64> tb;
        SkillCooldown s1 = lp.GetSkill1Cooldown();
        SkillCooldown s2 = lp.GetSkill2Cooldown();
        tb.put("dash_cd=").putFloat(s1.current, 1).put(" slice_cd=").putFloat(s2.current, 1);
        Draw::TextShadow(x, y, Color(200, 200, 200), tb.c_str(), F); y += L;

        tb.clear();
        tb.put("dash_hold=").putFloat(g_dashHoldEnd > 0.f ? g_dashHoldEnd - now : 0.f, 2)
          .put(" slice_hold=").putFloat(g_sliceHoldEnd > 0.f ? g_sliceHoldEnd - now : 0.f, 2);
        Draw::TextShadow(x, y, Color(200, 200, 200), tb.c_str(), F); y += L;

        bool blocking = g_autoBlock && now < g_blockHoldEnd;
        tb.clear(); tb.put("blocking=").putInt(blocking ? 1 : 0)
                       .put(" hp=").putFloat(g_prevHp, 0);
        Draw::TextShadow(x, y, blocking ? Color::Cyan() : Color(200, 200, 200), tb.c_str(), F); y += L;

        // E debug: show why Soaring Slice might not fire
        bool canKillDbg  = g_targetValid && g_targetHp > 0.f && g_targetHp <= g_abilityKillHp;
        bool distOkDbg   = g_targetValid && g_targetDist >= g_sliceMinDist && g_targetDist <= g_sliceMaxDist;
        tb.clear();
        tb.put("E: kill=").putInt(canKillDbg)
          .put(" dist=").putInt(distOkDbg)
          .put("(").putFloat(g_targetDist, 1).put("m)")
          .put(" tHP=").putFloat(g_targetHp, 0);
        Draw::TextShadow(x, y, (canKillDbg && distOkDbg) ? Color::Green() : Color::Yellow(), tb.c_str(), F);
    }

}

// ── menu ──────────────────────────────────────────────────────────────────────

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Vendetta"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        ImGui::Checkbox("Show Energy Debug HUD", &g_showEnergyDebug);
        ImGui::Checkbox("Show Melee Debug HUD", &g_showMeleeDebug);
        if (!g_enabled) return;
        ImGui::Separator();

        g_triggerKey.Render("Trigger Key");
        ImGui::SliderFloat("Smoothing (0=snap)", &g_stiffness, 0.f, 1500.f);
        ImGui::SliderFloat("E Arc Comp (m/m)",   &g_eArcComp,  0.f,  0.5f);
        ImGui::SliderFloat("FOV Radius", &g_fovRadius, 10.f, 500.f);
        ImGui::Combo("Target Mode", &g_targetMode, "Closest Crosshair\0Lowest HP\0");
        ImGui::SliderFloat("Hitbox Scale", &g_hitboxScale, 0.7f, 1.5f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Melee (LMouse)", &g_autoShoot);
        ImGui::Separator();

        ImGui::SliderFloat("Slice Kill HP",  &g_abilityKillHp, 1.f, 500.f);
        ImGui::Separator();

        ImGui::Checkbox("Group Safety (no engage near groups)", &g_groupSafetyEn);
        if (g_groupSafetyEn)
        {
            ImGui::SliderInt("Min Enemies to Suppress", &g_groupMin,  1, 5);
            ImGui::SliderFloat("Group Radius (m)",     &g_groupRange, 1.f, 15.f);
            TextBuilder<32> gb; gb.put("Nearby enemies: ").putInt(g_nearbyCount);
            ImGui::Text(gb.c_str());
        }
        ImGui::Separator();
        // PE charge simulation (SDK doesn't expose PE charges)
        ImGui::SliderInt("PE Max Charges",       &g_peMaxCharges,   1, 4);
        ImGui::SliderInt("PE Reserve (min keep)", &g_peReserve,        0, 3);
        ImGui::SliderFloat("PE Recharge Time (s)", &g_peRechargeTime,  1.0f, 8.f);
        ImGui::SliderFloat("PE Regen Delay (s)",   &g_peRechargeDelay, 0.0f, 3.0f);
        {
            TextBuilder<48> cb;
            cb.put("PE charges: ").putInt(g_peCharges).put(" / ").putInt(g_peMaxCharges);
            cb.put("  (fire when > ").putInt(g_peReserve).put(")");
            ImGui::Text(cb.c_str());
        }
        ImGui::Separator();
        ImGui::Checkbox("Beam Spam below HP", &g_beamSpam);
        if (g_beamSpam)
        {
            ImGui::SliderFloat("Beam Spam HP Threshold", &g_beamSpamHp, 1.f, 1000.f);
            ImGui::SliderFloat("Beam Spam Min Dist (m)", &g_beamSpamMin, 1.f, 30.f);
            ImGui::SliderFloat("Beam Spam Max Dist (m)", &g_beamSpamMax, 1.f, 40.f);
        }
        ImGui::Separator();
        ImGui::Checkbox("Auto Opener (beam -> E) + Poke", &g_autoOpener);
        if (g_autoOpener)
            ImGui::SliderFloat("Beam/Poke Kill HP", &g_beamKillHp, 1.f, 500.f);
        ImGui::Checkbox("Auto Soaring Slice (E kill confirm)", &g_autoSlice);
        ImGui::SliderFloat("Downstrike Range (m)",    &g_downstrikeRange,     1.f, 10.f);
        ImGui::SliderFloat("Downstrike Min Height (m)", &g_downstrikeMinHeight, 0.0f, 2.f);
        if (g_autoSlice || g_autoOpener)
        {
            ImGui::SliderFloat("Slice Min Dist (m)",    &g_sliceMinDist,    1.f,  15.f);
            ImGui::SliderFloat("Slice Max Dist (m)",    &g_sliceMaxDist,    5.f,  30.f);
            ImGui::SliderFloat("Vert Throw < (m)",      &g_sliceVertThresh, 1.f,  30.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Whirlwind Dash (Shift)", &g_autoDash);
        if (g_autoDash)
        {
            ImGui::SliderFloat("Dash Kill HP",        &g_dashKillHp,    1.f, 200.f);
            ImGui::SliderFloat("Post-Dash Lockout (s)", &g_postDashDelay, 0.f, 4.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Melee+Block in Range", &g_autoMeleeRange_en);
        if (g_autoMeleeRange_en)
            ImGui::SliderFloat("Melee Auto Range (m)", &g_meleeAutoRange, 1.f, 8.f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Block (Warding Stance)", &g_autoBlock);
        if (g_autoBlock)
        {
            ImGui::SliderFloat("Block HP Drop Trigger", &g_blockHpDrop,    1.f, 50.f);
            ImGui::SliderFloat("Block Hold Duration",   &g_blockDuration,  0.1f, 5.0f);
        }
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
}
