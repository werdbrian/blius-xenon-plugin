#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "autoblock", "Auto Block", "c",
    "Auto-triggers defensive abilities on incoming CC/damage threats", "2.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ─────────────────────────────────────────────
//  Threat table
// ─────────────────────────────────────────────

enum ThreatIdx
{
    THR_ZARYA_ULT = 0,   // Graviton Surge    — ultActive  rising edge
    THR_ILLARI_ULT,       // Captive Sun       — ultActive  rising edge
    THR_ANA_SLEEP,        // Sleep Dart        — skill1Active rising edge
    THR_BAP_IMMO,         // Immortality Field — skill2Active rising edge
    THR_CASS_NADE,        // Magnetic Grenade  — skill2Active rising edge
    THR_MEI_ULT,          // Blizzard          — ultActive  rising edge
    THR_ORISA_JAVELIN,    // Energy Javelin    — skill3Active rising edge
    THR_SIGMA_ROCK,       // Accretion         — skill3Active rising edge
    THR_GENJI_DEFLECT,    // Deflect           — skill2Active rising edge
    THR_ORISA_SPIN,       // Javelin Spin      — skill2Active rising edge
    THR_ANRAN_E,          // Anran E ability   — skill2Active rising edge
    THR_DVA_DM,           // Defense Matrix    — skill2Active rising edge
    THR_COUNT
};

static const char* kThreatLabel[THR_COUNT] = {
    "Zarya Ult",
    "Illari Ult",
    "Ana Sleep Dart",
    "Bap Immo Field",
    "Cass Nade",
    "Mei Ult (Blizzard)",
    "Orisa Energy Javelin",
    "Sigma Rock",
    "Genji Deflect",
    "Orisa Javelin Spin",
    "Anran E",
    "D.Va Defense Matrix",
};

static const char* kThreatKey[THR_COUNT] = {
    "zarya_ult", "illari_ult", "ana_sleep",  "bap_immo",
    "cass_nade", "mei_ult",    "orisa_jav",  "sigma_rock",
    "genji_def", "orisa_spin", "anran_e",    "dva_dm",
};

// ─────────────────────────────────────────────
//  Defensive hero table
// ─────────────────────────────────────────────

enum DefType { DEF_NONE, DEF_SKILL1, DEF_SKILL2, DEF_ULT };

enum DefHeroIdx { DH_MEI=0, DH_TRACER, DH_REAPER, DH_DOOM, DH_MOIRA, DH_SIGMA, DH_COUNT };

struct DefHeroInfo
{
    const char* label;
    const char* key;
    uint64_t    heroId;
    DefType     defType;
    float       defaultHp;   // suggested starting HP threshold for this hero
};

static const DefHeroInfo kDefHero[DH_COUNT] = {
    { "Mei (Cryo-Freeze)",        "mei",    HeroId::Mei,      DEF_SKILL1,  80.f },
    { "Tracer (Recall)",          "tracer", HeroId::Tracer,   DEF_SKILL2,  50.f },
    { "Reaper (Wraith Form)",     "reaper", HeroId::Reaper,   DEF_SKILL1,  80.f },
    { "Doomfist (Meteor Strike)", "doom",   HeroId::Doomfist, DEF_ULT,    150.f },
    { "Moira (Fade)",             "moira",  HeroId::Moira,    DEF_SKILL1,  70.f },
    { "Sigma (Kinetic Grasp)",    "sigma",  HeroId::Sigma,    DEF_SKILL2, 120.f },
};

// ─────────────────────────────────────────────
//  Per-hero config
// ─────────────────────────────────────────────

struct HeroCfg
{
    bool  enabled;
    float hpThresh;          // static HP value — 0 disables HP trigger
    float reactDist;         // max enemy distance to react to any threat
    bool  threats[THR_COUNT];
};

static HeroCfg g_heroes[DH_COUNT];
static bool    g_hpArmed[DH_COUNT];  // re-arms when HP rises back above threshold

static void InitDefaults()
{
    for (int i = 0; i < DH_COUNT; ++i)
    {
        g_heroes[i].enabled   = true;
        g_heroes[i].hpThresh  = kDefHero[i].defaultHp;
        g_heroes[i].reactDist = 15.f;
        for (int t = 0; t < THR_COUNT; ++t)
            g_heroes[i].threats[t] = true;
        g_hpArmed[i] = true;
    }
}

// ─────────────────────────────────────────────
//  Global settings
// ─────────────────────────────────────────────

static bool  g_enabled      = true;
static bool  g_debug        = false;
static float g_trigCooldown = 0.5f;

