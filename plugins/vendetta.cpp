// vendetta.cpp
// Hold trigger key: aim assist + auto melee (LMouse)
// Auto Soaring Slice (E) when target in gap-close range
// Auto Whirlwind Dash (Shift) at melee range
// Auto Warding Stance (RMouse) when HP drops — also fires Projected Edge if LMouse held

#include <xenon/SDK.hpp>
using namespace xenon;

static bool    g_enabled      = true;
static int     g_triggerKey   = 18;    // VK_MENU = Left Alt
static float   g_stiffness    = 30.f;
static float   g_fovRadius    = 200.f;
static int     g_targetMode   = 0;    // 0=closest crosshair, 1=lowest HP
static float   g_hitboxScale  = 1.0f;
static bool    g_autoShoot    = true;

// Soaring Slice (E / Skill2) — gap closer
static bool    g_autoSlice    = true;
static float   g_sliceMinDist = 6.f;
static float   g_sliceMaxDist = 14.f;  // Soaring Slice max range is 15m

// Whirlwind Dash (Shift / Skill1) — spin AoE, use only when already in melee, not to close gap
static bool    g_autoDash     = true;
static float   g_dashRange    = 4.f;

// Kill thresholds — abilities only fire when target HP is low enough to secure the kill
static float   g_abilityKillHp = 400.f;  // Soaring Slice (leads to 120 overhead crit)
static float   g_dashKillHp    = 70.f;   // Whirlwind Dash max damage is 70 at center

// Escape dash — fires Whirlwind without trigger key when taking heavy damage
static float   g_escapeDashHpDrop = 20.f;
static bool    g_escapePending    = false;

// Auto melee range — engage LMouse + RMouse without trigger key when this close
static bool    g_autoMeleeRange_en = true;
static float   g_meleeAutoRange    = 5.f;

// Auto block (Warding Stance / RMouse)
static bool    g_autoBlock       = true;
static float   g_blockHpDrop     = 5.f;   // HP drop per frame to trigger block
static float   g_blockDuration   = 0.5f;  // how long to hold block after hit detected
static float   g_blockHoldEnd    = 0.f;
static float   g_prevHp          = -1.f;

// Hold timers — press+hold pattern for reliable SDK input
static float   g_sliceHoldEnd  = 0.f;
static float   g_dashHoldEnd   = 0.f;
static float   g_lastSliceAt   = 0.f;   // when Soaring Slice last fired
static constexpr float kHoldDur       = 0.12f;
static constexpr float kOverheadWindow = 1.5f;  // Soaring Slice overhead duration (0.64s + 0.74s + buffer)

// Opener combo: beam (Projected Edge = RMouse+LMouse) → Soaring Slice (E)
static bool    g_autoOpener    = true;
static float   g_beamKillHp    = 150.f;        // also beam when target HP is this low (poke/kill)
static float   g_beamHoldEnd   = 0.f;
static float   g_lastBeamAt    = -99.f;
static constexpr float kBeamHold        = 0.12f;
static constexpr float kBeamToSlice     = 0.15f;  // delay between beam and E
static constexpr float kBeamCooldown    = 3.0f;   // opener cooldown
static constexpr float kPokeCooldown    = 1.0f;   // poke/kill beam cooldown
static constexpr float kOverheadStrikeAt = 1.0f;  // time after E when overhead has landed

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
static int32_t g_cachedTarget = -1;
static bool    g_targetValid  = false;
static float   g_targetHp     = 0.f;
static float   g_targetDist   = 9999.f;
static int     g_cachedHitbox = -1;
static int     g_bestBone     = Bone::Chest;

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
    g_triggerKey    = Config::GetInt("triggerKey",       18);
    g_stiffness     = Config::GetFloat("stiffness",      30.f);
    g_fovRadius     = Config::GetFloat("fovRadius",      200.f);
    g_targetMode    = Config::GetInt("targetMode",       0);
    g_hitboxScale   = Config::GetFloat("hitboxScale",    1.0f);
    g_autoShoot     = Config::GetBool("autoShoot",       true);
    g_autoSlice     = Config::GetBool("autoSlice",       true);
    g_sliceMinDist  = Config::GetFloat("sliceMinDist",   6.f);
    g_sliceMaxDist  = Config::GetFloat("sliceMaxDist",   14.f);
    g_autoOpener    = Config::GetBool("autoOpener",      true);
    g_beamKillHp    = Config::GetFloat("beamKillHp",     150.f);
    g_autoDash          = Config::GetBool("autoDash",            true);
    g_dashRange         = Config::GetFloat("dashRange",          4.f);
    g_abilityKillHp     = Config::GetFloat("abilityKillHp",     400.f);
    g_dashKillHp        = Config::GetFloat("dashKillHp",        70.f);
    g_escapeDashHpDrop  = Config::GetFloat("escapeDashHpDrop",  20.f);
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
    Config::SetInt("triggerKey",      g_triggerKey);
    Config::SetFloat("stiffness",     g_stiffness);
    Config::SetFloat("fovRadius",     g_fovRadius);
    Config::SetInt("targetMode",      g_targetMode);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetBool("autoShoot",      g_autoShoot);
    Config::SetBool("autoSlice",      g_autoSlice);
    Config::SetFloat("sliceMinDist",  g_sliceMinDist);
    Config::SetFloat("sliceMaxDist",  g_sliceMaxDist);
    Config::SetBool("autoOpener",      g_autoOpener);
    Config::SetFloat("beamKillHp",     g_beamKillHp);
    Config::SetBool("autoDash",            g_autoDash);
    Config::SetFloat("dashRange",          g_dashRange);
    Config::SetFloat("abilityKillHp",      g_abilityKillHp);
    Config::SetFloat("dashKillHp",         g_dashKillHp);
    Config::SetFloat("escapeDashHpDrop",   g_escapeDashHpDrop);
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

extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; g_prevHp = -1.f; g_lastSliceAt = 0.f; g_lastBeamAt = -99.f; g_beamHoldEnd = 0.f; g_quickMeleeEnd = 0.f; g_escapePending = false; AimResetSmoothing(); }

// ── frame logic ───────────────────────────────────────────────────────────────

extern "C" void on_frame(float dt)
{
    (void)dt;
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    float  now   = GetTime();
    Entity local = LocalPlayer();

    // HP tracking — always runs (block + escape dash both need it)
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
        if (g_autoDash && drop >= g_escapeDashHpDrop)
            g_escapePending = true;
        g_prevHp = hp;
    }

    bool held         = IsKeyDown(g_triggerKey);
    bool overheadDone = (now - g_lastSliceAt) > kOverheadWindow;

    // Arm escape dash — fires Whirlwind without trigger key when taking heavy damage
    if (g_autoDash && g_escapePending && g_dashHoldEnd <= 0.f)
    {
        SkillCooldown s1 = local.IsValid() ? local.GetSkill1Cooldown() : SkillCooldown{};
        if (!s1.IsOnCooldown())
        {
            g_dashHoldEnd   = now + kHoldDur;
            g_escapePending = false;
        }
    }

    // Arm quick melee follow-up after overhead swing (target must be in melee range)
    float timeSinceSlice = (g_lastSliceAt > 0.f) ? (now - g_lastSliceAt) : 99.f;
    bool inQuickMeleeRange = g_targetValid && g_targetDist <= g_meleeAutoRange;
    if (g_autoShoot && inQuickMeleeRange
        && timeSinceSlice >= kOverheadStrikeAt
        && timeSinceSlice < kOverheadWindow
        && g_quickMeleeEnd <= 0.f)
    {
        g_quickMeleeEnd = now + kHoldDur;
    }

    bool abilityActive   = (now < g_sliceHoldEnd) || (now < g_dashHoldEnd);
    bool inMeleeRange    = g_autoMeleeRange_en && g_targetValid && g_targetDist <= g_meleeAutoRange;
    bool overheadPending = !overheadDone;

    // Warding Stance (RMouse) — hold when: damage timer active, OR within melee range
    // Skipped during overhead window — LMouse + RMouse = Projected Edge, not overhead strike
    bool shouldBlock = !overheadPending
                       && ((!abilityActive && g_autoBlock && now < g_blockHoldEnd)
                           || (inMeleeRange && !abilityActive));
    if (shouldBlock)
        PressGameButton(GameButton::RMouse);
    else
        ReleaseGameButton(GameButton::RMouse);

    // Auto sword melee (LMouse) — trigger key required
    if (g_autoShoot)
    {
        bool shouldSwing = held && g_targetValid && g_cachedHitbox >= 0
                           && g_targetDist <= g_meleeAutoRange;
        if (shouldSwing)
            PressGameButton(GameButton::LMouse);
        else
            ReleaseGameButton(GameButton::LMouse);
    }

    // Quick V-melee (GameButton::Melee) after overhead swing
    if (now < g_quickMeleeEnd)
        PressGameButton(GameButton::Melee);
    else
    {
        ReleaseGameButton(GameButton::Melee);
        g_quickMeleeEnd = 0.f;
    }

    // Whirlwind Dash button — unified: handles both escape (no trigger) and kill (trigger)
    if (now < g_dashHoldEnd)
        PressGameButton(GameButton::Skill1);
    else
    {
        ReleaseGameButton(GameButton::Skill1);
        g_dashHoldEnd = 0.f;
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

    // Arm beam: opener (entering range) OR poke/kill (target low HP)
    // Energy gate disabled — SDK doesn't expose Vendetta block energy correctly (s3Active returns 0)
    bool canPoke   = g_targetHp > 0.f && g_targetHp <= g_beamKillHp;
    bool pokeReady = (now - g_lastBeamAt) >= kPokeCooldown;
    if (g_autoOpener && inSliceRange && g_beamHoldEnd <= 0.f
        && (!beamRecent || (canPoke && pokeReady)))
    {
        g_beamHoldEnd = now + kBeamHold;
        g_lastBeamAt  = now;
    }

    // Soaring Slice (E): opener follow-up after beam, OR kill confirm
    bool canSlice  = g_targetHp > 0.f && g_targetHp <= g_abilityKillHp;
    bool openerE   = g_autoOpener && inSliceRange && beamRecent
                     && (now - g_lastBeamAt) >= kBeamToSlice;
    // E must wait kBeamToSlice after beam — keeps beam→E ordering, but fires standalone
    // if beam never armed (g_lastBeamAt stays -99 → check is always true)
    bool killSlice = g_autoSlice && canSlice && inSliceRange
                     && (now - g_lastBeamAt) >= kBeamToSlice;
    if ((openerE || killSlice) && g_sliceHoldEnd <= 0.f)
    {
        SkillCooldown s2 = local.GetSkill2Cooldown();
        if (!s2.IsOnCooldown())
        {
            g_sliceHoldEnd = now + kHoldDur;
            g_lastSliceAt  = now;
        }
    }

    // Arm kill dash — target must be killable and in melee range; overhead must be done
    if (g_autoDash && canDash && overheadDone && g_dashHoldEnd <= 0.f)
    {
        SkillCooldown s1 = local.GetSkill1Cooldown();
        if (!s1.IsOnCooldown() && g_targetDist <= g_dashRange)
            g_dashHoldEnd = now + kHoldDur;
    }

    // Beam button: RMouse + LMouse = Projected Edge
    // Suppressed during overhead window (would convert overhead strike to Projected Edge)
    if (now < g_beamHoldEnd)
    {
        if (!overheadPending)
        {
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

static bool g_showEnergyDebug = true;

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
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    bool held = IsKeyDown(g_triggerKey);

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    int32_t bestIdx   = -1;
    float   bestScore = 99999.f;
    float   bestHp    = 0.f;
    float   bestDist  = 9999.f;
    Entity  bestEnt;
    bool    lockedAlive = false;
    Entity  lockedEnt;

    for (Entity p : Players())
    {
        if (!p.IsAlive() || p.IsLocal() || !p.IsVisible()) continue;

        Vector3 headWorld = p.GetBonePos(Bone::Head);
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;

        float dist = WorldDist(local.GetPosition(), p.GetPosition());

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

        // Aim assist only when trigger held; hitbox check always needed for auto-shoot
        if (held) AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
        g_cachedHitbox = AimHitsHitbox(g_cachedTarget, g_hitboxScale);
    }
    else
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_cachedHitbox = -1;
    }

    // Energy debug panel — always visible (before early-out so trigger key not needed)
    if (g_showEnergyDebug)
    {
        float ex = 10.f, ey = 20.f;
        const float EL = 14.f;
        Draw::TextShadow(ex, ey, Color::Cyan(), "-- ENERGY DEBUG (hold/use RMouse block) --", 11); ey += EL;

        // Read raw PluginEntity for local player — exposes both .x and .y of each skill active vec
        PluginEntity ld{};
        xn_get_local_player(&ld);
        DrawEnergyBar(ex, ey, "s1A.x:", ld.skill1Active.x, 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s1A.y:", ld.skill1Active.y, 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2A.x:", ld.skill2Active.x, 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2A.y:", ld.skill2Active.y, 3.f, Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s3A.x:", ld.skill3Active.x, 3.f, Color::Cyan());      ey += EL;
        DrawEnergyBar(ex, ey, "s3A.y:", ld.skill3Active.y, 3.f, Color::Cyan());      ey += EL;

        // Durations (current/max) — Vendetta block might use duration field
        SkillCooldown d1 = local.GetSkill1Duration();
        SkillCooldown d2 = local.GetSkill2Duration();
        SkillCooldown d3 = local.GetSkill3Duration();
        DrawEnergyBar(ex, ey, "s1Dur:", d1.current, d1.max > 0 ? d1.max : 3.f, Color(120,180,255)); ey += EL;
        DrawEnergyBar(ex, ey, "s2Dur:", d2.current, d2.max > 0 ? d2.max : 3.f, Color(120,180,255)); ey += EL;
        DrawEnergyBar(ex, ey, "s3Dur:", d3.current, d3.max > 0 ? d3.max : 3.f, Color(120,180,255)); ey += EL;

        // HP-adjacent fields — energy can hide in barrier/armor/overhealth slots
        DrawEnergyBar(ex, ey, "armor:",  ld.armor,       300.f, Color(180,140,40));  ey += EL;
        DrawEnergyBar(ex, ey, "barrier:",ld.barrier,     300.f, Color(80,140,255));  ey += EL;
        DrawEnergyBar(ex, ey, "ovrhp:",  ld.overhealth,  300.f, Color(120,255,120)); ey += EL;

        // Ult charge
        DrawEnergyBar(ex, ey, "ultChg:", GetUltCharge(), 100.f, Color(220,200,0)); ey += EL;

        // Generic lookup skill scanner — probe candidate IDs near known Vendetta range
        // Known IDs: Sojourn=0x00F6, UltCharge=0x00F8, Hanzo=0x00C9, Illari=0x0651, Sombra=0x0C41
        static const uint16_t kProbes[] = { 0x00F7, 0x00F9, 0x00FA, 0x0652, 0x0653, 0x0C42, 0x0C43 };
        for (uint16_t pid : kProbes)
        {
            float v = GetLookupSkill(pid);
            if (v == 0.f) continue;  // skip empty
            TextBuilder<24> lb;
            lb.put("lk").putInt(pid).put(":");
            DrawEnergyBar(ex, ey, lb.c_str(), v, 3.f, Color(255,180,255)); ey += EL;
        }

        Draw::TextShadow(ex, ey, Color::Yellow(), "energy gate: DISABLED", 11);
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

    // Energy debug panel — always visible when enabled, use RMouse to watch which bar moves
    if (g_showEnergyDebug)
    {
        float ex = 10.f, ey = 450.f;
        const float EL = 14.f;
        Draw::TextShadow(ex, ey, Color::Cyan(), "-- ENERGY DEBUG (hold RMouse) --", 11); ey += EL;
        DrawEnergyBar(ex, ey, "s3Active:", xn_is_skill3_active(), 3.f,   Color::Cyan());    ey += EL;
        DrawEnergyBar(ex, ey, "ultChg%: ", GetUltCharge(),        100.f, Color(220,200,0)); ey += EL;
        DrawEnergyBar(ex, ey, "s1Active:", xn_is_skill1_active(), 3.f,   Color(180,180,180)); ey += EL;
        DrawEnergyBar(ex, ey, "s2Active:", xn_is_skill2_active(), 3.f,   Color(180,180,180)); ey += EL;
        // beam gate: shows which bar the current condition reads
        float s3val = xn_is_skill3_active();
        bool  gated = s3val >= 2.0f;
        TextBuilder<32> tb2;
        tb2.put("beam gate: ").put(gated ? "OPEN" : "BLOCKED");
        Draw::TextShadow(ex, ey, gated ? Color::Green() : Color::Red(), tb2.c_str(), 11);
    }
}

// ── menu ──────────────────────────────────────────────────────────────────────

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Vendetta"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        ImGui::Checkbox("Show Energy Debug HUD", &g_showEnergyDebug);
        if (!g_enabled) return;
        ImGui::Separator();

        ImGui::SliderInt("Trigger Key (VK)", &g_triggerKey, 0, 255);
        ImGui::SliderFloat("Smoothing (0=snap)", &g_stiffness, 0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius", &g_fovRadius, 10.f, 500.f);
        ImGui::Combo("Target Mode", &g_targetMode, "Closest Crosshair\0Lowest HP\0");
        ImGui::SliderFloat("Hitbox Scale", &g_hitboxScale, 0.7f, 1.5f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Melee (LMouse)", &g_autoShoot);
        ImGui::Separator();

        ImGui::SliderFloat("Slice Kill HP",  &g_abilityKillHp, 1.f, 500.f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Opener (beam -> E) + Poke", &g_autoOpener);
        if (g_autoOpener)
            ImGui::SliderFloat("Beam/Poke Kill HP", &g_beamKillHp, 1.f, 500.f);
        ImGui::Checkbox("Auto Soaring Slice (E kill confirm)", &g_autoSlice);
        if (g_autoSlice || g_autoOpener)
        {
            ImGui::SliderFloat("Slice Min Dist (m)", &g_sliceMinDist, 1.f, 15.f);
            ImGui::SliderFloat("Slice Max Dist (m)", &g_sliceMaxDist, 5.f, 30.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Whirlwind Dash (Shift)", &g_autoDash);
        if (g_autoDash)
        {
            ImGui::SliderFloat("Dash Range (m)",         &g_dashRange,         1.f,  6.f);
            ImGui::SliderFloat("Dash Kill HP",           &g_dashKillHp,        1.f,  200.f);
            ImGui::SliderFloat("Escape HP Drop Trigger", &g_escapeDashHpDrop,  1.f,  100.f);
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
            ImGui::SliderFloat("Block Hold Duration",   &g_blockDuration,  0.1f, 1.5f);
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
