// Brigitte Plugin
// Auto Rocket Flail (melee), auto Whip Shot with prediction, auto Shield Bash combo, auto block

#include <xenon/SDK.hpp>
using namespace xenon;

static float    g_stiffness       = 30.f;   // legacy / fallback
static float    g_meleeStiffness  = 30.f;
static float    g_whipStiffness   = 80.f;
static float    g_fovRadius       = 200.f;
static bool     g_drawFov         = true;
static int      g_targetMode      = 0;      // 0=closest crosshair, 1=lowest HP
static int      g_triggerKey      = 1;
static bool     g_enabled         = true;
static bool     g_autoMelee       = true;
static float    g_meleeRange      = 6.f;
static bool     g_autoWhip        = true;
static float    g_whipMinDist     = 7.f;
static float    g_whipMaxDist     = 20.f;
static float    g_whipKillHp      = 500.f;  // effectively always fire by default
static float    g_targetMaxRange  = 25.f;   // ignore enemies beyond this distance (m)
static float    g_whipSpeed       = 30.f;   // m/s projectile speed
static bool     g_autoBash        = false;
static float    g_bashRange       = 5.f;
static float    g_bashKillHp      = 200.f;
static bool     g_autoBlock       = true;
static float    g_blockHpDrop     = 5.f;
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

// Target cache (written in on_render, read in on_frame)
static int32_t  g_cachedTarget    = -1;
static bool     g_targetValid     = false;
static float    g_targetHp        = 0.f;
static float    g_targetDist      = 99999.f;
static int      g_cachedHitbox    = -1;
static int      g_bestBone        = Bone::Chest;
static Vector3  g_targetHeadPos   = {};
static Vector3  g_targetBodyPos   = {};
static bool     g_targetVisible   = false;

// Melee-specific aim settings
static float    g_meleeFovRadius  = 350.f;
static float    g_meleeHitboxScale = 1.3f;

// Velocity tracking for Whip Shot prediction
static Vector3  g_targetVelocity  = {};
static Vector3  g_prevHeadPos     = {};
static float    g_prevHeadTime    = 0.f;

// HP tracking for auto block
static float    g_prevLocalHp     = -1.f;
static float    g_blockHoldEnd    = 0.f;   // reactive HP-drop timer
static float    g_shieldHoldEnd   = 0.f;   // proactive hold timer (refreshed each frame while trigger held)
static constexpr float kBlockDur  = 0.6f;
static constexpr float kShieldGrace = 0.25f; // keep shield up this long after trigger/condition fails

// Shield Bash state machine
enum BashPhase { BA_IDLE, BA_RAISING, BA_BASHING };
static BashPhase g_bashPhase      = BA_IDLE;
static float     g_bashPhaseT     = 0.f;
static float     g_bashCdEnd      = 0.f;
static constexpr float kRaiseDur  = 0.15f;  // hold RMouse before pressing Skill1
static constexpr float kBashHold  = 0.12f;  // hold Skill1 for bash
static constexpr float kBashCd    = 5.f;    // manual bash cooldown tracking

// Whip Shot hold timer
static float    g_whipHoldEnd     = 0.f;
static constexpr float kWhipHold  = 0.12f;
static float    g_velSmoothAlpha  = 0.25f;  // EMA factor for target velocity (0=frozen, 1=raw)
static float    g_whipAimPixels   = 40.f;   // fire only when prediction within N px of center