static float       g_lastTriggerAt  = -999.f;
static const char* g_lastThreatName = nullptr;

// Defense press window — PulseGameButton doesn't exist in this SDK, so we
// hold the button briefly (maintained in on_frame) then release.
static uint32_t    g_defBtn         = 0;
static float       g_defHoldEnd     = 0.f;
static constexpr float kDefHold     = 0.10f;

// ─────────────────────────────────────────────
//  Entity snapshot + per-entity prev state
// ─────────────────────────────────────────────

static PluginEntity g_ents[32]{};
static int          g_entCount   = 0;
static Vector3      g_localPos{};
static float        g_localHp    = 0.f;
static float        g_localHpMax = 0.f;

struct PrevState
{
    uint32_t id  = 0;
    bool sk1 = false, sk2 = false, sk3 = false, ult = false;
};
static PrevState g_prev[32]{};

// ─────────────────────────────────────────────
//  Defense helpers
// ─────────────────────────────────────────────

static int LocalHeroIdx()
{
    uint64_t h = LocalPlayer().GetHeroId();
    for (int i = 0; i < DH_COUNT; ++i)
        if (h == kDefHero[i].heroId) return i;
    return -1;
}

static bool IsDefenseReady(DefType t)
{
    switch (t)
    {
    case DEF_SKILL1: return !IsSkill1Active() && GetSkill1Cooldown().current <= 0.f;
    case DEF_SKILL2: return !IsSkill2Active() && GetSkill2Cooldown().current <= 0.f;
    case DEF_ULT:    return IsUltReady() && !IsUltActive();
    default:         return false;
    }
}

static void FireDefense(DefType t)
{
    switch (t)
    {
    case DEF_SKILL1: g_defBtn = GameButton::Skill1; break;
    case DEF_SKILL2: g_defBtn = GameButton::Skill2; break;
    case DEF_ULT:    g_defBtn = GameButton::Ult;    break;
    default: return;
    }
    g_defHoldEnd = GetTime() + kDefHold;
    PressGameButton(g_defBtn);
}

// ─────────────────────────────────────────────
//  Debug HUD
// ─────────────────────────────────────────────

