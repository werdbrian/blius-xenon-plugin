#include <xenon/SDK.hpp>
using namespace xenon;

// ============================================================================
//  ability_tracker — per-entity ability state indicators (v2)
//
//  Colors:
//    RED    — ability READY (threat — can be used against you)
//    GRAY   — on cooldown (safe window)
//    CYAN   — currently ACTIVE / in use
//    YELLOW — partial state (e.g. Zarya 1 bubble)
//    GREEN  — full state   (e.g. Zarya 2 bubbles)
//    ORANGE — ultimate active or ready
//
//  Skill slot mapping (OW2 in-game → SDK field):
//    Skill1 = Shift ability   → skill1Cd / skill1Duration / skill1Active
//    Skill2 = E ability       → skill2Cd / skill2Duration / skill2Active
//    Skill3 = secondary slot  → skill3Cd (used for Sigma rock / Doom punch)
//
//  NOTE: If an indicator never fires, verify hero name strings via debug_hud.
//  NOTE: "anran E" from user request was unrecognised — hero name unknown.
// ============================================================================

XENON_PLUGIN_INFO(
    "ability_tracker", "Ability Tracker", "c", "Tracks abilities per hero", "2.7", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ─────────────────────────────────────────────
//  Hero table
// ─────────────────────────────────────────────

enum {
    H_ROADHOG = 0, H_MEI,      H_SIGMA,    H_DVA,      H_ZARYA,
    H_ANA,         H_BAPTISTE, H_SOMBRA,
    H_TRACER,      H_CASSIDY,  H_VENTURE,  H_LIFEWEAVER, H_KIRIKO,
    H_BASTION,     H_HAZARD,   H_DOOMFIST, H_WINSTON,  H_WBALL,
    H_REINHARDT,   H_REAPER,
    H_ORISA,       H_GENJI,    H_VENDETTA, H_HANZO,    H_ECHO,
    H_MIZUKI,      H_BRIGITTE,
    H_SOJOURN,     H_ASHE,     H_PHARAH,   H_WUYANG,   H_MERCY,
    H_ILLARI,
    H_COUNT
};

static const char* kHeroName[H_COUNT] = {
    "Roadhog",      "Mei",       "Sigma",       "D.Va",      "Zarya",
    "Ana",          "Baptiste",  "Sombra",
    "Tracer",       "Cassidy",   "Venture",     "Lifeweaver","Kiriko",
    "Bastion",      "Hazard",    "Doomfist",    "Winston",   "Wrecking Ball",
    "Reinhardt",    "Reaper",
    "Orisa",        "Genji",     "Vendetta",    "Hanzo",     "Echo",
    "Mizuki",       "Brigitte",  // Vendetta/Mizuki: verify exact names via debug_hud
    "Sojourn",      "Ashe",      "Pharah",      "Wuyang",    "Mercy",
    "Illari"        // Wuyang: verify exact name via debug_hud
};

static const char* kHeroKey[H_COUNT] = {
    "hog",  "mei",  "sig",  "dva",  "zar",
    "ana",  "bap",  "som",
    "tra",  "cas",  "ven",  "lw",   "kir",
    "bas",  "haz",  "doom", "win",  "ball",
    "rein", "rea",
    "ori",  "gen",  "vend", "han",  "echo",
    "miz",  "brig",
    "soj",  "ash",  "pha",  "wuy",  "mer",
    "ill"
};

struct HeroCfg { bool enabled, s1, s2, s3; };
static HeroCfg g_cfg[H_COUNT]{};

// ─────────────────────────────────────────────
//  Global settings
// ─────────────────────────────────────────────

enum { RM_WORLD = 0, RM_HUD_2D };
static const char* kRenderModes = "World\0HUD 2D\0";

static bool  g_enabled     = true;
static bool  g_enemiesOnly = true;
static float g_maxDist     = 40.f;
static int   g_renderMode  = RM_WORLD;
static float g_padX        = -40.f;  // px offset from head (negative = left)
static float g_padY        = -14.f;  // px offset from head (negative = above)
static float g_hudX        = 10.f;
static float g_hudY        = 10.f;

static PluginEntity g_entities[32]{};
static int          g_entityCount = 0;
static Vector3      g_localPos{};
static int32_t      g_localTeam = -1;

// Draw context — set per-entity (world mode) or per-frame (HUD modes)
static float s_lineX    = 0.f;
static float s_lineStep = -14.f;

// Colors — edited live via ImGui::ColorSliders
static Color g_colReady   = Color(255, 0,   0,   255);
static Color g_colCd      = Color(160, 160, 160, 200);
static Color g_colActive  = Color(0,   255, 255, 255);
static Color g_colPartial = Color(255, 200, 0,   220);
static Color g_colFull    = Color(0,   255, 0,   255);

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────

static bool StrEq(const char* a, const char* b)
{
    if (!a || !b) return false;
    for (int i = 0; ; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i])        return true;
    }
}

static bool CdUp(const PluginCooldown& cd)
{
    return !cd.enabled || cd.current <= 0.f;
}

