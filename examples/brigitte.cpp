// Brigitte Plugin
// Auto Rocket Flail (melee), auto Whip Shot with prediction, auto Shield Bash combo, auto block

#include <xenon/SDK.hpp>
using namespace xenon;

static float    g_stiffness       = 30.f;   // legacy / fallback
static float    g_meleeStiffness  = 30.f;
static float    g_whipStiffness   = 80.f;
static float    g_fovRadius       = 200.f;
static bool     g_drawFov         = true;
static bool     g_showDebug       = true;   // show on-screen bash/debug overlay
static int      g_targetMode      = 0;      // 0=closest crosshair, 1=lowest HP
static Hotkey   g_triggerKey(VK::LButton);  // press-to-bind in the menu (defaults to LMB)
static bool     g_shieldHoldMode = false;   // shield test mode: drive the shield from the trigger key only
static int      g_shieldHoldMethod = 0;     // 0 = RMouse, 1 = Skill1 button — A/B which input drives the shield
static bool     g_shieldUp       = false;   // cached REAL shield state (from S3, see ShieldIsUp); for the overlay
static float    g_shieldClickEnd = 0.f;     // >0: a shield toggle click press is in progress, release at this time
static float    g_shieldSettleEnd = 0.f;    // >0: waiting after a click for S3 to report the new state before re-clicking
static constexpr float kShieldClickDur = 0.08f;  // RMB hold per toggle click — long enough the game reliably sees the edge
static constexpr float kShieldSettle   = 0.08f;  // after a click, wait this long for S3 to update before deciding to re-click
static bool     g_enabled         = true;
static bool     g_autoMelee       = true;
static float    g_meleeRange      = 6.f;
static bool     g_autoWhip        = true;
static float    g_whipMinDist     = 0.f;
static float    g_whipMaxDist     = 20.f;
static float    g_targetMaxRange  = 25.f;   // ignore enemies beyond this distance (m)
static float    g_whipSpeed       = 80.f;   // m/s fallback — overridden by WeaponInfo if available
static bool     g_autoBash        = false;
static float    g_bashRange       = 5.f;
static float    g_bashKillHp      = 200.f;
static bool     g_autoBlock       = true;
static float    g_blockHpDrop     = 5.f;
static float    g_blockHoldEnd    = 0.f;
static constexpr float kBlockDur  = 0.6f;
static bool     g_duelMode        = false;  // auto-track + auto-melee nearest enemy, no trigger needed
static float    g_duelRange       = 8.f;    // duel tracking radius (m)
static bool     g_autoReaction    = true;   // face whoever just shot you on HP drop
static float    g_reactionMinDmg  = 20.f;   // HP drop threshold to trigger reaction turn
static float    g_reactionDur     = 1.5f;   // how long to lock onto the attacker (s)
static float    g_reactionTimer   = 0.f;
static float    g_hitboxScale     = 1.0f;
static uint64_t g_heroId          = 0;
static bool     g_heroLock        = false;
static bool     g_duelEngaged     = false;  // cached from on_render for on_frame use

// Ult detection — face enemy when their ult activates
static bool     g_ultDetect       = true;
static uint64_t g_ultWatchId      = HeroId::Junkrat;  // hero ID to watch (0 = any enemy ult)

// Target cache (written in on_render, read in on_frame)
static int32_t  g_cachedTarget    = -1;
static bool     g_targetValid     = false;
static float    g_targetHp        = 0.f;
static float    g_targetDist      = 99999.f;
static int      g_cachedHitbox    = -1;
static int      g_bestBone        = Bone::Chest;
static Vector3  g_targetHeadPos   = {};
static Vector3  g_targetBodyPos   = {};
static Vector3  g_targetPos       = {};
static bool     g_targetVisible   = false;
static float    g_targetBarrier   = 0.f;
static bool     g_targetShielded  = false;

// Melee-specific aim settings
static float    g_meleeFovRadius  = 350.f;
static float    g_meleeHitboxScale = 1.3f;

// Velocity tracking for Whip Shot prediction
static Vector3  g_targetVelocity  = {};
static Vector3  g_prevHeadPos     = {};
static float    g_prevHeadTime    = 0.f;

// HP tracking for auto reaction
static float    g_prevLocalHp     = -1.f;

// Shield Bash state machine
enum BashPhase { BA_IDLE, BA_RAISING, BA_BASHING };
static BashPhase g_bashPhase      = BA_IDLE;
static float     g_bashPhaseT     = 0.f;
static float     g_bashCdEnd      = 0.f;
static float     g_postBashWhipSuppress = 0.f;  // suppress whip for a moment after bash
static float     g_postBashMeleeSuppress = 0.f; // suppress melee (LMB) for a moment after bash, see g_postBashMeleeDelay
static float     g_postBashMeleeDelay   = 0.25f;// how long after a bash before melee resumes (tunable in menu)
static constexpr float kRaiseTimeout = 0.30f;  // fallback: bash anyway if the shield read never confirms up
static constexpr float kBashHold  = 0.12f;  // hold LMouse for bash
static constexpr float kBashCd    = 5.f;    // manual bash cooldown tracking

// Melee-cancel: in melee range, land one flail swing BEFORE bashing/whipping. The
// swing connects, then the bash/whip cancels its recovery animation for extra damage.
static bool   g_meleeCancel     = true;
static float  g_meleeCancelTime = 0.35f;  // hold the swing this long (let it connect) before canceling
static float  g_meleeCancelEnd  = 0.f;    // active swing-window end time
static bool   g_meleeCancelDone = false;  // latched: swing landed, ability may now fire
static bool   g_dbgPreMelee     = false;  // overlay debug

// Whip Shot hold timer
static float    g_whipHoldEnd     = 0.f;
static float    g_whipPreAimEnd   = 0.f;
static constexpr float kWhipHold    = 0.12f;
static constexpr float kWhipPreAim  = 0.05f;  // aim at prediction this long before firing

static int        g_bashGroupMin  = 2;     // skip bash when this many enemies in range
static float      g_bashGroupRange = 5.f;  // radius around target to count enemies

// Repair Pack
static bool     g_autoRepair      = true;
static float    g_repairHpPct     = 50.f;   // heal ally below this % HP
static float    g_repairRange     = 25.f;   // max throw range (m)
static float    g_repairHoldEnd   = 0.f;
static float    g_repairCdEnd     = 0.f;   // self-managed cooldown, avoids re-fire before game registers
static int32_t  g_repairTarget      = -1;
static Vector3  g_repairTargetPos   = {};
static bool     g_repairTargetInFov = false;
static constexpr float kRepairHold = 0.15f;
static constexpr float kRepairCd   = 0.5f;  // prevent same-frame re-fire; SDK cooldown gates the rest

// Debug: count which filter rejected each ally in the scan loop
static int      g_dbgAllyCount    = 0;
static int      g_dbgRejHp        = 0;   // rejected: above repairHpPct
static int      g_dbgRejRange     = 0;   // rejected: out of range
static int      g_dbgRejLOS       = 0;   // rejected: LOS blocked
static float    g_dbgClosestDist  = 0.f;
static float    g_dbgClosestHpPct = 0.f;

// RMouse debug — set each frame in on_frame
static int      g_dbgBashPhase    = 0;   // 0=IDLE 1=RAISING 2=BASHING
static bool     g_dbgEngaged      = false;
static bool     g_dbgHeld         = false;
static bool     g_dbgKeyDown      = false;
static bool     g_dbgBlockPending = false;
static bool     g_dbgBashActive   = false;
static bool     g_dbgRMouseDown   = false;  // what we actually sent this frame
static bool     g_dbgLMouseDown   = false;

// Live debug state updated each frame
static bool     g_dbgTriggerHeld  = false;
static bool     g_dbgRepairFiring = false;
static bool     g_dbgS1OnCd       = false;
static bool     g_dbgS2OnCd       = false;
static int      g_dbgTotalPlayers  = 0;
static int      g_dbgEnemyCount    = 0;
static int      g_dbgEnemyVisible  = 0;   // passed IsAlive+IsEnemy+IsVisible
static int      g_dbgEnemyInFov    = 0;   // also passed head-on-screen + FOV radius