static void DebugHud(int dhIdx, float now)
{
    if (!g_debug) return;

    float x = 10.f, y = 10.f;
    TextBuilder<128> tb;

    Draw::TextShadow(x, y, Color::White(), "[ Auto Block ]"); y += 18.f;

    if (dhIdx < 0)
    {
        Draw::TextShadow(x, y, Color(120, 120, 120, 200), "Hero: not a supported defensive hero");
        return;
    }

    const DefHeroInfo& dh  = kDefHero[dhIdx];
    const HeroCfg&     cfg = g_heroes[dhIdx];
    DefType            def = dh.defType;

    tb.put("Hero:  ").put(dh.label);
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

    bool ready = IsDefenseReady(def);
    Draw::TextShadow(x, y, ready ? Color::Green() : Color(160, 160, 160, 200),
        ready ? "Ready: YES" : "Ready: NO (cooldown)"); y += 16.f;

    // HP
    if (cfg.hpThresh > 0.f)
    {
        tb.put("HP:    ").putFloat(g_localHp, 0).put(" / ").putFloat(g_localHpMax, 0);
        tb.put("  (thresh ").putFloat(cfg.hpThresh, 0).put(")");
        Color hpCol = g_localHp <= cfg.hpThresh ? Color::Red() : Color(160, 160, 160, 200);
        Draw::TextShadow(x, y, hpCol, tb.c_str()); y += 16.f; tb.clear();
    }

    tb.put("Dist:  ").putFloat(cfg.reactDist, 0).put("m");
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

    y += 6.f;

    if (g_lastThreatName)
    {
        tb.put("Last:  ").put(g_lastThreatName);
        tb.put(" (").putFloat(now - g_lastTriggerAt, 1).put("s ago)");
        Draw::TextShadow(x, y, Color::Orange(), tb.c_str());
    }
    else
    {
        Draw::TextShadow(x, y, Color(120, 120, 120, 200), "Last:  none");
    }
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

extern "C" void on_load()
{
    InitDefaults();

    g_enabled      = Config::GetBool("enabled",       true);
    g_debug        = Config::GetBool("debug",         false);
    g_trigCooldown = Config::GetFloat("trigCooldown", 0.5f);

    for (int i = 0; i < DH_COUNT; ++i)
    {
        const char* hk = kDefHero[i].key;
        TextBuilder<48> k;

        k.put(hk).put("_en");
        g_heroes[i].enabled = Config::GetBool(k.c_str(), g_heroes[i].enabled); k.clear();

        k.put(hk).put("_hp");
        g_heroes[i].hpThresh = Config::GetFloat(k.c_str(), g_heroes[i].hpThresh); k.clear();

        k.put(hk).put("_dist");
        g_heroes[i].reactDist = Config::GetFloat(k.c_str(), g_heroes[i].reactDist); k.clear();

        for (int t = 0; t < THR_COUNT; ++t)
        {
            k.put(hk).put("_t_").put(kThreatKey[t]);
            g_heroes[i].threats[t] = Config::GetBool(k.c_str(), g_heroes[i].threats[t]); k.clear();
        }
    }
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",       g_enabled);
    Config::SetBool("debug",         g_debug);
    Config::SetFloat("trigCooldown", g_trigCooldown);

    for (int i = 0; i < DH_COUNT; ++i)
    {
        const char* hk = kDefHero[i].key;
        TextBuilder<48> k;

        k.put(hk).put("_en");
        Config::SetBool(k.c_str(), g_heroes[i].enabled); k.clear();

        k.put(hk).put("_hp");
        Config::SetFloat(k.c_str(), g_heroes[i].hpThresh); k.clear();

        k.put(hk).put("_dist");
        Config::SetFloat(k.c_str(), g_heroes[i].reactDist); k.clear();

        for (int t = 0; t < THR_COUNT; ++t)
        {
            k.put(hk).put("_t_").put(kThreatKey[t]);
            Config::SetBool(k.c_str(), g_heroes[i].threats[t]); k.clear();
        }
    }

    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled",   &g_enabled);
    ImGui::Checkbox("Debug HUD", &g_debug);
    ImGui::SliderFloat("Trigger Cooldown (s)", &g_trigCooldown, 0.1f, 3.f);
    ImGui::Separator();

    for (int i = 0; i < DH_COUNT; ++i)
    {
        if (!ImGui::CollapsingHeader(kDefHero[i].label)) continue;

        TextBuilder<96> label;
        const char* hk = kDefHero[i].key;

        label.put("Enabled##").put(hk);
        ImGui::Checkbox(label.c_str(), &g_heroes[i].enabled); label.clear();

        if (!g_heroes[i].enabled) continue;

        label.put("HP Threshold (0=off)##").put(hk);
        ImGui::SliderFloat(label.c_str(), &g_heroes[i].hpThresh, 0.f, 500.f); label.clear();
        ImGui::Tooltip("Fires defense when your HP drops to or below this value. 0 disables.");

        label.put("React Distance (m)##").put(hk);
        ImGui::SliderFloat(label.c_str(), &g_heroes[i].reactDist, 1.f, 50.f); label.clear();
        ImGui::Tooltip("Only react to threats fired by enemies within this distance.");

        ImGui::Text("Incoming CC:");
        for (int t = 0; t < THR_COUNT; ++t)
        {
            label.put(kThreatLabel[t]).put("##").put(hk);
            ImGui::Checkbox(label.c_str(), &g_heroes[i].threats[t]); label.clear();
        }
    }
}

// ─────────────────────────────────────────────
//  on_frame — entity snapshot + state cleanup
// ─────────────────────────────────────────────

extern "C" void on_frame(float)
{
    // Maintain the defense press window, then release.
    if (g_defHoldEnd > 0.f)
    {
        if (GetTime() < g_defHoldEnd) PressGameButton(g_defBtn);
        else { ReleaseGameButton(g_defBtn); g_defHoldEnd = 0.f; }
    }

    g_entCount = GetPlayerCount();
    if (g_entCount > 32) g_entCount = 32;

    for (int i = 0; i < g_entCount; ++i)
    {
        xn_get_entity(i, &g_ents[i]);
        if (g_ents[i].isLocalPlayer)
        {
            g_localPos   = g_ents[i].position;
            g_localHp    = g_ents[i].health;
            g_localHpMax = g_ents[i].maxHealth;
        }
    }

    // Clear prev slots for entities no longer present
    for (int j = 0; j < 32; ++j)
    {
        if (!g_prev[j].id) continue;
        bool found = false;
        for (int i = 0; i < g_entCount; ++i)
            if (g_ents[i].id == g_prev[j].id) { found = true; break; }
        if (!found) g_prev[j] = {};
    }
}

// ─────────────────────────────────────────────
//  on_render — threat scan + auto-block trigger
// ─────────────────────────────────────────────

extern "C" void on_render()
{
    float now   = GetTime();
    int   dhIdx = LocalHeroIdx();

    bool shouldBlock = g_enabled && dhIdx >= 0 && g_heroes[dhIdx].enabled;

    if (!shouldBlock)
        return;

    HeroCfg& cfg     = g_heroes[dhIdx];
    DefType  def     = kDefHero[dhIdx].defType;
    bool defReady    = IsDefenseReady(def);
    bool offCooldown = (now - g_lastTriggerAt) >= g_trigCooldown;
    float distLim    = cfg.reactDist * cfg.reactDist;

    // ── CC threat scan ────────────────────────────────────────────────────────
    for (int i = 0; i < g_entCount; ++i)
    {
        const PluginEntity& e = g_ents[i];
        if (!e.alive || e.isLocalPlayer) continue;
        if (!xn_is_entity_enemy(i)) continue;

        float dx = e.position.x - g_localPos.x;
        float dy = e.position.y - g_localPos.y;
        float dz = e.position.z - g_localPos.z;
        if (dx*dx + dy*dy + dz*dz > distLim) continue;

        // Find or allocate prev state slot
        PrevState* ps       = nullptr;
        PrevState* freeSlot = nullptr;
        for (int j = 0; j < 32; ++j)
        {
            if (g_prev[j].id == e.id) { ps = &g_prev[j]; break; }
            if (!freeSlot && g_prev[j].id == 0) freeSlot = &g_prev[j];
        }
        if (!ps)
        {
            if (!freeSlot) continue;
            freeSlot->id  = e.id;
            freeSlot->sk1 = freeSlot->sk2 = freeSlot->sk3 = freeSlot->ult = false;
            ps = freeSlot;
        }

        bool cur_sk1 = e.skill1Active.x > 0.5f;
        bool cur_sk2 = e.skill2Active.x > 0.5f;
        bool cur_sk3 = e.skill3Active.x > 0.5f;
        bool cur_ult = e.ultActive != 0;

        auto tryTrigger = [&](ThreatIdx ti, bool rising)
        {
            if (!rising || !cfg.threats[ti]) return;
            if (!defReady || !offCooldown) return;
            defReady        = false;
            offCooldown     = false;
            g_lastTriggerAt  = now;
            g_lastThreatName = kThreatLabel[ti];
            FireDefense(def);
        };

        if (e.heroId == HeroId::Zarya)
            tryTrigger(THR_ZARYA_ULT,     cur_ult  && !ps->ult);
        if (e.heroId == HeroId::Illari)
            tryTrigger(THR_ILLARI_ULT,    cur_ult  && !ps->ult);
        if (e.heroId == HeroId::Ana)
            tryTrigger(THR_ANA_SLEEP,     cur_sk1  && !ps->sk1);
        if (e.heroId == HeroId::Baptiste)
            tryTrigger(THR_BAP_IMMO,      cur_sk2  && !ps->sk2);
        if (e.heroId == HeroId::Cassidy)
            tryTrigger(THR_CASS_NADE,     cur_sk2  && !ps->sk2);
        if (e.heroId == HeroId::Mei)
            tryTrigger(THR_MEI_ULT,       cur_ult  && !ps->ult);
        if (e.heroId == HeroId::Orisa)
        {
            tryTrigger(THR_ORISA_JAVELIN, cur_sk3  && !ps->sk3);
            tryTrigger(THR_ORISA_SPIN,    cur_sk2  && !ps->sk2);
        }
        if (e.heroId == HeroId::Sigma)
            tryTrigger(THR_SIGMA_ROCK,    cur_sk3  && !ps->sk3);
        if (e.heroId == HeroId::Genji)
            tryTrigger(THR_GENJI_DEFLECT, cur_sk2  && !ps->sk2);
        if (e.heroId == HeroId::Anran)
            tryTrigger(THR_ANRAN_E,       cur_sk2  && !ps->sk2);
        if (e.heroId == HeroId::Dva)
            tryTrigger(THR_DVA_DM,        cur_sk2  && !ps->sk2);

        ps->sk1 = cur_sk1;
        ps->sk2 = cur_sk2;
        ps->sk3 = cur_sk3;
        ps->ult = cur_ult;
    }

    // ── HP threshold ─────────────────────────────────────────────────────────
    if (cfg.hpThresh > 0.f && g_localHpMax > 0.f)
    {
        if (g_localHp > cfg.hpThresh)
        {
            g_hpArmed[dhIdx] = true;
        }
        else if (g_hpArmed[dhIdx])
        {
            bool ready = IsDefenseReady(def);
            if (ready && (now - g_lastTriggerAt) >= g_trigCooldown)
            {
                g_hpArmed[dhIdx] = false;
                g_lastTriggerAt  = now;
                g_lastThreatName = "Low HP";
                FireDefense(def);
            }
        }
    }

    DebugHud(dhIdx, now);
}