static void ColLoad(const char* pfx, Color& c, Color def)
{
    TextBuilder<16> k;
    float r, g, b, a;
    k.put(pfx).put("r"); r = Config::GetFloat(k.c_str(), (float)def.R()); k.clear();
    k.put(pfx).put("g"); g = Config::GetFloat(k.c_str(), (float)def.G()); k.clear();
    k.put(pfx).put("b"); b = Config::GetFloat(k.c_str(), (float)def.B()); k.clear();
    k.put(pfx).put("a"); a = Config::GetFloat(k.c_str(), (float)def.A()); k.clear();
    c = Color((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

static void ColSave(const char* pfx, const Color& c)
{
    TextBuilder<16> k;
    k.put(pfx).put("r"); Config::SetFloat(k.c_str(), (float)c.R()); k.clear();
    k.put(pfx).put("g"); Config::SetFloat(k.c_str(), (float)c.G()); k.clear();
    k.put(pfx).put("b"); Config::SetFloat(k.c_str(), (float)c.B()); k.clear();
    k.put(pfx).put("a"); Config::SetFloat(k.c_str(), (float)c.A()); k.clear();
}

static float DrawAbilLine(float y, Color col, const char* text)
{
    Draw::TextShadow(s_lineX, y, col, text);
    return y + s_lineStep;
}

static void CfgLoad(int i)
{
    TextBuilder<32> k;
    const char* p = kHeroKey[i];
    k.put(p).put("_on"); g_cfg[i].enabled = Config::GetBool(k.c_str(), true); k.clear();
    k.put(p).put("_s1"); g_cfg[i].s1      = Config::GetBool(k.c_str(), true); k.clear();
    k.put(p).put("_s2"); g_cfg[i].s2      = Config::GetBool(k.c_str(), true); k.clear();
    k.put(p).put("_s3"); g_cfg[i].s3      = Config::GetBool(k.c_str(), true); k.clear();
}

static void CfgSave(int i)
{
    TextBuilder<32> k;
    const char* p = kHeroKey[i];
    k.put(p).put("_on"); Config::SetBool(k.c_str(), g_cfg[i].enabled); k.clear();
    k.put(p).put("_s1"); Config::SetBool(k.c_str(), g_cfg[i].s1);      k.clear();
    k.put(p).put("_s2"); Config::SetBool(k.c_str(), g_cfg[i].s2);      k.clear();
    k.put(p).put("_s3"); Config::SetBool(k.c_str(), g_cfg[i].s3);      k.clear();
}

static int GetHeroIdx(uint64_t heroId)
{
    const char* name = GetHeroName(heroId);
    for (int i = 0; i < H_COUNT; i++)
        if (StrEq(name, kHeroName[i])) return i;
    return -1;
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

extern "C" void on_load()
{
    g_enabled     = Config::GetBool("enabled",     true);
    g_enemiesOnly = Config::GetBool("enemiesOnly", true);
    g_maxDist     = Config::GetFloat("maxDist",    40.f);
    g_renderMode  = (int)Config::GetFloat("renderMode", 0.f);
    g_padX        = Config::GetFloat("padX",       -40.f);
    g_padY        = Config::GetFloat("padY",       -14.f);
    g_hudX        = Config::GetFloat("hudX",       10.f);
    g_hudY        = Config::GetFloat("hudY",       10.f);
    ColLoad("cr", g_colReady,   Color(255, 0,   0,   255));
    ColLoad("cc", g_colCd,      Color(160, 160, 160, 200));
    ColLoad("ca", g_colActive,  Color(0,   255, 255, 255));
    ColLoad("cp", g_colPartial, Color(255, 200, 0,   220));
    ColLoad("cf", g_colFull,    Color(0,   255, 0,   255));
    for (int i = 0; i < H_COUNT; i++) CfgLoad(i);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",     g_enabled);
    Config::SetBool("enemiesOnly", g_enemiesOnly);
    Config::SetFloat("maxDist",    g_maxDist);
    Config::SetFloat("renderMode", (float)g_renderMode);
    Config::SetFloat("padX",       g_padX);
    Config::SetFloat("padY",       g_padY);
    Config::SetFloat("hudX",       g_hudX);
    Config::SetFloat("hudY",       g_hudY);
    ColSave("cr", g_colReady);
    ColSave("cc", g_colCd);
    ColSave("ca", g_colActive);
    ColSave("cp", g_colPartial);
    ColSave("cf", g_colFull);
    for (int i = 0; i < H_COUNT; i++) CfgSave(i);
    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled",         &g_enabled);
    ImGui::Checkbox("Enemies Only",    &g_enemiesOnly);
    ImGui::SliderFloat("Max Dist (m)", &g_maxDist, 5.f, 80.f);
    ImGui::Combo("Mode",               &g_renderMode, kRenderModes);
    if (g_renderMode == RM_HUD_2D) {
        ImGui::SliderFloat("HUD X",    &g_hudX, 0.f, 3840.f);
        ImGui::SliderFloat("HUD Y",    &g_hudY, 0.f, 2160.f);
    } else {
        ImGui::SliderFloat("Pad X",    &g_padX, -300.f, 300.f);
        ImGui::SliderFloat("Pad Y",    &g_padY, -300.f, 300.f);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Colors")) {
        ImGui::ColorSliders("Ready",    &g_colReady);
        ImGui::ColorSliders("Cooldown", &g_colCd);
        ImGui::ColorSliders("Active",   &g_colActive);
        ImGui::ColorSliders("Partial",  &g_colPartial);
        ImGui::ColorSliders("Full",     &g_colFull);
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Roadhog")) {
        ImGui::Checkbox("Enabled##hog", &g_cfg[H_ROADHOG].enabled);
        if (g_cfg[H_ROADHOG].enabled) {
            ImGui::Checkbox("Chain Hook (E)##hog", &g_cfg[H_ROADHOG].s2);
        }
    }

    if (ImGui::CollapsingHeader("Mei")) {
        ImGui::Checkbox("Enabled##mei", &g_cfg[H_MEI].enabled);
        if (g_cfg[H_MEI].enabled) {
            ImGui::Checkbox("Cryo-Freeze (Shift)##mei", &g_cfg[H_MEI].s1);
        }
    }

    if (ImGui::CollapsingHeader("Sigma")) {
        ImGui::Checkbox("Enabled##sig", &g_cfg[H_SIGMA].enabled);
        if (g_cfg[H_SIGMA].enabled) {
            ImGui::Checkbox("Kinetic Grasp (E)##sig", &g_cfg[H_SIGMA].s2);
            ImGui::Checkbox("Accretion / Rock##sig",  &g_cfg[H_SIGMA].s3);
        }
    }

    if (ImGui::CollapsingHeader("D.Va")) {
        ImGui::Checkbox("Enabled##dva", &g_cfg[H_DVA].enabled);
        if (g_cfg[H_DVA].enabled) {
            ImGui::Checkbox("Defense Matrix (E)##dva", &g_cfg[H_DVA].s2);
        }
    }

    if (ImGui::CollapsingHeader("Zarya")) {
        ImGui::Checkbox("Enabled##zar", &g_cfg[H_ZARYA].enabled);
        if (g_cfg[H_ZARYA].enabled) {
            ImGui::Checkbox("Particle Barrier (Shift)##zar", &g_cfg[H_ZARYA].s1);
            ImGui::Checkbox("Projected Barrier (E)##zar",    &g_cfg[H_ZARYA].s2);
        }
    }

    if (ImGui::CollapsingHeader("Ana")) {
        ImGui::Checkbox("Enabled##ana", &g_cfg[H_ANA].enabled);
        if (g_cfg[H_ANA].enabled) {
            ImGui::Checkbox("Sleep Dart (Shift)##ana",  &g_cfg[H_ANA].s1);
            ImGui::Checkbox("Biotic Grenade (E)##ana",  &g_cfg[H_ANA].s2);
        }
    }

    if (ImGui::CollapsingHeader("Baptiste")) {
        ImGui::Checkbox("Enabled##bap", &g_cfg[H_BAPTISTE].enabled);
        if (g_cfg[H_BAPTISTE].enabled) {
            ImGui::Checkbox("Immortality Field (E)##bap", &g_cfg[H_BAPTISTE].s2);
        }
    }

    if (ImGui::CollapsingHeader("Sombra")) {
        ImGui::Checkbox("Enabled##som", &g_cfg[H_SOMBRA].enabled);
        if (g_cfg[H_SOMBRA].enabled) {
            ImGui::Checkbox("Hack (E)##som", &g_cfg[H_SOMBRA].s2);
        }
    }

    if (ImGui::CollapsingHeader("Tracer")) {
        ImGui::Checkbox("Enabled##tra", &g_cfg[H_TRACER].enabled);
        if (g_cfg[H_TRACER].enabled) {
            ImGui::Checkbox("Blink (Shift)##tra", &g_cfg[H_TRACER].s1);
            ImGui::Checkbox("Recall (E)##tra",    &g_cfg[H_TRACER].s2);
        }
    }

    if (ImGui::CollapsingHeader("Cassidy")) {
        ImGui::Checkbox("Enabled##cas", &g_cfg[H_CASSIDY].enabled);
        if (g_cfg[H_CASSIDY].enabled) {
            ImGui::Checkbox("Magnetic Grenade (E)##cas", &g_cfg[H_CASSIDY].s2);
        }
    }

    if (ImGui::CollapsingHeader("Venture")) {
        ImGui::Checkbox("Enabled##ven", &g_cfg[H_VENTURE].enabled);
        if (g_cfg[H_VENTURE].enabled) {
            ImGui::Checkbox("Burrow (Shift)##ven", &g_cfg[H_VENTURE].s1);
        }
    }

    if (ImGui::CollapsingHeader("Lifeweaver")) {
        ImGui::Checkbox("Enabled##lw", &g_cfg[H_LIFEWEAVER].enabled);
        if (g_cfg[H_LIFEWEAVER].enabled) {
            ImGui::Checkbox("Life Grip (E)##lw", &g_cfg[H_LIFEWEAVER].s2);
        }
    }

    if (ImGui::CollapsingHeader("Kiriko")) {
        ImGui::Checkbox("Enabled##kir", &g_cfg[H_KIRIKO].enabled);
        if (g_cfg[H_KIRIKO].enabled) {
            ImGui::Checkbox("Protection Suzu (E)##kir", &g_cfg[H_KIRIKO].s2);
        }
    }

    if (ImGui::CollapsingHeader("Bastion")) {
        ImGui::Checkbox("Enabled##bas", &g_cfg[H_BASTION].enabled);
        if (g_cfg[H_BASTION].enabled) {
            ImGui::Checkbox("Configuration: Sentry (Shift)##bas", &g_cfg[H_BASTION].s1);
        }
    }

    if (ImGui::CollapsingHeader("Hazard")) {
        ImGui::Checkbox("Enabled##haz", &g_cfg[H_HAZARD].enabled);
        if (g_cfg[H_HAZARD].enabled) {
            ImGui::Checkbox("Leap (Shift)##haz", &g_cfg[H_HAZARD].s1);
            ImGui::Checkbox("Block (E)##haz",    &g_cfg[H_HAZARD].s2);
        }
    }

    if (ImGui::CollapsingHeader("Doomfist")) {
        ImGui::Checkbox("Enabled##doom", &g_cfg[H_DOOMFIST].enabled);
        if (g_cfg[H_DOOMFIST].enabled) {
            ImGui::Checkbox("Power Block (Shift)##doom", &g_cfg[H_DOOMFIST].s1);
            ImGui::Checkbox("Seismic Slam (E)##doom",   &g_cfg[H_DOOMFIST].s2);
            ImGui::Checkbox("Rocket Punch##doom",        &g_cfg[H_DOOMFIST].s3);
        }
    }

    if (ImGui::CollapsingHeader("Winston")) {
        ImGui::Checkbox("Enabled##win", &g_cfg[H_WINSTON].enabled);
        if (g_cfg[H_WINSTON].enabled) {
            ImGui::Checkbox("Jump Pack (Shift)##win",       &g_cfg[H_WINSTON].s1);
            ImGui::Checkbox("Barrier Projector (E)##win",   &g_cfg[H_WINSTON].s2);
        }
    }

    if (ImGui::CollapsingHeader("Wrecking Ball")) {
        ImGui::Checkbox("Enabled##ball", &g_cfg[H_WBALL].enabled);
        if (g_cfg[H_WBALL].enabled) {
            ImGui::Checkbox("Grappling Claw (Shift)##ball", &g_cfg[H_WBALL].s1);
        }
    }

    if (ImGui::CollapsingHeader("Reinhardt")) {
        ImGui::Checkbox("Enabled##rein", &g_cfg[H_REINHARDT].enabled);
        if (g_cfg[H_REINHARDT].enabled) {
            ImGui::Checkbox("Charge (Shift)##rein",  &g_cfg[H_REINHARDT].s1);
            ImGui::Checkbox("Fire Strike (E)##rein", &g_cfg[H_REINHARDT].s2);
        }
    }

    if (ImGui::CollapsingHeader("Reaper")) {
        ImGui::Checkbox("Enabled##rea", &g_cfg[H_REAPER].enabled);
        if (g_cfg[H_REAPER].enabled) {
            ImGui::Checkbox("Wraith Form (Shift)##rea", &g_cfg[H_REAPER].s1);
            ImGui::Checkbox("Shadow Step (E)##rea",     &g_cfg[H_REAPER].s2);
        }
    }

    if (ImGui::CollapsingHeader("Orisa")) {
        ImGui::Checkbox("Enabled##ori", &g_cfg[H_ORISA].enabled);
        if (g_cfg[H_ORISA].enabled) {
            ImGui::Checkbox("Fortify (Shift)##ori",  &g_cfg[H_ORISA].s1);
            ImGui::Checkbox("Javelin Spin (E)##ori", &g_cfg[H_ORISA].s2);
            ImGui::Checkbox("Energy Javelin##ori",   &g_cfg[H_ORISA].s3);
        }
    }

    if (ImGui::CollapsingHeader("Genji")) {
        ImGui::Checkbox("Enabled##gen", &g_cfg[H_GENJI].enabled);
        if (g_cfg[H_GENJI].enabled) {
            ImGui::Checkbox("Swift Strike (Shift)##gen", &g_cfg[H_GENJI].s1);
            ImGui::Checkbox("Deflect (E)##gen",          &g_cfg[H_GENJI].s2);
        }
    }

    if (ImGui::CollapsingHeader("Vendetta")) {  // verify name via debug_hud
        ImGui::Checkbox("Enabled##vend", &g_cfg[H_VENDETTA].enabled);
        if (g_cfg[H_VENDETTA].enabled) {
            ImGui::Checkbox("Whirlwind Dash (Shift)##vend", &g_cfg[H_VENDETTA].s1);
            ImGui::Checkbox("Soaring Slice (E)##vend",      &g_cfg[H_VENDETTA].s2);
        }
    }

    if (ImGui::CollapsingHeader("Hanzo")) {
        ImGui::Checkbox("Enabled##han", &g_cfg[H_HANZO].enabled);
        if (g_cfg[H_HANZO].enabled) {
            ImGui::Checkbox("Sonic Arrow (Shift)##han", &g_cfg[H_HANZO].s1);
            ImGui::Checkbox("Storm Arrows (E)##han",    &g_cfg[H_HANZO].s2);
        }
    }

    if (ImGui::CollapsingHeader("Echo")) {
        ImGui::Checkbox("Enabled##echo", &g_cfg[H_ECHO].enabled);
        if (g_cfg[H_ECHO].enabled) {
            ImGui::Checkbox("Flight (Shift)##echo",   &g_cfg[H_ECHO].s1);
            ImGui::Checkbox("Sticky Bombs (E)##echo", &g_cfg[H_ECHO].s2);
        }
    }

    if (ImGui::CollapsingHeader("Mizuki")) {  // verify name via debug_hud
        ImGui::Checkbox("Enabled##miz", &g_cfg[H_MIZUKI].enabled);
        if (g_cfg[H_MIZUKI].enabled) {
            ImGui::Checkbox("Chain (E)##miz", &g_cfg[H_MIZUKI].s2);
        }
    }

    if (ImGui::CollapsingHeader("Brigitte")) {
        ImGui::Checkbox("Enabled##brig", &g_cfg[H_BRIGITTE].enabled);
        if (g_cfg[H_BRIGITTE].enabled) {
            ImGui::Checkbox("Shield Bash (Shift)##brig", &g_cfg[H_BRIGITTE].s1);
            ImGui::Checkbox("Whip Shot (E)##brig",       &g_cfg[H_BRIGITTE].s2);
        }
    }

    if (ImGui::CollapsingHeader("Sojourn")) {
        ImGui::Checkbox("Enabled##soj", &g_cfg[H_SOJOURN].enabled);
        if (g_cfg[H_SOJOURN].enabled) {
            ImGui::Checkbox("Slide (Shift)##soj", &g_cfg[H_SOJOURN].s1);
        }
    }

    if (ImGui::CollapsingHeader("Ashe")) {
        ImGui::Checkbox("Enabled##ash", &g_cfg[H_ASHE].enabled);
        if (g_cfg[H_ASHE].enabled) {
            ImGui::Checkbox("Dynamite (Shift)##ash",  &g_cfg[H_ASHE].s1);
            ImGui::Checkbox("Coach Gun (E)##ash",     &g_cfg[H_ASHE].s2);
        }
    }

    if (ImGui::CollapsingHeader("Pharah")) {
        ImGui::Checkbox("Enabled##pha", &g_cfg[H_PHARAH].enabled);
        if (g_cfg[H_PHARAH].enabled) {
            ImGui::Checkbox("Jump Jet (Shift)##pha",       &g_cfg[H_PHARAH].s1);
            ImGui::Checkbox("Concussive Blast (E)##pha",   &g_cfg[H_PHARAH].s2);
        }
    }

    if (ImGui::CollapsingHeader("Wuyang")) {  // verify name via debug_hud
        ImGui::Checkbox("Enabled##wuy", &g_cfg[H_WUYANG].enabled);
        if (g_cfg[H_WUYANG].enabled) {
            ImGui::Checkbox("Staff Slam (E)##wuy", &g_cfg[H_WUYANG].s2);
        }
    }

    if (ImGui::CollapsingHeader("Mercy")) {
        ImGui::Checkbox("Enabled##mer", &g_cfg[H_MERCY].enabled);
        if (g_cfg[H_MERCY].enabled) {
            ImGui::Checkbox("Resurrect (E)##mer", &g_cfg[H_MERCY].s2);
        }
    }

    if (ImGui::CollapsingHeader("Illari")) {
        ImGui::Checkbox("Enabled##ill", &g_cfg[H_ILLARI].enabled);
        if (g_cfg[H_ILLARI].enabled) {
            ImGui::Checkbox("Outburst (Shift)##ill", &g_cfg[H_ILLARI].s1);
        }
    }
}

// ─────────────────────────────────────────────
//  on_frame — entity snapshot
// ─────────────────────────────────────────────

extern "C" void on_frame(float)
{
    int count = GetPlayerCount();
    g_entityCount = count < 32 ? count : 32;
    for (int i = 0; i < g_entityCount; i++)
    {
        xn_get_entity(i, &g_entities[i]);
        if (g_entities[i].isLocalPlayer)
        {
            g_localPos  = g_entities[i].position;
            g_localTeam = (int32_t)g_entities[i].team;
        }
    }
}

// ─────────────────────────────────────────────
//  on_render — draw ability indicators
// ─────────────────────────────────────────────

extern "C" void on_render()
{
    if (!g_enabled) return;

    Color kReady   = g_colReady;
    Color kCd      = g_colCd;
    Color kActive  = g_colActive;
    Color kYellow  = g_colPartial;
    Color kGreen   = g_colFull;

    bool isHud = (g_renderMode == RM_HUD_2D);

    // HUD 2D: compute fixed panel anchor once per frame
    float hudCurY = g_hudY;
    if (isHud) {
        s_lineX    = g_hudX;
        s_lineStep = 14.f;
    }

    for (int i = 0; i < g_entityCount; i++)
    {
        const PluginEntity& e = g_entities[i];
        if (!e.alive || e.isLocalPlayer) continue;
        if (g_enemiesOnly && (int32_t)e.team == g_localTeam) continue;

        float dx = e.position.x - g_localPos.x;
        float dy = e.position.y - g_localPos.y;
        float dz = e.position.z - g_localPos.z;
        if (dx*dx + dy*dy + dz*dz > g_maxDist * g_maxDist) continue;

        int  hi          = GetHeroIdx(e.heroId);
        bool heroTracked = (hi >= 0) && g_cfg[hi].enabled;
        if (!heroTracked) continue;

        float y;
        if (isHud) {
            y = hudCurY;
        } else {
            Vector3 headWorld = { e.position.x, e.position.y, e.position.z + e.delta2.z };
            Vector2 head;
            if (!WorldToScreen(headWorld, head)) continue;
            s_lineX    = head.x + g_padX;
            s_lineStep = -14.f;
            y          = head.y + g_padY;
        }

        TextBuilder<56> tb;

        // HUD mode: draw hero name as header
        if (isHud) {
            const char* name = GetHeroName(e.heroId);
            Draw::TextShadow(s_lineX, y, Color::White(), name ? name : "?");
            y += s_lineStep;
        }

        // ── Hero-specific abilities ──────────────────────────────────────────
        switch (hi)
        {

        // ── Roadhog — Chain Hook (E) ──────────────────────────────────────────
        case H_ROADHOG:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "HOOK: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "HOOK: READY");
                } else {
                    tb.put("HOOK: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Mei — Cryo-Freeze (Shift) ─────────────────────────────────────────
        case H_MEI:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    if (e.skill1Duration.current > 0.f)
                        tb.put("FREEZE: ").putFloat(e.skill1Duration.current, 1).put("s");
                    else
                        tb.put("FREEZE: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "FREEZE: READY");
                } else {
                    tb.put("FREEZE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Sigma — Kinetic Grasp (E) + Accretion Rock (skill3) ───────────────
        case H_SIGMA:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "GRASP: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "GRASP: READY");
                } else {
                    tb.put("GRASP: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s3)
            {
                if (CdUp(e.skill3Cd)) {
                    y = DrawAbilLine(y, kReady, "ROCK: READY");
                } else {
                    tb.put("ROCK: ").putFloat(e.skill3Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── D.Va — Defense Matrix (E) ─────────────────────────────────────────
        case H_DVA:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "DM: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "DM: READY");
                } else {
                    tb.put("DM: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Zarya — Particle Barrier (Shift) + Projected Barrier (E) ──────────
        case H_ZARYA:
            if (g_cfg[hi].s1 || g_cfg[hi].s2)
            {
                bool s1Up  = g_cfg[hi].s1 && CdUp(e.skill1Cd);
                bool s2Up  = g_cfg[hi].s2 && CdUp(e.skill2Cd);
                int  avail = (s1Up ? 1 : 0) + (s2Up ? 1 : 0);

                float cd1    = (g_cfg[hi].s1 && !s1Up) ? e.skill1Cd.current : 0.f;
                float cd2    = (g_cfg[hi].s2 && !s2Up) ? e.skill2Cd.current : 0.f;
                float nextCd = 0.f;
                if      (cd1 > 0.f && cd2 > 0.f) nextCd = cd1 < cd2 ? cd1 : cd2;
                else if (cd1 > 0.f)               nextCd = cd1;
                else if (cd2 > 0.f)               nextCd = cd2;

                Color col = (avail == 0) ? kCd
                          : (avail == 1) ? kYellow
                                         : kGreen;

                tb.put("BUBBLES: ").putInt(avail);
                if (nextCd > 0.f) tb.put("  ").putFloat(nextCd, 1).put("s");
                y = DrawAbilLine(y, col, tb.c_str()); tb.clear();
            }
            break;

        // ── Ana — Sleep Dart (Shift) + Biotic Grenade (E) ────────────────────
        case H_ANA:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "SLEEP: READY");
                } else {
                    tb.put("SLEEP: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "NADE: READY");
                } else {
                    tb.put("NADE: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Baptiste — Immortality Field (E) ──────────────────────────────────
        case H_BAPTISTE:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    if (e.skill2Duration.current > 0.f)
                        tb.put("IMMO: ").putFloat(e.skill2Duration.current, 1).put("s");
                    else
                        tb.put("IMMO: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "IMMO: READY");
                } else {
                    tb.put("IMMO: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Sombra — Hack (E) ─────────────────────────────────────────────────
        case H_SOMBRA:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "HACK: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "HACK: READY");
                } else {
                    tb.put("HACK: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Tracer — Blink (Shift, 3 charges) + Recall (E) ──────────────────────
        case H_TRACER:
            if (g_cfg[hi].s1)
            {
                // skill1Active.x = current charge count (0–3 as float)
                int blinks = (int)(e.skill1Active.x + 0.5f);
                if (blinks < 0) blinks = 0;
                if (blinks > 3) blinks = 3;
                Color blinkCol = (blinks == 3) ? kReady : (blinks > 0) ? kYellow : kCd;
                tb.put("BLINKS: ").putInt(blinks);
                if (blinks < 3 && e.skill1Cd.enabled && e.skill1Cd.current > 0.f)
                    tb.put(" +").putFloat(e.skill1Cd.current, 1).put("s");
                y = DrawAbilLine(y, blinkCol, tb.c_str()); tb.clear();
            }
            if (g_cfg[hi].s2)
            {
                // Use skill2Active (not skill2Duration) — duration stays set after recall ends
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "RECALL: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "RECALL: READY");
                } else {
                    tb.put("RECALL: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Cassidy — Magnetic Grenade (E) ────────────────────────────────────
        case H_CASSIDY:
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "NADE: READY");
                } else {
                    tb.put("NADE: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Venture — Burrow (Shift) ───────────────────────────────────────────
        case H_VENTURE:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    if (e.skill1Duration.current > 0.f)
                        tb.put("BURROW: ").putFloat(e.skill1Duration.current, 1).put("s");
                    else
                        tb.put("BURROW: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "BURROW: READY");
                } else {
                    tb.put("BURROW: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Lifeweaver — Life Grip (E) ─────────────────────────────────────────
        case H_LIFEWEAVER:
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "GRIP: READY");
                } else {
                    tb.put("GRIP: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Kiriko — Protection Suzu (E) ──────────────────────────────────────
        case H_KIRIKO:
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SUZU: READY");
                } else {
                    tb.put("SUZU: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Bastion — Configuration: Sentry (Shift) ────────────────────────────
        case H_BASTION:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "TURRET: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "TURRET: READY");
                } else {
                    tb.put("TURRET: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Hazard — Leap (Shift) + Block (E) ─────────────────────────────────
        case H_HAZARD:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "LEAP: READY");
                } else {
                    tb.put("LEAP: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                bool blocking = e.skill2Active.x > 0.5f;
                if (blocking) {
                    y = DrawAbilLine(y, kActive, "BLOCK: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "BLOCK: READY");
                } else {
                    tb.put("BLOCK: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Doomfist — Power Block (Shift) + Seismic Slam (E) + Punch (skill3) ─
        case H_DOOMFIST:
            if (g_cfg[hi].s1)
            {
                bool blocking = e.skill1Active.x > 0.5f;
                if (blocking) {
                    y = DrawAbilLine(y, kActive, "BLOCK: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "BLOCK: READY");
                } else {
                    tb.put("BLOCK: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SLAM: READY");
                } else {
                    tb.put("SLAM: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s3)
            {
                if (CdUp(e.skill3Cd)) {
                    y = DrawAbilLine(y, kReady, "PUNCH: READY");
                } else {
                    tb.put("PUNCH: ").putFloat(e.skill3Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Winston — Jump Pack (Shift) + Barrier Projector (E) ───────────────
        case H_WINSTON:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "JUMP: READY");
                } else {
                    tb.put("JUMP: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    if (e.skill2Duration.current > 0.f)
                        tb.put("BARRIER: ").putFloat(e.skill2Duration.current, 1).put("s");
                    else
                        tb.put("BARRIER: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "BARRIER: READY");
                } else {
                    tb.put("BARRIER: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Wrecking Ball — Grappling Claw (Shift) ────────────────────────────
        case H_WBALL:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "GRAPPLE: READY");
                } else {
                    tb.put("GRAPPLE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Reinhardt — Charge (Shift) + Fire Strike (E) ──────────────────────
        case H_REINHARDT:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "CHARGE: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "CHARGE: READY");
                } else {
                    tb.put("CHARGE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "FIRE: READY");
                } else {
                    tb.put("FIRE: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Reaper — Wraith Form (Shift) + Shadow Step (E) ────────────────────
        case H_REAPER:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "WRAITH: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "WRAITH: READY");
                } else {
                    tb.put("WRAITH: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SHADOW: READY");
                } else {
                    tb.put("SHADOW: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Orisa — Fortify (Shift) + Javelin Spin (E) + Energy Javelin (skill3)
        case H_ORISA:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    if (e.skill1Duration.current > 0.f)
                        tb.put("FORTIFY: ").putFloat(e.skill1Duration.current, 1).put("s");
                    else
                        tb.put("FORTIFY: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "FORTIFY: READY");
                } else {
                    tb.put("FORTIFY: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "SPIN: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SPIN: READY");
                } else {
                    tb.put("SPIN: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s3)
            {
                if (CdUp(e.skill3Cd)) {
                    y = DrawAbilLine(y, kReady, "JAVELIN: READY");
                } else {
                    tb.put("JAVELIN: ").putFloat(e.skill3Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Genji — Swift Strike (Shift) + Deflect (E) ────────────────────────
        case H_GENJI:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "STRIKE: READY");
                } else {
                    tb.put("STRIKE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "DEFLECT: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "DEFLECT: READY");
                } else {
                    tb.put("DEFLECT: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Vendetta — Whirlwind Dash (Shift) + Soaring Slice (E) ───────────
        case H_VENDETTA:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "DASH: READY");
                } else {
                    tb.put("DASH: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SLICE: READY");
                } else {
                    tb.put("SLICE: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Hanzo — Sonic Arrow (Shift) + Storm Arrows (E) ────────────────────
        case H_HANZO:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "SONIC: READY");
                } else {
                    tb.put("SONIC: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    if (e.skill2Duration.current > 0.f)
                        tb.put("STORM: ").putFloat(e.skill2Duration.current, 1).put("s");
                    else
                        tb.put("STORM: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "STORM: READY");
                } else {
                    tb.put("STORM: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Echo — Flight (Shift) + Sticky Bombs (E) ──────────────────────────
        case H_ECHO:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    if (e.skill1Duration.current > 0.f)
                        tb.put("FLIGHT: ").putFloat(e.skill1Duration.current, 1).put("s");
                    else
                        tb.put("FLIGHT: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "FLIGHT: READY");
                } else {
                    tb.put("FLIGHT: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "STICKY: READY");
                } else {
                    tb.put("STICKY: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Mizuki — Chain (E) ────────────────────────────────────────────────
        case H_MIZUKI:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    if (e.skill2Duration.current > 0.f)
                        tb.put("CHAIN: ").putFloat(e.skill2Duration.current, 1).put("s");
                    else
                        tb.put("CHAIN: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "CHAIN: READY");
                } else {
                    tb.put("CHAIN: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Brigitte — Shield Bash (Shift) + Whip Shot (E) ────────────────────
        case H_BRIGITTE:
            if (g_cfg[hi].s1)
            {
                if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "BASH: READY");
                } else {
                    tb.put("BASH: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "WHIP: READY");
                } else {
                    tb.put("WHIP: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Sojourn — Slide (Shift) ───────────────────────────────────────────
        case H_SOJOURN:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "SLIDE: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "SLIDE: READY");
                } else {
                    tb.put("SLIDE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Ashe — Dynamite (Shift) + Coach Gun (E) ───────────────────────────
        case H_ASHE:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    if (e.skill1Duration.current > 0.f)
                        tb.put("DYNAMITE: ").putFloat(e.skill1Duration.current, 1).put("s");
                    else
                        tb.put("DYNAMITE: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "DYNAMITE: READY");
                } else {
                    tb.put("DYNAMITE: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "COACH GUN: READY");
                } else {
                    tb.put("COACH GUN: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Pharah — Jump Jet (Shift) + Concussive Blast (E) ─────────────────
        case H_PHARAH:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "JUMP JET: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "JUMP JET: READY");
                } else {
                    tb.put("JUMP JET: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            if (g_cfg[hi].s2)
            {
                if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "BLAST: READY");
                } else {
                    tb.put("BLAST: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Wuyang — Staff Slam (E) ───────────────────────────────────────────
        case H_WUYANG:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "SLAM: ACTIVE");
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "SLAM: READY");
                } else {
                    tb.put("SLAM: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Mercy — Resurrect (E) ─────────────────────────────────────────────
        case H_MERCY:
            if (g_cfg[hi].s2)
            {
                if (e.skill2Active.x > 0.5f) {
                    if (e.skill2Duration.current > 0.f)
                        tb.put("REZ: ").putFloat(e.skill2Duration.current, 1).put("s");
                    else
                        tb.put("REZ: ACTIVE");
                    y = DrawAbilLine(y, kActive, tb.c_str()); tb.clear();
                } else if (CdUp(e.skill2Cd)) {
                    y = DrawAbilLine(y, kReady, "REZ: READY");
                } else {
                    tb.put("REZ: ").putFloat(e.skill2Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        // ── Illari — Outburst (Shift) ─────────────────────────────────────────
        case H_ILLARI:
            if (g_cfg[hi].s1)
            {
                if (e.skill1Active.x > 0.5f) {
                    y = DrawAbilLine(y, kActive, "OUTBURST: ACTIVE");
                } else if (CdUp(e.skill1Cd)) {
                    y = DrawAbilLine(y, kReady, "OUTBURST: READY");
                } else {
                    tb.put("OUTBURST: ").putFloat(e.skill1Cd.current, 1).put("s");
                    y = DrawAbilLine(y, kCd, tb.c_str()); tb.clear();
                }
            }
            break;

        } // switch

        if (isHud)
            hudCurY = y + s_lineStep;  // gap between hero blocks

        (void)y;
    }
}