// Bash gate debug — shows which standalone-bash condition is blocking
static bool     g_dbgBashReady     = false; // now >= bash cooldown
static bool     g_dbgBashInRange   = false; // target valid+visible & within bashRange
static bool     g_dbgBashLowHp     = false; // target HP <= bashKillHp
static bool     g_dbgBashGroupOk   = false; // nearbyEnemyCount < bashGroupMin
static bool     g_dbgBashWant      = false; // all standalone-bash conditions met

static int      g_nearbyEnemyCount = 0;     // enemies near the target (live, for overlay)

// Whip gate debug — shows which whip condition is blocking
static bool     g_dbgWhipReady     = false; // Skill1 off cooldown
static bool     g_dbgWhipInRange   = false; // within whipMin..whipMax & visible
static bool     g_dbgWhipClear     = false; // no barrier & not shielded
static bool     g_dbgWhipWant      = false; // all whip conditions met (off cd)

// Aim bones
static const int kAimBones[]      = {
    Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis,
    Bone::LShoulder, Bone::RShoulder
};
static const char* kBoneNames[]   = {
    "Head", "Neck", "Chest", "Body", "Pelvis", "L Shoulder", "R Shoulder"
};
static const int kAimBoneCount    = 7;
static bool g_meleeBoneEnabled[7] = { true, true, true, true, true, true, true };
static bool g_whipBoneEnabled[7]  = { false, false, true, true, true, false, false }; // Chest/Body/Pelvis

// Whip-specific aim settings
static float    g_whipFovRadius    = 200.f;
static float    g_whipHitboxScale  = 1.f;
static float    g_whipAimTolerance = 20.f;  // pixels — only fire when crosshair this close to predicted point