// Burst Combo: Flail → Bash → Flail → Whip Shot (230 dmg, KOs 225 HP)
enum ComboPhase {
    CB_IDLE,
    CB_FLAIL1,      // hold LMouse for one swing
    CB_BASH,        // press bash key
    CB_FLAIL2,      // hold LMouse for second swing
    CB_WHIP         // press Skill1 (whip shot)
};
static bool       g_autoCombo     = false;
static float      g_comboRange    = 5.f;
static float      g_comboKillHp   = 250.f;
static int        g_bashGroupMin  = 2;     // skip bash when this many enemies in range
static float      g_bashGroupRange = 5.f;  // radius around target to count enemies
static ComboPhase g_combo         = CB_IDLE;
static float      g_comboT        = 0.f;
static float      g_comboCdEnd    = 0.f;
static constexpr float kCbFlail1   = 0.55f;
static constexpr float kCbBashRaise = 0.12f; // hold RMouse to raise shield
static constexpr float kCbBash      = 0.45f; // total bash phase (raise + LMouse press)
static constexpr float kCbFlail2   = 0.55f;
static constexpr float kCbWhip     = 0.10f;
static constexpr float kCbCooldown = 5.5f;

// Repair Pack
static bool     g_autoRepair      = true;
static float    g_repairHpPct     = 70.f;   // heal ally below this % HP
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

// Live debug state updated each frame
static bool     g_dbgTriggerHeld  = false;
static bool     g_dbgRepairFiring = false;
static bool     g_dbgS1OnCd       = false;
static bool     g_dbgS2OnCd       = false;
static int      g_dbgTotalPlayers  = 0;
static int      g_dbgEnemyCount    = 0;
static int      g_dbgEnemyVisible  = 0;   // passed IsAlive+IsEnemy+IsVisible
static int      g_dbgEnemyInFov    = 0;   // also passed head-on-screen + FOV radius

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
    g_targetMode    = Config::GetInt("targetMode",      0);
    g_triggerKey    = Config::GetInt("triggerKey",      1);
    g_enabled       = Config::GetBool("enabled",        true);
    g_autoMelee     = Config::GetBool("autoMelee",      true);
    g_meleeRange    = Config::GetFloat("meleeRange",    6.f);
    g_autoWhip      = Config::GetBool("autoWhip",       true);
    g_whipMinDist   = Config::GetFloat("whipMinDist",   7.f);
    g_whipMaxDist   = Config::GetFloat("whipMaxDist",   20.f);
    g_whipKillHp      = Config::GetFloat("whipKillHp",    500.f);
    g_targetMaxRange  = Config::GetFloat("targetMaxRange", 25.f);
    g_whipSpeed     = Config::GetFloat("whipSpeed",     30.f);
    g_autoBash      = Config::GetBool("autoBash",       true);
    g_bashRange     = Config::GetFloat("bashRange",     5.f);
    g_bashKillHp    = Config::GetFloat("bashKillHp",    200.f);
    g_autoBlock     = Config::GetBool("autoBlock",      true);
    g_duelMode        = Config::GetBool("duelMode",         false);
    g_duelRange       = Config::GetFloat("duelRange",       8.f);
    g_autoReaction    = Config::GetBool("autoReaction",     true);
    g_reactionMinDmg  = Config::GetFloat("reactionMinDmg",  20.f);
    g_reactionDur     = Config::GetFloat("reactionDur",     1.5f);
    g_blockHpDrop   = Config::GetFloat("blockHpDrop",   5.f);
    g_hitboxScale   = Config::GetFloat("hitboxScale",   1.0f);
    g_autoRepair    = Config::GetBool("autoRepair",     true);
    g_repairHpPct   = Config::GetFloat("repairHpPct",  70.f);
    g_repairRange   = Config::GetFloat("repairRange",  25.f);
    g_autoCombo      = Config::GetBool("autoCombo",       false);
    g_comboRange     = Config::GetFloat("comboRange",     5.f);
    g_comboKillHp    = Config::GetFloat("comboKillHp",    250.f);
    g_bashGroupMin   = Config::GetInt("bashGroupMin",     2);
    g_bashGroupRange = Config::GetFloat("bashGroupRange", 5.f);
    g_meleeFovRadius   = Config::GetFloat("meleeFovRadius",   350.f);
    g_meleeHitboxScale = Config::GetFloat("meleeHitboxScale", 1.3f);
    g_whipFovRadius    = Config::GetFloat("whipFovRadius",    200.f);
    g_whipHitboxScale  = Config::GetFloat("whipHitboxScale",  1.f);
    g_velSmoothAlpha   = Config::GetFloat("velSmoothAlpha",   0.25f);
    g_whipAimPixels    = Config::GetFloat("whipAimPixels",    40.f);
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
    Config::SetInt("targetMode",      g_targetMode);
    Config::SetInt("triggerKey",      g_triggerKey);
    Config::SetBool("enabled",        g_enabled);
    Config::SetBool("autoMelee",      g_autoMelee);
    Config::SetFloat("meleeRange",    g_meleeRange);
    Config::SetBool("autoWhip",       g_autoWhip);
    Config::SetFloat("whipMinDist",   g_whipMinDist);
    Config::SetFloat("whipMaxDist",   g_whipMaxDist);
    Config::SetFloat("whipKillHp",    g_whipKillHp);
    Config::SetFloat("targetMaxRange", g_targetMaxRange);
    Config::SetFloat("whipSpeed",     g_whipSpeed);
    Config::SetBool("autoBash",       g_autoBash);
    Config::SetFloat("bashRange",     g_bashRange);
    Config::SetFloat("bashKillHp",    g_bashKillHp);
    Config::SetBool("autoBlock",      g_autoBlock);
    Config::SetBool("duelMode",           g_duelMode);
    Config::SetFloat("duelRange",         g_duelRange);
    Config::SetBool("autoReaction",       g_autoReaction);
    Config::SetFloat("reactionMinDmg",    g_reactionMinDmg);
    Config::SetFloat("reactionDur",       g_reactionDur);
    Config::SetFloat("blockHpDrop",   g_blockHpDrop);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetBool("autoRepair",     g_autoRepair);
    Config::SetFloat("repairHpPct",   g_repairHpPct);
    Config::SetFloat("repairRange",   g_repairRange);
    Config::SetBool("autoCombo",       g_autoCombo);
    Config::SetFloat("comboRange",     g_comboRange);
    Config::SetFloat("comboKillHp",    g_comboKillHp);
    Config::SetInt("bashGroupMin",     g_bashGroupMin);
    Config::SetFloat("bashGroupRange", g_bashGroupRange);
    Config::SetFloat("meleeFovRadius",   g_meleeFovRadius);
    Config::SetFloat("meleeHitboxScale", g_meleeHitboxScale);
    Config::SetFloat("whipFovRadius",    g_whipFovRadius);
    Config::SetFloat("whipHitboxScale",  g_whipHitboxScale);
    Config::SetFloat("velSmoothAlpha",   g_velSmoothAlpha);
    Config::SetFloat("whipAimPixels",    g_whipAimPixels);
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
    g_bashPhase     = BA_IDLE;
    g_combo         = CB_IDLE;
    g_comboT        = 0.f;
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

extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    float now  = GetTime();
    bool  held = IsKeyDown(g_triggerKey);

    // HP drop reaction — face the enemy who just shot you
    if (g_autoReaction)
    {
        Entity localE = LocalPlayer();
        if (localE.IsValid())
        {
            float curHp = localE.GetHealth();
            if (g_prevLocalHp > 0.f && (g_prevLocalHp - curHp) >= g_reactionMinDmg)
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
            g_prevLocalHp = curHp;
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

    // -------------------------------------------------------
    // Auto Repair Pack (Skill2) — fires regardless of combat state
    // Briefly snaps aim to ally, then combat aim resumes
    // -------------------------------------------------------
    // Cancel mid-fire repair if combo started — combo owns all buttons
    if (g_repairHoldEnd > 0.f && g_combo != CB_IDLE)
    {
        ReleaseGameButton(GameButton::Skill2);
        g_repairHoldEnd = 0.f;
    }

    bool repairFiring = false;
    if (held && g_autoRepair && g_repairTarget >= 0 && g_repairHoldEnd <= 0.f && now >= g_repairCdEnd
        && g_combo == CB_IDLE)
    {
        if (local.IsValid())
        {
            g_dbgS1OnCd = local.GetSkill1Cooldown().IsOnCooldown();
            g_dbgS2OnCd = local.GetSkill2Cooldown().IsOnCooldown();
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

    // -------------------------------------------------------
    // Burst Combo: Flail → Bash → Flail → Whip Shot
    // Trigger when target is in close range and combo is off cooldown.
    // Once started, runs to completion regardless of trigger state.
    // -------------------------------------------------------
    if (g_autoCombo && g_combo == CB_IDLE && now >= g_comboCdEnd
        && held && g_targetValid && g_targetVisible && g_targetDist <= g_comboRange
        && g_targetHp > 0.f && g_targetHp <= g_comboKillHp
        && nearbyEnemyCount < g_bashGroupMin)
    {
        g_combo  = CB_FLAIL1;
        g_comboT = 0.f;
    }

    if (g_combo != CB_IDLE)
    {
        g_comboT += dt;
        if (g_targetValid)
            AimAtBone(g_cachedTarget, g_bestBone, g_meleeStiffness);

        switch (g_combo)
        {
            case CB_FLAIL1:
                PressGameButton(GameButton::LMouse);
                ReleaseGameButton(GameButton::RMouse);
                ReleaseGameButton(GameButton::Skill1);
                ReleaseGameButton(GameButton::Melee);
                ReleaseGameButton(GameButton::Crouch);
                if (g_comboT >= kCbFlail1) { g_combo = CB_BASH; g_comboT = 0.f; }
                break;
            case CB_BASH:
                // Skip bash if too many enemies nearby, bash on cooldown, or target not visible
                if (nearbyEnemyCount >= g_bashGroupMin || now < g_bashCdEnd || !g_targetVisible)
                {
                    ReleaseGameButton(GameButton::RMouse);
                    ReleaseGameButton(GameButton::LMouse);
                    g_combo = CB_FLAIL2;
                    g_comboT = 0.f;
                    break;
                }
                // Shield Bash: raise shield (RMouse) first, then bash (LMouse while shield up)
                PressGameButton(GameButton::RMouse);
                if (g_comboT >= kCbBashRaise)
                    PressGameButton(GameButton::LMouse);
                else
                    ReleaseGameButton(GameButton::LMouse);
                ReleaseGameButton(GameButton::Skill1);
                ReleaseGameButton(GameButton::Skill2);
                ReleaseGameButton(GameButton::Melee);
                ReleaseGameButton(GameButton::Crouch);
                if (g_comboT >= kCbBash) { g_combo = CB_FLAIL2; g_comboT = 0.f; g_bashCdEnd = now + kBashCd; }
                break;
            case CB_FLAIL2:
                ReleaseGameButton(GameButton::RMouse);
                ReleaseGameButton(GameButton::Skill1);
                ReleaseGameButton(GameButton::Melee);
                ReleaseGameButton(GameButton::Crouch);
                PressGameButton(GameButton::LMouse);
                if (g_comboT >= kCbFlail2) { g_combo = CB_WHIP; g_comboT = 0.f; }
                break;
            case CB_WHIP:
                ReleaseGameButton(GameButton::LMouse);
                ReleaseGameButton(GameButton::RMouse);
                ReleaseGameButton(GameButton::Melee);
                ReleaseGameButton(GameButton::Crouch);
                {
                    SkillCooldown s1 = local.IsValid() ? local.GetSkill1Cooldown() : SkillCooldown{};
                    if (!s1.IsOnCooldown())
                        PressGameButton(GameButton::Skill1);
                    else
                        ReleaseGameButton(GameButton::Skill1);
                }
                if (g_comboT >= kCbWhip)
                {
                    ReleaseGameButton(GameButton::Skill1);
                    g_combo      = CB_IDLE;
                    g_comboT     = 0.f;
                    g_comboCdEnd = now + kCbCooldown;
                }
                break;
            default: break;
        }
        return;  // combo owns all buttons; skip other combat logic
    }

    bool inMelee     = g_targetValid && g_targetDist <= g_meleeRange;
    bool inWhipRange = g_autoWhip && g_targetValid
                       && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist;
    bool engaged     = held || inMelee || inWhipRange || duelClose;

    if (!engaged)
    {
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::Skill1);
        g_whipHoldEnd = 0.f;
        if (!repairFiring) AimResetSmoothing();
        return;
    }

    bool bashActive = false;

    // Don't whip if combo is primed — save Skill1 cooldown for the combo's whip phase
    bool comboWaiting = g_autoCombo && g_combo == CB_IDLE && now >= g_comboCdEnd
        && g_targetValid && g_targetDist <= g_comboRange
        && g_targetHp > 0.f && g_targetHp <= g_comboKillHp;
    if (comboWaiting) g_whipHoldEnd = 0.f;

    // -------------------------------------------------------
    // Auto Whip trigger — only when whip would secure the kill
    // -------------------------------------------------------
    float   whipTtt  = (g_whipSpeed > 0.f) ? g_targetDist / g_whipSpeed : 0.f;
    Vector3 whipBase = g_targetBodyPos.IsValid() ? g_targetBodyPos : g_targetHeadPos;
    Vector3 whipPred = { whipBase.x + g_targetVelocity.x * whipTtt,
                         whipBase.y + g_targetVelocity.y * whipTtt,
                         whipBase.z + g_targetVelocity.z * whipTtt };

    bool whipReady = !bashActive && !comboWaiting && g_autoWhip && g_targetValid && g_targetVisible
        && g_targetDist >= g_whipMinDist && g_targetDist <= g_whipMaxDist
        && g_targetHp > 0.f && g_targetHp <= g_whipKillHp
        && g_whipHoldEnd <= 0.f;

    if (whipReady)
    {
        // Pre-aim at predicted position — camera catches up over frames
        AimAtPosition(whipPred, g_whipStiffness);

        // Gate fire on how close prediction is to screen center
        SkillCooldown s1 = local.IsValid() ? local.GetSkill1Cooldown() : SkillCooldown{};
        if (!s1.IsOnCooldown())
        {
            Vector2 predScr, sz = ScreenSize(), ctr = { sz.x * 0.5f, sz.y * 0.5f };
            bool aimed = !whipPred.IsValid()
                         || (WorldToScreen(whipPred, predScr) && ScreenDist(predScr, ctr) <= g_whipAimPixels);
            if (aimed)
                g_whipHoldEnd = now + kWhipHold;
        }
    }
    bool whipQueued = (!bashActive && g_whipHoldEnd > 0.f);

    // -------------------------------------------------------
    // Auto Whip Shot press
    // -------------------------------------------------------
    bool whipFiring = false;
    if (whipQueued && !repairFiring)
    {
        if (now < g_whipHoldEnd)
        {
            float   ttt  = (g_whipSpeed > 0.f) ? g_targetDist / g_whipSpeed : 0.f;
            Vector3 base = g_targetBodyPos.IsValid() ? g_targetBodyPos : g_targetHeadPos;
            Vector3 pred = base;
            pred.x += g_targetVelocity.x * ttt;
            pred.y += g_targetVelocity.y * ttt;
            pred.z += g_targetVelocity.z * ttt;
            AimAtPosition(pred, g_whipStiffness);
            PressGameButton(GameButton::Skill1);
            whipFiring = true;
        }
        else
        {
            ReleaseGameButton(GameButton::Skill1);
            g_whipHoldEnd = 0.f;
        }
    }

    // -------------------------------------------------------
    // Auto Rocket Flail (LMouse) — melee range, not while bashing/whipping/blocking
    // -------------------------------------------------------
    if (bashActive)
    {
        ReleaseGameButton(GameButton::LMouse);
    }
    else if (!repairFiring && g_autoMelee && inMelee && g_targetValid && !whipReady)
    {
        AimAtBone(g_cachedTarget, g_bestBone, g_meleeStiffness);
        PressGameButton(GameButton::LMouse);
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
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    if (g_drawFov)
        Draw::Circle(center, g_fovRadius, Color(255, 255, 255, 60), 1.f);

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

    bool triggerHeld  = IsKeyDown(g_triggerKey);
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
        int32_t healIdx = -1; float healScore = 999.f; Vector3 healPos = {};
        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsAlly()) continue;
            g_dbgAllyCount++;
            Vector3 allyPos = p.GetPosition();
            float   d       = localE.IsValid() ? WorldDist(myPos, allyPos) : 99999.f;
            float   hpPct   = p.GetHealthPercent();
            if (d < g_dbgClosestDist) { g_dbgClosestDist = d; g_dbgClosestHpPct = hpPct; }
            if (p.IsFullHealth() || hpPct > g_repairHpPct) { g_dbgRejHp++;    continue; }
            if (d > g_repairRange)                          { g_dbgRejRange++; continue; }
            Vector3 allyHead = p.GetHeadPos();
            Vector2 allyScreen;
            bool inFov = allyHead.IsValid() && WorldToScreen(allyHead, allyScreen)
                         && ScreenDist(allyScreen, center) <= g_fovRadius;
            if (!inFov) continue;
            if (hpPct < healScore) { healScore = hpPct; healIdx = p.Index(); healPos = allyHead; }
        }
        g_repairTarget      = healIdx;
        g_repairTargetPos   = healPos;
        g_repairTargetInFov = (healIdx >= 0);
    };

    {
        // ── Enemy scan (trigger held, duel engaged, reaction active, or enemy in melee range) ──
        bool reactionActive = g_reactionTimer > 0.f;
        bool meleeEngaged = false;
        if (g_autoMelee && !triggerHeld && !duelEngaged && !reactionActive)
        {
            Entity  localM = LocalPlayer();
            Vector3 mPos   = localM.IsValid() ? localM.GetPosition() : Vector3{};
            for (Entity p : Players())
            {
                if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
                Vector3 ePos = p.GetPosition();
                float dx = mPos.x-ePos.x, dy = mPos.y-ePos.y, dz = mPos.z-ePos.z;
                if (Sqrt(dx*dx+dy*dy+dz*dz) <= g_meleeRange) { meleeEngaged = true; break; }
            }
        }
        if (triggerHeld || duelEngaged || reactionActive || meleeEngaged || g_combo != CB_IDLE)
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
                Vector2 headScreen;
                if (!WorldToScreen(headWorld, headScreen)) continue;
                float fovCheck = (dist3d <= g_meleeRange) ? g_meleeFovRadius : g_whipFovRadius;
                if (ScreenDist(headScreen, center) > fovCheck) continue;
                float score = (g_targetMode == 1) ? p.GetHealth() : dist3d;
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
                g_targetDist = headPos.IsValid()
                               ? WorldDist(myPos3, tgt.GetPosition()) : 99999.f;
                float nowV = GetTime();
                if (g_prevHeadTime > 0.f && (nowV - g_prevHeadTime) > 0.001f)
                {
                    float invDt = 1.f / (nowV - g_prevHeadTime);
                    float rawVx = (headPos.x - g_prevHeadPos.x) * invDt;
                    float rawVy = (headPos.y - g_prevHeadPos.y) * invDt;
                    float rawVz = (headPos.z - g_prevHeadPos.z) * invDt;
                    float a = g_velSmoothAlpha;
                    g_targetVelocity.x = a * rawVx + (1.f - a) * g_targetVelocity.x;
                    g_targetVelocity.y = a * rawVy + (1.f - a) * g_targetVelocity.y;
                    g_targetVelocity.z = a * rawVz + (1.f - a) * g_targetVelocity.z;
                }
                g_prevHeadPos  = headPos;
                g_prevHeadTime = nowV;
                g_targetVisible = tgt.IsVisible();

                // Cache body pos for whip prediction
                Vector3 bodyPos = tgt.GetBonePos(Bone::Body);
                if (!bodyPos.IsValid()) bodyPos = tgt.GetBonePos(Bone::Chest);
                g_targetBodyPos = bodyPos.IsValid() ? bodyPos : g_targetHeadPos;

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
                g_targetBodyPos = {}; g_targetVisible = false;
            }
        }
        else
        {
            // Idle: clear enemy targets
            g_targetValid = false; g_cachedTarget = -1; g_cachedHitbox = -1;
            g_targetDist = 99999.f; g_targetVelocity = {}; g_prevHeadTime = 0.f;
            g_targetBodyPos = {}; g_targetVisible = false;
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
        ImGui::Checkbox("Auto Repair",  &g_autoRepair);
        ImGui::Checkbox("Burst Combo",  &g_autoCombo);
        ImGui::SliderInt("No-Bash Group Size",  &g_bashGroupMin,   1, 5);
        ImGui::SliderFloat("No-Bash Group Range (m)", &g_bashGroupRange, 1.f, 15.f);
        ImGui::Separator();
        ImGui::SliderFloat("Melee Smoothing", &g_meleeStiffness, 0.f, 1500.f);
        ImGui::SliderFloat("Whip Smoothing",  &g_whipStiffness,  0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius",      &g_fovRadius,    10.f,  500.f);
        ImGui::Checkbox("Draw FOV",           &g_drawFov);
        ImGui::Combo("Target Mode", &g_targetMode, "Closest Distance\0Lowest HP\0");
        ImGui::SliderInt("Trigger Key",       &g_triggerKey,   0,     31);
        ImGui::Separator();
        ImGui::SliderFloat("Melee Range (m)",     &g_meleeRange,       2.f,   10.f);
        ImGui::SliderFloat("Melee FOV Radius",    &g_meleeFovRadius,   50.f,  600.f);
        ImGui::SliderFloat("Melee Hitbox Scale",  &g_meleeHitboxScale, 0.5f,  3.f);
        ImGui::SliderFloat("Whip Min Dist",    &g_whipMinDist,    2.f,   15.f);
        ImGui::SliderFloat("Whip Max Dist",    &g_whipMaxDist,    5.f,   30.f);
        ImGui::SliderFloat("Whip Kill HP",     &g_whipKillHp,     0.f,   600.f);
        ImGui::SliderFloat("Whip FOV Radius",   &g_whipFovRadius,   50.f,  500.f);
        ImGui::SliderFloat("Whip Hitbox Scale", &g_whipHitboxScale,  0.5f,  3.f);
        ImGui::SliderFloat("Whip Aim Pixels",   &g_whipAimPixels,    5.f,   200.f);
        ImGui::SliderFloat("Vel Smooth Alpha",  &g_velSmoothAlpha,   0.05f, 1.f);
        ImGui::SliderFloat("Target Max Range",&g_targetMaxRange, 5.f,  60.f);
        ImGui::SliderFloat("Whip Speed",      &g_whipSpeed,    5.f,   80.f);
        ImGui::SliderFloat("Bash Range (m)",  &g_bashRange,    1.f,   10.f);
        ImGui::SliderFloat("Bash Kill HP",    &g_bashKillHp,   0.f,   500.f);
        ImGui::SliderFloat("Block HP Drop",   &g_blockHpDrop,  1.f,   50.f);
        ImGui::SliderFloat("Repair HP %",     &g_repairHpPct,  10.f,  99.f);
        ImGui::SliderFloat("Repair Range (m)",&g_repairRange,  5.f,   40.f);
        ImGui::SliderFloat("Combo Range (m)", &g_comboRange,   2.f,   10.f);
        ImGui::SliderFloat("Combo Kill HP",   &g_comboKillHp,  50.f,  500.f);
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
            if (g_heroLock) g_heroId = GetCurrentHero();
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
    }
}