XENON_PLUGIN_INFO(
    "brigitte",
    "Brigitte",
    "Xenon",
    "Auto melee, Whip Shot with prediction, Shield Bash combo, auto block.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()
{
    g_stiffness      = Config::GetFloat("stiffness",      30.f);
    g_meleeStiffness = Config::GetFloat("meleeStiffness", 30.f);
    g_whipStiffness  = Config::GetFloat("whipStiffness",  80.f);
    g_fovRadius     = Config::GetFloat("fovRadius",     200.f);
    g_drawFov       = Config::GetBool("drawFov",        true);
    g_showDebug     = Config::GetBool("showDebug",      true);
    g_shieldHoldMethod = Config::GetInt("shieldHoldMethod", 0);
    g_targetMode    = Config::GetInt("targetMode",      0);
    g_triggerKey.Load("triggerKey");
    g_enabled       = Config::GetBool("enabled",        true);
    g_autoMelee     = Config::GetBool("autoMelee",      true);
    g_meleeRange    = Config::GetFloat("meleeRange",    6.f);
    g_autoWhip      = Config::GetBool("autoWhip",       true);
    g_whipMinDist   = Config::GetFloat("whipMinDist",   7.f);
    g_whipMaxDist   = Config::GetFloat("whipMaxDist",   20.f);
    g_targetMaxRange  = Config::GetFloat("targetMaxRange", 25.f);
    g_whipSpeed     = Config::GetFloat("whipSpeed",     80.f);
    g_autoBash      = Config::GetBool("autoBash",       true);
    g_bashRange     = Config::GetFloat("bashRange",     5.f);
    g_bashKillHp    = Config::GetFloat("bashKillHp",    200.f);
    g_meleeCancel     = Config::GetBool("meleeCancel",      true);
    g_meleeCancelTime = Config::GetFloat("meleeCancelTime", 0.35f);
    g_postBashMeleeDelay = Config::GetFloat("postBashMeleeDelay", 0.25f);
    g_autoBlock     = Config::GetBool("autoBlock",      true);
    g_duelMode        = Config::GetBool("duelMode",         false);
    g_duelRange       = Config::GetFloat("duelRange",       8.f);
    g_autoReaction    = Config::GetBool("autoReaction",     true);
    g_reactionMinDmg  = Config::GetFloat("reactionMinDmg",  20.f);
    g_reactionDur     = Config::GetFloat("reactionDur",     1.5f);
    g_blockHpDrop   = Config::GetFloat("blockHpDrop",   5.f);
    g_hitboxScale   = Config::GetFloat("hitboxScale",   1.0f);
    g_ultDetect        = Config::GetBool("ultDetect",         true);
    {
        uint32_t lo = (uint32_t)Config::GetInt("ultWatchId_lo", (int32_t)(HeroId::Junkrat & 0xFFFFFFFF));
        uint32_t hi = (uint32_t)Config::GetInt("ultWatchId_hi", (int32_t)(HeroId::Junkrat >> 32));
        g_ultWatchId = ((uint64_t)hi << 32) | lo;
    }
    g_autoRepair       = Config::GetBool("autoRepair",        true);
    g_repairHpPct      = Config::GetFloat("repairHpPct",      50.f);
    g_repairRange      = Config::GetFloat("repairRange",      25.f);
    g_bashGroupMin   = Config::GetInt("bashGroupMin",     2);
    g_bashGroupRange = Config::GetFloat("bashGroupRange", 5.f);
    g_meleeFovRadius   = Config::GetFloat("meleeFovRadius",   350.f);
    g_meleeHitboxScale = Config::GetFloat("meleeHitboxScale", 1.3f);
    g_whipFovRadius    = Config::GetFloat("whipFovRadius",    200.f);
    g_whipHitboxScale  = Config::GetFloat("whipHitboxScale",  1.f);
    g_whipAimTolerance = Config::GetFloat("whipAimTolerance", 20.f);
    for (int i = 0; i < kAimBoneCount; i++)
    {
        TextBuilder<24> k; k.put("meleeBone").putInt(i);
        g_meleeBoneEnabled[i] = Config::GetBool(k.c_str(), i < 5);
        TextBuilder<24> w; w.put("whipBone").putInt(i);
        g_whipBoneEnabled[i]  = Config::GetBool(w.c_str(), i >= 2 && i <= 4);
    }
    uint32_t heroLo = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t heroHi = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId        = ((uint64_t)heroHi << 32) | heroLo;
    g_heroLock      = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetFloat("stiffness",      g_stiffness);
    Config::SetFloat("meleeStiffness", g_meleeStiffness);
    Config::SetFloat("whipStiffness",  g_whipStiffness);
    Config::SetFloat("fovRadius",     g_fovRadius);
    Config::SetBool("drawFov",        g_drawFov);
    Config::SetBool("showDebug",      g_showDebug);
    Config::SetInt("shieldHoldMethod", g_shieldHoldMethod);
    Config::SetInt("targetMode",      g_targetMode);
    g_triggerKey.Save("triggerKey");
    Config::SetBool("enabled",        g_enabled);
    Config::SetBool("autoMelee",      g_autoMelee);
    Config::SetFloat("meleeRange",    g_meleeRange);
    Config::SetBool("autoWhip",       g_autoWhip);
    Config::SetFloat("whipMinDist",   g_whipMinDist);
    Config::SetFloat("whipMaxDist",   g_whipMaxDist);
    Config::SetFloat("targetMaxRange", g_targetMaxRange);
    Config::SetFloat("whipSpeed",     g_whipSpeed);
    Config::SetBool("autoBash",       g_autoBash);
    Config::SetFloat("bashRange",     g_bashRange);
    Config::SetFloat("bashKillHp",    g_bashKillHp);
    Config::SetBool("meleeCancel",       g_meleeCancel);
    Config::SetFloat("meleeCancelTime",  g_meleeCancelTime);
    Config::SetFloat("postBashMeleeDelay", g_postBashMeleeDelay);
    Config::SetBool("autoBlock",      g_autoBlock);
    Config::SetBool("duelMode",           g_duelMode);
    Config::SetFloat("duelRange",         g_duelRange);
    Config::SetBool("autoReaction",       g_autoReaction);
    Config::SetFloat("reactionMinDmg",    g_reactionMinDmg);
    Config::SetFloat("reactionDur",       g_reactionDur);
    Config::SetFloat("blockHpDrop",   g_blockHpDrop);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetBool("ultDetect",            g_ultDetect);
    Config::SetInt("ultWatchId_lo",        (int32_t)(g_ultWatchId & 0xFFFFFFFF));
    Config::SetInt("ultWatchId_hi",        (int32_t)(g_ultWatchId >> 32));
    Config::SetBool("autoRepair",          g_autoRepair);
    Config::SetFloat("repairHpPct",        g_repairHpPct);
    Config::SetFloat("repairRange",        g_repairRange);
    Config::SetInt("bashGroupMin",     g_bashGroupMin);
    Config::SetFloat("bashGroupRange", g_bashGroupRange);
    Config::SetFloat("meleeFovRadius",   g_meleeFovRadius);
    Config::SetFloat("meleeHitboxScale", g_meleeHitboxScale);
    Config::SetFloat("whipFovRadius",    g_whipFovRadius);
    Config::SetFloat("whipHitboxScale",  g_whipHitboxScale);
    Config::SetFloat("whipAimTolerance", g_whipAimTolerance);
    for (int i = 0; i < kAimBoneCount; i++)
    {
        TextBuilder<24> k; k.put("meleeBone").putInt(i);
        Config::SetBool(k.c_str(), g_meleeBoneEnabled[i]);
        TextBuilder<24> w; w.put("whipBone").putInt(i);
        Config::SetBool(w.c_str(), g_whipBoneEnabled[i]);
    }
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t)
{
    g_cachedTarget  = -1;
    g_repairTarget  = -1;
    g_repairHoldEnd = 0.f;
    g_repairCdEnd   = 0.f;
    g_bashPhase            = BA_IDLE;
    g_bashCdEnd            = 0.f;   // clear any stale cooldown so rdy isn't stuck at n after respawn/hero swap
    g_shieldClickEnd       = 0.f;
    g_shieldSettleEnd      = 0.f;
    g_meleeCancelEnd       = 0.f;
    g_meleeCancelDone      = false;
    g_postBashWhipSuppress = 0.f;
    g_postBashMeleeSuppress = 0.f;
    g_shieldUp         = false;
    g_prevLocalHp      = -1.f;
    g_prevHeadTime     = 0.f;
    g_reactionTimer    = 0.f;
    AimResetSmoothing();
}


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

// The REAL shield state is readable from S3 (its `enabled` flag tracks the deployed shield),
// so we drive the toggle-bound Barrier Shield as a CLOSED LOOP: read the real state, and if
// it doesn't match what we want, fire a clean toggle click — then keep clicking until the
// read confirms the desired state. This self-corrects missed toggles (the open-loop blind
// click sometimes landed between input samples → trig:Y but s3:n) and never double-toggles
// (it stops the instant S3 reports the target state).
static bool ShieldIsUp()
{
    // The deployed shield shows up as IsSkill3Active()==1 (overlay "act .../.../1"). The S3
    // COOLDOWN.enabled flag ("s3 en") is NOT the shield state — it stays 0 while up — which is
    // why reading it made the closed loop misfire.
    g_shieldUp = IsSkill3Active();   // cache the REAL state for the overlay
    return g_shieldUp;
}

static void SetRMouse(bool wantUp)
{
    float now = GetTime();

    // Press phase: hold RMB down for the full click, then release.
    if (g_shieldClickEnd > 0.f)
    {
        if (now < g_shieldClickEnd) { PressGameButton(GameButton::RMouse); return; }
        ReleaseGameButton(GameButton::RMouse);
        g_shieldClickEnd  = 0.f;
        g_shieldSettleEnd = now + kShieldSettle;   // let S3 reflect this click before judging again
        return;
    }

    // Settle phase: keep RMB up while the game applies the toggle and S3 catches up.
    if (now < g_shieldSettleEnd) { ReleaseGameButton(GameButton::RMouse); return; }

    // Compare REAL state to desired; if they match we're done, otherwise start another click.
    if (ShieldIsUp() == wantUp) { ReleaseGameButton(GameButton::RMouse); return; }

    PressGameButton(GameButton::RMouse);           // begin a toggle click
    g_shieldClickEnd = now + kShieldClickDur;
}

extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Brigitte) return; }  // dormant on any other hero

    g_triggerKey.Update();
    float now        = GetTime();

    // Shield test mode — emit ONE clean ~80ms click per key PRESS (not a continuous hold).
    // Holding the button down re-toggles every frame → the shield flickers; a single click
    // flips it once and it stays. Press again to flip back. The dropdown picks which input to
    // click (RMouse vs the Skill1 button); watch "s3 en" to see which one drives the shield.
    if (g_shieldHoldMode)
    {
        uint32_t btn = (g_shieldHoldMethod == 1) ? GameButton::Skill1 : GameButton::RMouse;
        static float clickEnd = 0.f;
        if (g_triggerKey.Pressed()) clickEnd = now + 0.08f;   // start a single click on the press edge
        if (clickEnd > 0.f)
        {
            if (now < clickEnd) PressGameButton(btn);
            else { ReleaseGameButton(btn); clickEnd = 0.f; }
        }
        return;
    }
    bool  keyDown    = g_triggerKey.IsDown();
    bool  held       = keyDown;

    // HP drop — triggers auto reaction (face attacker) and auto block (raise shield)
    {
        Entity localE = LocalPlayer();
        if (localE.IsValid())
        {
            float curHp  = localE.GetHealth();
            float hpDrop = (g_prevLocalHp > 0.f) ? (g_prevLocalHp - curHp) : 0.f;

            if (g_autoReaction && hpDrop >= g_reactionMinDmg)
            {
                Vector3 lPos    = localE.GetPosition();
                int32_t bestIdx = -1; float bestDot = 0.5f;
                for (Entity p : Players())
                {
                    if (p.IsLocal() || !p.IsAlive() || !p.IsEnemy()) continue;
                    Vector3 ePos = p.GetPosition(); Vector3 eFwd = p.GetForward();
                    float dx = lPos.x-ePos.x, dy = lPos.y-ePos.y, dz = lPos.z-ePos.z;
                    float len = Sqrt(dx*dx + dy*dy + dz*dz);
                    if (len < 0.001f || len > g_targetMaxRange) continue;
                    float dot = (eFwd.x*dx + eFwd.y*dy + eFwd.z*dz) / len;
                    if (dot > bestDot) { bestDot = dot; bestIdx = p.Index(); }
                }
                if (bestIdx >= 0)
                {
                    if (bestIdx != g_cachedTarget) AimResetSmoothing();
                    g_cachedTarget  = bestIdx;
                    g_targetValid   = true;
                    g_reactionTimer = g_reactionDur;
                }
            }

            if (g_autoBlock && hpDrop >= g_blockHpDrop)
                g_blockHoldEnd = now + kBlockDur;

            g_prevLocalHp = curHp;
        }
    }

    // Ult detection — snap to face any enemy (or specific hero) using their ult
    if (g_ultDetect)
    {
        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
            if (g_ultWatchId != 0 && p.GetHeroId() != g_ultWatchId) continue;
            if (p.IsUltActive())
            {
                if (p.Index() != g_cachedTarget) AimResetSmoothing();
                g_cachedTarget  = p.Index();
                g_targetValid   = true;
                g_reactionTimer = g_reactionDur;
                break;
            }
        }
    }

    // Tick reaction timer
    if (g_reactionTimer > 0.f)
    {
        g_reactionTimer -= dt;
        held = true;
    }

    bool duelClose = g_duelMode && g_targetValid && g_targetDist <= g_duelRange;

    g_dbgTriggerHeld = held;

    Entity local = LocalPlayer();

    // Count enemies within 3m of the target — used to suppress bash into groups
    int nearbyEnemyCount = 0;
    if (g_targetValid && g_targetHeadPos.IsValid())
    {
        Vector3 tPos = g_targetHeadPos;
        for (Entity p : Players())
        {
            if (p.IsLocal() || !p.IsAlive() || !p.IsEnemy()) continue;
            Vector3 ePos = p.GetPosition();
            float dx = tPos.x - ePos.x, dy = tPos.y - ePos.y, dz = tPos.z - ePos.z;
            if (Sqrt(dx*dx + dy*dy + dz*dz) <= g_bashGroupRange)
                nearbyEnemyCount++;
        }
    }
    g_nearbyEnemyCount = nearbyEnemyCount;   // expose for the always-on overlay

    // -------------------------------------------------------
    // Auto Repair Pack (Skill2) — fires regardless of combat state
    // Briefly snaps aim to ally, then combat aim resumes
    // -------------------------------------------------------
    // Cancel mid-fire repair if combo or bash started
    if (g_repairHoldEnd > 0.f && g_bashPhase != BA_IDLE)
    {
        ReleaseGameButton(GameButton::Skill2);
        g_repairHoldEnd = 0.f;
    }

    bool repairFiring = false;
    if (held && g_autoRepair && g_repairTarget >= 0 && g_repairHoldEnd <= 0.f && now >= g_repairCdEnd
        && g_bashPhase == BA_IDLE)
    {
        if (local.IsValid())
        {
            g_dbgS1OnCd = false;
            g_dbgS2OnCd = false;
        }
        g_repairHoldEnd = now + kRepairHold;
        g_repairCdEnd   = now + kRepairCd;
    }
    if (g_repairHoldEnd > 0.f && now < g_repairHoldEnd) g_dbgRepairFiring = true;
    else g_dbgRepairFiring = false;
    if (g_repairHoldEnd > 0.f)
    {
        if (now < g_repairHoldEnd)
        {
            if (g_repairTarget >= 0 && g_repairTargetPos.IsValid())
                AimAtPosition(g_repairTargetPos, g_stiffness);
            PressGameButton(GameButton::Skill2);
            repairFiring = true;
        }
        else
        {
            ReleaseGameButton(GameButton::Skill2);
            g_repairHoldEnd = 0.f;
        }
    }

    bool inMelee     = keyDown && g_targetValid && g_targetDist <= g_meleeRange;
    bool inWhipRange = keyDown && g_autoWhip && g_targetValid
                       && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist;
    bool blockPending = g_autoBlock && now < g_blockHoldEnd;
    bool engaged      = held || duelClose || blockPending || g_bashPhase != BA_IDLE;

    if (!engaged)
    {
        ReleaseGameButton(GameButton::LMouse);
        SetRMouse(false);
        ReleaseGameButton(GameButton::Skill1);
        g_whipHoldEnd   = 0.f;
        g_whipPreAimEnd = 0.f;
        g_meleeCancelEnd  = 0.f;
        g_meleeCancelDone = false;
        if (!repairFiring) AimResetSmoothing();
        return;
    }

    // -------------------------------------------------------
    // Standalone Auto Bash (RMouse raise → LMouse bash)
    // -------------------------------------------------------
    // Explicit gates (also surfaced in the menu debug so we can see what's blocking)
    bool bashReady   = now >= g_bashCdEnd;
    bool bashInRange = g_targetValid && g_targetVisible && g_targetDist <= g_bashRange;
    bool bashLowHp   = g_targetHp > 0.f && g_targetHp <= g_bashKillHp;
    bool bashGroupOk = nearbyEnemyCount < g_bashGroupMin;
    // "want bash" ignores cooldown — used to prioritize bash over melee while it recharges
    bool wantBash    = g_autoBash && keyDown && bashInRange && bashLowHp && bashGroupOk;

    g_dbgBashReady   = bashReady;
    g_dbgBashInRange = bashInRange;
    g_dbgBashLowHp   = bashLowHp;
    g_dbgBashGroupOk = bashGroupOk;
    g_dbgBashWant    = wantBash;

    // ── Melee-cancel: if we're in melee range and a bash (or whip) is about to fire,
    // land one flail swing FIRST, then let the ability cancel its recovery. The actual
    // LMouse swing is driven by the normal melee branch below — here we just hold the
    // ability back until the swing has had g_meleeCancelTime to connect.
    // Can a flail swing actually land right now? Barrier is a reliable value; the shield
    // raycast (g_targetShielded) FLICKERS, so we deliberately do NOT let it suppress the
    // pre-swing — otherwise the bash sometimes fires first on a false "shielded" frame.
    bool shieldUp = ShieldIsUp();   // real deployed-shield state (IsSkill3Active) — see note below
    bool canSwing = g_autoMelee && inMelee && g_targetValid && g_targetBarrier <= 0.f;
    // Raw "whip wants to fire this frame". Must include the Skill1 cooldown — otherwise
    // abilityImminent stays true while the whip recharges and the pre-melee keeps
    // re-engaging (the "melee twice then whip" bug). (Whip's range normally doesn't
    // overlap melee range, so this gates the bash in practice — but it covers whip too
    // if Whip Min Dist is lowered below melee range.)
    bool whipInRange   = g_targetVisible && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist;
    bool whipClear     = g_targetBarrier <= 0.f && !g_targetShielded;
    bool whipWantsRaw  = g_autoWhip && g_targetValid && whipInRange && whipClear
                         && now >= g_postBashWhipSuppress;

    g_dbgWhipReady   = true;  // LMouse has no cooldown
    g_dbgWhipInRange = whipInRange;
    g_dbgWhipClear   = whipClear;
    g_dbgWhipWant    = whipWantsRaw;

    bool abilityImminent = ((wantBash && bashReady) || whipWantsRaw)
                           && g_bashPhase == BA_IDLE && g_whipHoldEnd <= 0.f;

    bool preMeleeActive = false;
    if (g_meleeCancel && inMelee && abilityImminent && !g_meleeCancelDone)
    {
        if (canSwing)
        {
            if (shieldUp)
            {
                // Shield is already up — pressing LMB now would Shield Bash, not swing. Hold the
                // ability AND the swing timer; the unified SetRMouse(false) below lowers the
                // shield. Once it's down (shieldUp clears) we fall through to the real swing.
                preMeleeActive   = true;
                g_meleeCancelEnd = 0.f;   // don't start/burn the swing window until the shield is down
            }
            else
            {
                if (g_meleeCancelEnd <= 0.f) g_meleeCancelEnd = now + g_meleeCancelTime; // begin swing
                if (now < g_meleeCancelEnd)  preMeleeActive = true;                       // swinging — hold ability
                else { g_meleeCancelEnd = 0.f; g_meleeCancelDone = true; }                // swing landed — release
            }
        }
        else
        {
            // in range but can't swing (barrier up / auto-melee off): don't block the ability
            g_meleeCancelEnd = 0.f; g_meleeCancelDone = true;
        }
    }
    // Once the ability is no longer imminent (it fired, or target left), re-arm for next time.
    if (!abilityImminent) { g_meleeCancelEnd = 0.f; g_meleeCancelDone = false; }
    g_dbgPreMelee = preMeleeActive;

    // Hard gate: in melee range with melee-cancel on, the bash may ONLY fire once the
    // melee-first swing is satisfied (g_meleeCancelDone) — never before it.
    bool meleeFirstOk = !g_meleeCancel || !inMelee || g_meleeCancelDone;
    if (wantBash && g_bashPhase == BA_IDLE && bashReady && !preMeleeActive && meleeFirstOk)
    {
        g_bashPhase    = BA_RAISING;
        g_bashPhaseT   = 0.f;
        g_whipPreAimEnd = 0.f;
        g_whipHoldEnd   = 0.f;
        g_meleeCancelDone = false;   // re-arm the pre-swing for the next bash
    }

    // RMouse (shield) is driven once, by the unified handler at the end of this
    // block — these phases only manage the bash timing + LMouse so the shield is
    // never released and re-pressed mid-action (no RMB "tapping").
    if (g_bashPhase == BA_RAISING)
    {
        g_bashPhaseT += dt;
        ReleaseGameButton(GameButton::LMouse);   // shield up, no bash yet
        // Bash the instant the REAL shield read (S3) confirms the shield is up — pressing LMB
        // before then would just melee. The closed-loop SetRMouse(true) (driven at the end of
        // the frame) raises it; ShieldIsUp() flips true as soon as the game deploys it.
        // kRaiseTimeout is a fallback so we never hang in RAISING if the read never flips.
        if (ShieldIsUp() || g_bashPhaseT >= kRaiseTimeout)
        {
            g_bashPhase  = BA_BASHING;
            g_bashPhaseT = 0.f;
            // Start the cooldown the moment the bash FIRES (entering BASHING = first LMouse
            // press), not when the bash animation finishes — that's when the real ability
            // goes on cooldown, so rdy lines up with the game.
            g_bashCdEnd  = now + kBashCd;
        }
    }
    else if (g_bashPhase == BA_BASHING)
    {
        g_bashPhaseT += dt;
        PressGameButton(GameButton::LMouse);     // bash while shield held
        g_dbgLMouseDown = true;
        if (g_bashPhaseT >= kBashHold)
        {
            ReleaseGameButton(GameButton::LMouse);
            g_bashPhase          = BA_IDLE;
            g_bashPhaseT         = 0.f;
            g_postBashWhipSuppress  = now + 0.6f;               // don't whip right after bash
            g_postBashMeleeSuppress = now + g_postBashMeleeDelay; // don't melee right after bash (tunable)
            // (cooldown was already started on bash fire, in the RAISING->BASHING transition)
            // Leave the belief at "up" (don't reset g_shieldUp): the shield does not drop
            // on its own after the bash, so the end-of-action SetRMouse(false) below sees
            // belief==up != desired==down and sends a real lowering click.
        }
    }

    // Unified RMouse (shield) control: held continuously through the whole bash
    // AND across a bash->block handoff. Single Press/Release per frame, so the
    // shield never gets released-then-repressed within an action.
    bool bashActive  = (g_bashPhase != BA_IDLE);
    bool blockActive = blockPending && !bashActive && !preMeleeActive;
    g_dbgBashPhase    = (int)g_bashPhase;
    g_dbgEngaged      = engaged;
    g_dbgHeld         = held;
    g_dbgKeyDown      = keyDown;
    g_dbgBlockPending = blockPending;
    g_dbgBashActive   = bashActive;
    bool wantRMouse = bashActive || blockActive;
    g_dbgRMouseDown = wantRMouse;
    if (g_bashPhase == BA_BASHING)
    {
        // Shield was already confirmed up entering BASHING. Do NOT run the closed-loop
        // corrective here — a single-frame flicker of IsSkill3Active() to 0 would make it
        // click RMB and toggle the shield DOWN mid-bash, whiffing it. Just hold: RMB stays
        // released and the toggle keeps the shield up for the whole LMB bash.
        ReleaseGameButton(GameButton::RMouse);
        g_shieldClickEnd  = 0.f;
        g_shieldSettleEnd = 0.f;
    }
    else
    {
        SetRMouse(wantRMouse);
    }

    // track what we send to LMouse this frame for debug
    g_dbgLMouseDown = false;

    // -------------------------------------------------------
    // Auto Whip trigger — pre-aim at prediction, then fire
    // -------------------------------------------------------
    bool whipReady    = false;
    bool whipPreAiming = false;
    // meleeFirstOk: in melee range with melee-cancel on, the whip may only fire once the
    // melee-first swing is done — same hard gate as the bash, so it can't jump the queue.
    bool whipConditions = !bashActive && !preMeleeActive && meleeFirstOk
        && now >= g_postBashWhipSuppress
        && keyDown && g_autoWhip && g_targetValid && g_targetVisible
        && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist
        && g_targetBarrier <= 0.f && !g_targetShielded;

    if (whipConditions && g_whipHoldEnd <= 0.f)
    {
        if (g_whipPreAimEnd <= 0.f)
        {
            g_whipPreAimEnd = now + kWhipPreAim;
            whipPreAiming = true;
            whipReady = true;
        }
        else if (now < g_whipPreAimEnd)
        {
            whipPreAiming = true;
            whipReady = true;
        }
        else
        {
            g_whipHoldEnd   = now + kWhipHold;
            g_whipPreAimEnd = 0.f;
            whipReady = true;
            g_meleeCancelDone = false;
        }
    }
    else if (!whipConditions && g_whipHoldEnd <= 0.f)
    {
        g_whipPreAimEnd = 0.f;
    }
    bool whipQueued = (!bashActive && g_whipHoldEnd > 0.f);

    // -------------------------------------------------------
    // Auto Whip Shot press (aim during both pre-aim and fire)
    // -------------------------------------------------------
    bool whipFiring = false;
    if ((whipQueued || whipPreAiming) && !repairFiring)
    {
        WeaponInfo wi{};
        float speed = (GetWeaponInfo(InputFlag::Skill1, wi) && wi.projectileSpeed > 0.f)
                      ? wi.projectileSpeed : g_whipSpeed;
        float   ttt  = (speed > 0.f) ? g_targetDist / speed : 0.f;
        Vector3 base = g_targetBodyPos.IsValid() ? g_targetBodyPos : g_targetHeadPos;
        Vector3 pred = base;
        // SDK PredictPosition: server-side lead using entity velocity/acceleration
        Entity wtgt(g_cachedTarget);
        if (wtgt.IsValid() && g_targetPos.IsValid())
        {
            Vector3 predEntity = wtgt.PredictPosition(ttt);
            if (predEntity.IsValid())
            {
                pred.x = predEntity.x + (base.x - g_targetPos.x);
                pred.y = predEntity.y + (base.y - g_targetPos.y);
                pred.z = predEntity.z + (base.z - g_targetPos.z);
            }
        }
        AimAtPosition(pred, g_whipStiffness);

        if (whipQueued)
        {
            if (now < g_whipHoldEnd && g_targetVisible)
            {
                PressGameButton(GameButton::Skill1);
                whipFiring = true;
            }
            else
            {
                ReleaseGameButton(GameButton::Skill1);
                g_whipHoldEnd = 0.f;
            }
        }
    }

    // -------------------------------------------------------
    // Auto Rocket Flail (LMouse) — melee range, not while bashing/whipping/blocking
    // -------------------------------------------------------
    if (bashActive)
    {
        if (g_targetValid)
            AimAtBone(g_cachedTarget, g_bestBone, g_meleeStiffness);
        // bash state machine manages RMouse/LMouse
    }
    else if (!repairFiring && g_autoMelee && inMelee && g_targetValid && !whipReady && !blockActive
             && g_targetBarrier <= 0.f   // note: no shield-raycast gate (it flickers); barrier only
             && !shieldUp                // don't LMB while OUR shield is up — that bashes, not melees.
                                         // SetRMouse(false) lowers it first; we swing once it's down.
             && now >= g_postBashMeleeSuppress)  // brief delay after a bash before meleeing again (tunable)
    {
        AimAtBone(g_cachedTarget, g_bestBone, g_meleeStiffness);
        PressGameButton(GameButton::LMouse);
        g_dbgLMouseDown = true;
    }
    else
    {
        if (!repairFiring && !whipReady && (held || inMelee || inWhipRange || duelClose) && g_targetValid)
            AimAtBone(g_cachedTarget, g_bestBone, g_meleeStiffness);
        ReleaseGameButton(GameButton::LMouse);
    }
}

extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Brigitte) return; }  // dormant on any other hero

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    if (g_drawFov)
        Draw::Circle(center, g_fovRadius, Color(255, 255, 255, 60), 1.f);

    // Live gate readout on the overlay — computed HERE (not from on_frame's frozen
    // g_dbg* values) so it updates every frame even when the trigger isn't held.
    if (g_showDebug)
    {
        float  nowD  = GetTime();
        Entity lpD   = LocalPlayer();

        bool dBashReady   = nowD >= g_bashCdEnd;
        bool dBashInRange = g_targetValid && g_targetVisible && g_targetDist <= g_bashRange;
        bool dBashLowHp   = g_targetHp > 0.f && g_targetHp <= g_bashKillHp;
        bool dBashGroupOk = g_nearbyEnemyCount < g_bashGroupMin;
        bool dBashWant    = g_autoBash && dBashInRange && dBashLowHp && dBashGroupOk;

        bool dWhipReady   = true;  // LMouse, no cooldown
        bool dWhipInRange = g_targetVisible && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist;
        bool dWhipClear   = g_targetBarrier <= 0.f && !g_targetShielded;
        bool dWhipWant    = g_autoWhip && g_targetValid && dWhipReady && dWhipInRange && dWhipClear;

        float bx = 12.f, by = 120.f;
        TextBuilder<128> bt;
        bt.put("BASH ").put(dBashWant ? "WANT" : "----")
          .put("  rdy:").put(dBashReady ? "Y" : "n")
          .put(" rng:").put(dBashInRange ? "Y" : "n")
          .put(" hp:").put(dBashLowHp ? "Y" : "n")
          .put(" grp:").put(dBashGroupOk ? "Y" : "n");
        Draw::TextShadow(bx, by, dBashWant ? Color::Green() : Color(255,180,0,255), bt.c_str());
        by += 18.f;
        TextBuilder<192> bt2;
        bt2.put("auto:").put(g_autoBash ? "Y" : "n")
           .put(" trig:").put(g_triggerKey.IsDown() ? "Y" : "n")
           .put(" dist:").putFloat(g_targetDist, 1)
           .put("/").putFloat(g_bashRange, 1)
           .put(" hp:").putFloat(g_targetHp, 0)
           .put("/").putFloat(g_bashKillHp, 0)
           .put("  preMelee:").put(g_dbgPreMelee ? "SWING" : (g_meleeCancelDone ? "done" : "-"))
           .put(" shld:").put(g_targetShielded ? "Y" : "n")
           .put(" bar:").putFloat(g_targetBarrier, 0);
        Draw::TextShadow(bx, by, Color::White(), bt2.c_str());

        // My-shield + post-bash melee suppression readout (for tuning the combo timing)
        by += 18.f;
        float meleeSupp = g_postBashMeleeSuppress - nowD; if (meleeSupp < 0.f) meleeSupp = 0.f;
        TextBuilder<128> mb;
        mb.put("myShield:").put(IsSkill3Active() ? "UP" : "down")
          .put("  postBashMeleeSupp:").putFloat(meleeSupp, 2).put("s")
          .put(" (delay ").putFloat(g_postBashMeleeDelay, 2).put(")");
        Draw::TextShadow(bx, by, IsSkill3Active() ? Color::Cyan() : Color::White(), mb.c_str());

        // Live whip-gate readout — any 'n' is the blocker
        by += 18.f;
        TextBuilder<160> wt;
        wt.put("WHIP ").put(dWhipWant ? "WANT" : "----")
          .put("  rdy:").put(dWhipReady ? "Y" : "n")
          .put(" rng:").put(dWhipInRange ? "Y" : "n")
          .put(" clr:").put(dWhipClear ? "Y" : "n")
          .put("  dist:").putFloat(g_targetDist, 1)
          .put(" (").putFloat(g_whipMinDist, 0).put("-").putFloat(g_whipMaxDist, 0).put(")");
        Draw::TextShadow(bx, by, dWhipWant ? Color::Green() : Color(255,180,0,255), wt.c_str());

        // RMouse driver diagnostic
        by += 18.f;
        const char* phaseStr = g_dbgBashPhase == 1 ? "RAISE" : g_dbgBashPhase == 2 ? "BASH" : "idle";
        TextBuilder<192> rm;
        rm.put("RMOUSE:").put(g_dbgRMouseDown ? "DOWN" : "up  ")
          .put(" LMOUSE:").put(g_dbgLMouseDown ? "DOWN" : "up  ")
          .put("  phase:").put(phaseStr)
          .put(" eng:").put(g_dbgEngaged ? "Y" : "n")
          .put(" held:").put(g_dbgHeld ? "Y" : "n")
          .put(" key:").put(g_dbgKeyDown ? "Y" : "n")
          .put(" blk:").put(g_dbgBlockPending ? "Y" : "n")
          .put(" bash:").put(g_dbgBashActive ? "Y" : "n")
          .put(" clk:").put(g_shieldClickEnd > 0.f ? "Y" : "n");   // a shield toggle click is in progress
        Draw::TextShadow(bx, by, g_dbgRMouseDown ? Color::Green() : Color::White(), rm.c_str());

        // ── FULL skill-indicator dump — one row per slot. Trigger a Shield Bash and watch
        // which field MOVES; that's the indicator the bash lives in (cd=recharge, dur=active
        // duration, act=active flag). Header then S1/S2/S3/Ult.
        by += 18.f;
        Draw::TextShadow(bx, by, Color(180,180,180,255), "SKILLS   cd cur/max en   dur cur/max en   act");
        if (lpD.IsValid())
        {
            struct { const char* name; SkillCooldown cd; SkillCooldown dur; int act; } rows[4] = {
                { "S1 ", lpD.GetSkill1Cooldown(), lpD.GetSkill1Duration(), IsSkill1Active()?1:0 },
                { "S2 ", lpD.GetSkill2Cooldown(), lpD.GetSkill2Duration(), IsSkill2Active()?1:0 },
                { "S3 ", lpD.GetSkill3Cooldown(), lpD.GetSkill3Duration(), IsSkill3Active()?1:0 },
                { "Ult", lpD.GetUltCooldown(),    lpD.GetUltDuration(),    -1 },
            };
            for (int i = 0; i < 4; i++)
            {
                by += 16.f;
                TextBuilder<192> r;
                r.put(rows[i].name)
                 .put("  ").putFloat(rows[i].cd.current,1).put("/").putFloat(rows[i].cd.max,1)
                 .put(" ").put(rows[i].cd.enabled?"Y":"n")
                 .put("   ").putFloat(rows[i].dur.current,1).put("/").putFloat(rows[i].dur.max,1)
                 .put(" ").put(rows[i].dur.enabled?"Y":"n")
                 .put("   ").put(rows[i].act < 0 ? "-" : (rows[i].act ? "1" : "0"));
                // highlight any slot that is currently "doing something"
                bool live = rows[i].cd.enabled || rows[i].dur.enabled || rows[i].act > 0;
                Draw::TextShadow(bx, by, live ? Color::Green() : Color::White(), r.c_str());
            }
        }

        // Repair-pack scan readout (counts are from last frame's ally scan)
        by += 18.f;
        TextBuilder<128> rt;
        rt.put("REPAIR ").put(g_repairTarget >= 0 ? "TGT" : "----")
          .put(" auto:").put(g_autoRepair ? "Y" : "n")
          .put(" fire:").put(g_dbgRepairFiring ? "Y" : "n")
          .put(" allies:").putInt(g_dbgAllyCount);
        Draw::TextShadow(bx, by, g_repairTarget >= 0 ? Color::Green() : Color(255,180,0,255), rt.c_str());
        by += 18.f;
        TextBuilder<128> rt2;
        rt2.put("rej hp:").putInt(g_dbgRejHp)
           .put(" rng:").putInt(g_dbgRejRange)
           .put(" los:").putInt(g_dbgRejLOS)
           .put("  closest:").putFloat(g_dbgClosestDist, 1).put("m ")
           .putFloat(g_dbgClosestHpPct, 0).put("%");
        Draw::TextShadow(bx, by, Color::White(), rt2.c_str());
    }

    // Count all players unconditionally to detect when game filters the array
    {
        int total = 0, enemies = 0, visible = 0, inFov = 0;
        for (Entity p : Players())
        {
            total++;
            if (!p.IsEnemy()) continue;
            enemies++;
            if (!p.IsAlive() || !p.IsVisible()) continue;
            visible++;
            Vector3 hw = p.GetHeadPos();
            if (!hw.IsValid()) continue;
            Vector2 hs;
            if (!WorldToScreen(hw, hs)) continue;
            if (ScreenDist(hs, center) <= g_fovRadius) inFov++;
        }
        g_dbgTotalPlayers = total;
        g_dbgEnemyCount   = enemies;
        g_dbgEnemyVisible = visible;
        g_dbgEnemyInFov   = inFov;
    }

    bool triggerHeld  = g_triggerKey.IsDown();
    float now2        = GetTime();
    bool duelEngaged  = g_duelMode &&
        (g_reactionTimer > 0.f || (g_targetValid && g_targetDist <= g_duelRange));
    g_duelEngaged = duelEngaged;  // cache for on_frame

    // ── Helper: ally scan (reused in both repairActive and normal modes) ──
    auto doAllyScan = [&]()
    {
        Entity  localE = LocalPlayer();
        Vector3 myPos  = localE.IsValid() ? localE.GetPosition() : Vector3{};
        Vector3 eyePos;
        if (!GetCameraPosition(eyePos)) eyePos = myPos;
        g_dbgAllyCount = 0; g_dbgRejHp = 0; g_dbgRejRange = 0; g_dbgRejLOS = 0;
        g_dbgClosestDist = 99999.f; g_dbgClosestHpPct = 0.f;
        int32_t healIdx = -1; float healScore = 99999.f; Vector3 healPos = {};
        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsAlly()) continue;
            g_dbgAllyCount++;
            Vector3 allyPos = p.GetPosition();
            float   d       = localE.IsValid() ? WorldDist(myPos, allyPos) : 99999.f;
            float   hpPct   = p.GetHealthPercent();
            float   absHp   = p.GetHealth();
            if (d < g_dbgClosestDist) { g_dbgClosestDist = d; g_dbgClosestHpPct = hpPct; }
            if (p.IsFullHealth() || hpPct > g_repairHpPct) { g_dbgRejHp++;    continue; }
            if (d > g_repairRange)                      { g_dbgRejRange++; continue; }
            Vector3 allyHead = p.GetHeadPos();
            // Only heal allies in line of sight — don't throw packs through walls.
            // IsVisible() isn't reliable for allies, so use an LOS raycast (same call
            // brigitte already uses for g_targetShielded). Skip the test if head pos is
            // invalid rather than wrongly rejecting the ally.
            if (allyHead.IsValid() && !IsPointVisible(eyePos, allyHead)) { g_dbgRejLOS++; continue; }
            Vector2 allyScreen;
            bool inFov = allyHead.IsValid() && WorldToScreen(allyHead, allyScreen)
                         && ScreenDist(allyScreen, center) <= g_fovRadius;
            if (!inFov) continue;
            if (absHp < healScore) { healScore = absHp; healIdx = p.Index(); healPos = allyHead; }
        }
        g_repairTarget      = healIdx;
        g_repairTargetPos   = healPos;
        g_repairTargetInFov = (healIdx >= 0);
    };

    {
        // ── Enemy scan — runs EVERY frame so target/debug values stay live even when the
        // trigger isn't held. (Firing still only happens in on_frame while the trigger is held.)
        bool reactionActive = g_reactionTimer > 0.f;
        {
            int32_t bestIdx   = -1;
            float   bestScore = 99999.f;
            float   bestHp    = 0.f;
            Entity  bestEntity;
            Entity  reactEntity;
            Entity  localP2   = LocalPlayer();
            Vector3 myPos3    = localP2.IsValid() ? localP2.GetPosition() : Vector3{};

            for (Entity p : Players())
            {
                if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
                // Always track the reaction target so we can update its data
                if (reactionActive && p.Index() == g_cachedTarget) reactEntity = p;
                Vector3 headWorld = p.GetHeadPos();
                if (!headWorld.IsValid()) continue;
                float dist3d = WorldDist(myPos3, p.GetPosition());
                if (dist3d > g_targetMaxRange) continue;
                if (!p.IsTargetable()) continue;  // skip sleeping/CC'd enemies
                if (!p.IsVisible()) continue;     // don't track enemies through walls
                Vector2 headScreen;
                if (!WorldToScreen(headWorld, headScreen)) continue;
                float fovCheck = (dist3d <= g_meleeRange) ? g_meleeFovRadius : g_whipFovRadius;
                if (ScreenDist(headScreen, center) > fovCheck) continue;
                float score = (g_targetMode == 1) ? p.GetHealth()
                           : (g_targetMode == 2) ? ScreenDist(headScreen, center)
                           : dist3d;
                if (score < bestScore)
                { bestScore = score; bestIdx = p.Index(); bestHp = p.GetHealth(); bestEntity = p; }
            }

            // Reaction owns g_cachedTarget — update data for that enemy, don't reset smoothing
            Entity  tgt    = reactionActive ? reactEntity : bestEntity;
            int32_t useIdx = reactionActive ? g_cachedTarget : bestIdx;
            float   useHp  = reactionActive ? (reactEntity.IsValid() ? reactEntity.GetHealth() : g_targetHp) : bestHp;

            if (useIdx >= 0 && tgt.IsValid())
            {
                if (!reactionActive && useIdx != g_cachedTarget) AimResetSmoothing();
                if (!reactionActive) g_cachedTarget = useIdx;
                g_targetValid  = true;
                g_targetHp     = useHp;
                Vector3 headPos = tgt.GetHeadPos();
                g_targetHeadPos = headPos;
                g_targetPos     = tgt.GetPosition();
                g_targetDist = headPos.IsValid()
                               ? WorldDist(myPos3, g_targetPos) : 99999.f;
                float nowV = GetTime();
                if (g_prevHeadTime > 0.f && (nowV - g_prevHeadTime) > 0.001f)
                {
                    float invDt = 1.f / (nowV - g_prevHeadTime);
                    g_targetVelocity.x = (headPos.x - g_prevHeadPos.x) * invDt;
                    g_targetVelocity.y = (headPos.y - g_prevHeadPos.y) * invDt;
                    g_targetVelocity.z = (headPos.z - g_prevHeadPos.z) * invDt;
                }
                g_prevHeadPos  = headPos;
                g_prevHeadTime = nowV;
                g_targetVisible = tgt.IsVisible();
                g_targetBarrier = tgt.GetBarrier();

                // Cache body pos for whip prediction (computed FIRST so the LOS
                // raycast below uses this frame's position, not last frame's).
                Vector3 bodyPos = tgt.GetBonePos(Bone::Body);
                if (!bodyPos.IsValid()) bodyPos = tgt.GetBonePos(Bone::Chest);
                g_targetBodyPos = bodyPos.IsValid() ? bodyPos : g_targetHeadPos;

                // Real line-of-sight gate: raycast camera→body AND camera→head. If
                // geometry is hit well BEFORE the target on both rays, there's a wall
                // (or shield) in the way — don't whip through it. IsPointVisible() reports
                // clear through walls here, so use the full Raycast and check the hit
                // fraction: fraction near 1.0 = hit at/near the target (clear); a small
                // fraction = geometry in between (blocked).
                g_targetShielded = false;
                {
                    Vector3 camPos;
                    if (GetCameraPosition(camPos))
                    {
                        auto clearTo = [&](const Vector3& to) -> bool {
                            if (!to.IsValid()) return false;
                            RaycastResult rc = Raycast(camPos, to);
                            return !rc.IsHit() || rc.fraction > 0.90f;   // reached (near) the target
                        };
                        bool bodyClear = clearTo(g_targetBodyPos);
                        bool headClear = clearTo(g_targetHeadPos);
                        g_targetShielded = !bodyClear && !headClear;
                    }
                }

                bool inMeleeNow = g_targetDist <= g_meleeRange;
                {
                    bool*  boneSet    = inMeleeNow ? g_meleeBoneEnabled : g_whipBoneEnabled;
                    float  hitboxScl  = inMeleeNow ? g_meleeHitboxScale : g_whipHitboxScale;
                    float closestBone = 99999.f;
                    g_bestBone = Bone::Body;
                    for (int b = 0; b < kAimBoneCount; b++)
                    {
                        if (!boneSet[b]) continue;
                        Vector3 bw = tgt.GetBonePos(kAimBones[b]);
                        if (!bw.IsValid()) continue;
                        Vector2 bs;
                        if (!WorldToScreen(bw, bs)) continue;
                        float d = ScreenDist(bs, center);
                        if (d < closestBone) { closestBone = d; g_bestBone = kAimBones[b]; }
                    }
                    g_cachedHitbox = AimHitsHitbox(g_cachedTarget, hitboxScl);
                }
            }
            else if (!reactionActive)
            {
                g_targetValid = false; g_cachedTarget = -1; g_cachedHitbox = -1;
                g_targetDist = 99999.f; g_targetVelocity = {}; g_prevHeadTime = 0.f;
                g_targetBodyPos = {}; g_targetPos = {}; g_targetVisible = false; g_targetBarrier = 0.f; g_targetShielded = false;
            }
        }

        doAllyScan();
    }

}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Brigitte"))
    {
        ImGui::Checkbox("Enabled",      &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Auto Melee",   &g_autoMelee);
        ImGui::Checkbox("Auto Whip",    &g_autoWhip);
        ImGui::Checkbox("Auto Bash",    &g_autoBash);
        ImGui::Checkbox("Auto Block",   &g_autoBlock);
        ImGui::Checkbox("Duel Mode",      &g_duelMode);
        ImGui::SliderFloat("Duel Range (m)",    &g_duelRange,      2.f,  20.f);
        ImGui::Checkbox("Auto Reaction",  &g_autoReaction);
        ImGui::SliderFloat("Reaction Min Dmg",  &g_reactionMinDmg, 5.f, 100.f);
        ImGui::SliderFloat("Reaction Duration", &g_reactionDur,    0.5f,  3.f);
        ImGui::Checkbox("Ult Detection",  &g_ultDetect);
        if (g_ultDetect)
        {
            TextBuilder<64> ultInfo;
            ultInfo.put("Watching: ").put(g_ultWatchId == 0 ? "any hero" : GetHeroName(g_ultWatchId));
            ImGui::Text(ultInfo.c_str());
            bool doSet = false;
            if (ImGui::Checkbox("Set from nearest enemy##ult", &doSet) && doSet)
            {
                Entity localP = LocalPlayer();
                Vector3 myPos = localP.IsValid() ? localP.GetPosition() : Vector3{};
                float bestDist = 99999.f;
                for (Entity p : Players())
                {
                    if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
                    Vector3 ep = p.GetPosition();
                    float dx = ep.x-myPos.x, dy = ep.y-myPos.y, dz = ep.z-myPos.z;
                    float d = Sqrt(dx*dx+dy*dy+dz*dz);
                    if (d < bestDist) { bestDist = d; g_ultWatchId = p.GetHeroId(); }
                }
            }
            bool doClear = false;
            if (ImGui::Checkbox("Clear hero ID##ult", &doClear) && doClear)
                g_ultWatchId = 0;
        }
        ImGui::Checkbox("Auto Repair",  &g_autoRepair);
        ImGui::SliderInt("No-Bash Group Size",  &g_bashGroupMin,   1, 5);
        ImGui::SliderFloat("No-Bash Group Range (m)", &g_bashGroupRange, 1.f, 15.f);
        ImGui::Separator();
        ImGui::SliderFloat("Melee Smoothing", &g_meleeStiffness, 0.f, 1500.f);
        ImGui::SliderFloat("Whip Smoothing",  &g_whipStiffness,  0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius",      &g_fovRadius,    10.f,  500.f);
        ImGui::Checkbox("Draw FOV",           &g_drawFov);
        ImGui::Checkbox("Show Debug Overlay", &g_showDebug);
        ImGui::Combo("Target Mode", &g_targetMode, "Closest Distance\0Lowest HP\0Closest Crosshair\0");
        g_triggerKey.Render("Trigger Key");
        ImGui::Checkbox("Shield Hold Test Mode", &g_shieldHoldMode);
        ImGui::Combo("  Shield Test Input", &g_shieldHoldMethod, "RMouse\0Skill1 button\0");
        ImGui::Separator();
        ImGui::SliderFloat("Melee Range (m)",     &g_meleeRange,       2.f,   10.f);
        ImGui::SliderFloat("Melee FOV Radius",    &g_meleeFovRadius,   50.f,  600.f);
        ImGui::SliderFloat("Melee Hitbox Scale",  &g_meleeHitboxScale, 0.5f,  3.f);
        ImGui::SliderFloat("Whip Min Dist",    &g_whipMinDist,    0.f,   15.f);
        ImGui::SliderFloat("Whip Max Dist",    &g_whipMaxDist,    5.f,   30.f);
        ImGui::SliderFloat("Whip FOV Radius",   &g_whipFovRadius,   50.f,  500.f);
        ImGui::SliderFloat("Whip Hitbox Scale", &g_whipHitboxScale,  0.5f,  3.f);
        ImGui::SliderFloat("Whip Aim Tolerance (px)", &g_whipAimTolerance, 2.f, 100.f);
        ImGui::SliderFloat("Target Max Range",&g_targetMaxRange, 5.f,  60.f);
        ImGui::SliderFloat("Whip Speed",      &g_whipSpeed,    5.f,   80.f);
        ImGui::SliderFloat("Bash Range (m)",  &g_bashRange,  1.f,  10.f);
        ImGui::SliderFloat("Bash Kill HP",    &g_bashKillHp, 0.f,  500.f);
        ImGui::Checkbox("Melee-Cancel (swing before bash/whip)", &g_meleeCancel);
        if (g_meleeCancel)
            ImGui::SliderFloat("Melee-Cancel Time (s)", &g_meleeCancelTime, 0.05f, 1.0f);
            ImGui::SliderFloat("Post-Bash Melee Delay (s)", &g_postBashMeleeDelay, 0.0f, 1.0f);
        ImGui::SliderFloat("Block HP Drop",   &g_blockHpDrop,  1.f,   50.f);
        ImGui::SliderFloat("Repair HP %",      &g_repairHpPct,  10.f, 99.f);
        ImGui::SliderFloat("Repair Range (m)", &g_repairRange,   5.f, 40.f);
        ImGui::SliderFloat("Hitbox Scale",    &g_hitboxScale,  0.7f,  1.5f);
        ImGui::Separator();
        ImGui::Text("Whip Bones:");
        for (int b = 0; b < kAimBoneCount; b++)
        {
            TextBuilder<32> wb; wb.put("W:").put(kBoneNames[b]);
            ImGui::Checkbox(wb.c_str(), &g_whipBoneEnabled[b]);
        }
        ImGui::Separator();
        ImGui::Text("Melee Bones:");
        for (int b = 0; b < kAimBoneCount; b++)
        {
            TextBuilder<32> mb; mb.put("M:").put(kBoneNames[b]);
            ImGui::Checkbox(mb.c_str(), &g_meleeBoneEnabled[b]);
        }
        ImGui::Separator();
        TextBuilder<48> hb;
        hb.put("Hero ID: ").putInt((int)g_heroId).put(g_heroId == 0 ? "  (any)" : "  (locked)");
        ImGui::Text(hb.c_str());
        bool prevLock = g_heroLock;
        ImGui::Checkbox("Lock to current hero", &g_heroLock);
        if (g_heroLock != prevLock)
        {
            if (g_heroLock) g_heroId = LocalPlayer().GetHeroId();
            else            g_heroId = 0;
        }
        ImGui::Separator();
        {
            TextBuilder<80> db;
            db.put("Tgt:").put(g_targetValid ? "YES" : "NO")
              .put(" dist:").putFloat(g_targetDist, 1)
              .put(" hp:").putFloat(g_targetHp, 0)
              ;
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("barrier:").putFloat(g_targetBarrier, 1)
              .put(" shielded:").put(g_targetShielded ? "YES" : "NO");
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("Repair tgt:").putInt(g_repairTarget)
              .put(" firing:").put(g_dbgRepairFiring ? "YES" : "NO");
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("Ally:").putInt(g_dbgAllyCount)
              .put(" rejHP:").putInt(g_dbgRejHp)
              .put(" rejRng:").putInt(g_dbgRejRange)
              .put(" rejLOS:").putInt(g_dbgRejLOS);
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("Trigger:").put(g_dbgTriggerHeld ? "HELD" : "off")
              .put(" S1cd:").put(g_dbgS1OnCd ? "YES" : "NO")
              .put(" S2cd:").put(g_dbgS2OnCd ? "YES" : "NO");
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("P:").putInt(g_dbgTotalPlayers)
              .put(" E:").putInt(g_dbgEnemyCount)
              .put(" Vis:").putInt(g_dbgEnemyVisible)
              .put(" FOV:").putInt(g_dbgEnemyInFov);
            ImGui::Text(db.c_str());
        }
        {
            // Bash gate: every flag must be YES for a standalone bash to fire
            TextBuilder<96> db;
            db.put("Bash want:").put(g_dbgBashWant ? "YES" : "no")
              .put(" rdy:").put(g_dbgBashReady ? "Y" : "n")
              .put(" rng:").put(g_dbgBashInRange ? "Y" : "n")
              .put(" hp:").put(g_dbgBashLowHp ? "Y" : "n")
              .put(" grp:").put(g_dbgBashGroupOk ? "Y" : "n");
            ImGui::Text(db.c_str());
        }
    }
}
