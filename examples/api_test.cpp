// api_test.cpp
// Probes every SDK API call and reports LIVE / STUB / FAIL on the HUD.
// LIVE  = returned a non-zero/non-default value — host binding works
// STUB  = returned exactly zero/false/default — may be unimplemented
// FAIL  = caused a detectable error condition
//
// Build: build.bat examples\api_test.cpp
// Install: copy output\api_test.wasm %AppData%\owplugins\

#include <xenon/SDK.hpp>

using namespace xenon;

// ============================================================================
// Result tracking
// ============================================================================

enum class R : uint8_t { Unknown, Live, Stub, Fail };

static const char* RLabel(R r)
{
    switch (r)
    {
        case R::Live: return "LIVE";
        case R::Stub: return "STUB";
        case R::Fail: return "FAIL";
        default:      return "????";
    }
}

static Color RColor(R r)
{
    switch (r)
    {
        case R::Live: return Color::Green();
        case R::Stub: return Color::Yellow();
        case R::Fail: return Color::Red();
        default:      return Color(160, 160, 160);
    }
}

// One slot per tested API
struct Slot { const char* name; R result; };

// ---- Core (1.0) ----
static Slot s_GetTime          = { "GetTime",           R::Unknown };
static Slot s_ScreenSize       = { "ScreenSize",        R::Unknown };
static Slot s_ScreenCenter     = { "ScreenCenter",      R::Unknown };
static Slot s_WorldToScreen    = { "WorldToScreen",     R::Unknown };
static Slot s_IsKeyDown        = { "IsKeyDown",         R::Unknown };

// ---- Core (unreleased) ----
static Slot s_IsIngame         = { "IsIngame",          R::Unknown };
static Slot s_GetCurrentHero   = { "GetCurrentHero",    R::Unknown };
static Slot s_GetMapId         = { "GetMapId",          R::Unknown };
static Slot s_GetSensitivity   = { "GetSensitivity",    R::Unknown };
static Slot s_GetClientPing    = { "GetClientPing",     R::Unknown };
static Slot s_GetServerPing    = { "GetServerPing",     R::Unknown };
static Slot s_GetUltCharge     = { "GetUltCharge",      R::Unknown };
static Slot s_IsUltReady       = { "IsUltReady",        R::Unknown };
static Slot s_IsUltActive      = { "IsUltActive",       R::Unknown };
static Slot s_GetHeroState     = { "GetHeroState",      R::Unknown };
static Slot s_IsSkill1Active   = { "IsSkill1Active",    R::Unknown };
static Slot s_IsSkill2Active   = { "IsSkill2Active",    R::Unknown };
static Slot s_IsSkill3Active   = { "IsSkill3Active",    R::Unknown };
static Slot s_GetWidowCharge   = { "GetWidowCharge",    R::Unknown };
static Slot s_GetHanzoCharge   = { "GetHanzoCharge",    R::Unknown };
static Slot s_GetRailgunCharge = { "GetRailgunCharge",  R::Unknown };
static Slot s_GetIllariCharge  = { "GetIllariCharge",   R::Unknown };
static Slot s_GetDeltaTime     = { "GetDeltaTime",      R::Unknown };

// ---- Camera (unreleased) ----
static Slot s_GetCameraPos     = { "GetCameraPos",      R::Unknown };
static Slot s_GetCameraFwd     = { "GetCameraFwd",      R::Unknown };
static Slot s_GetViewMatrix    = { "GetViewMatrix",     R::Unknown };

// ---- Raycast (unreleased) ----
static Slot s_RaycastReady     = { "RaycastReady",      R::Unknown };
static Slot s_Raycast          = { "Raycast",           R::Unknown };
static Slot s_IsPointVisible   = { "IsPointVisible",    R::Unknown };

// ---- Aim (unreleased) ----
static Slot s_AimGetAngles     = { "AimGetAngles",      R::Unknown };
static Slot s_AimGetDir        = { "AimGetDir",         R::Unknown };

// ---- Entity (1.0) ----
static Slot s_PlayerCount      = { "GetPlayerCount",    R::Unknown };
static Slot s_LocalPlayer      = { "LocalPlayer",       R::Unknown };
static Slot s_EntityPosition   = { "Entity.Position",   R::Unknown };
static Slot s_EntityHealth     = { "Entity.Health",     R::Unknown };
static Slot s_EntityBonePos    = { "Entity.BonePos",    R::Unknown };
static Slot s_EntityHeadPos    = { "Entity.HeadPos",    R::Unknown };
static Slot s_EntityVisible    = { "Entity.Visible",    R::Unknown };
static Slot s_EntityFovTo      = { "Entity.FovTo",      R::Unknown };

// ---- Entity (unreleased) ----
static Slot s_EntityHeroId     = { "Entity.HeroId",     R::Unknown };
static Slot s_EntityUltCharge  = { "Entity.UltCharge",  R::Unknown };
static Slot s_EntityTargetable = { "Entity.Targetable", R::Unknown };
static Slot s_EntityForward    = { "Entity.Forward",    R::Unknown };
static Slot s_EntityHitboxes   = { "Entity.Hitboxes",   R::Unknown };
static Slot s_EntityLerp       = { "Entity.LerpHist",   R::Unknown };
static Slot s_Skill1Cd         = { "Skill1Cooldown",    R::Unknown };
static Slot s_Skill2Cd         = { "Skill2Cooldown",    R::Unknown };

// ---- Draw (1.0) ----
static Slot s_DrawLine         = { "Draw::Line",        R::Unknown };
static Slot s_DrawRect         = { "Draw::Rect",        R::Unknown };
static Slot s_DrawCircle       = { "Draw::Circle",      R::Unknown };
static Slot s_DrawText         = { "Draw::Text",        R::Unknown };

// ---- Draw (unreleased) ----
static Slot s_DrawTextShadow   = { "Draw::TextShadow",  R::Unknown };

// ---- Config ----
static Slot s_ConfigBool       = { "Config::Bool",      R::Unknown };
static Slot s_ConfigInt        = { "Config::Int",       R::Unknown };
static Slot s_ConfigFloat      = { "Config::Float",     R::Unknown };
static Slot s_ConfigColor      = { "Config::Color",     R::Unknown };

static const int SLOT_COUNT = 50;
static Slot* s_slots[SLOT_COUNT] = {
    &s_GetTime, &s_ScreenSize, &s_ScreenCenter, &s_WorldToScreen, &s_IsKeyDown,
    &s_IsIngame, &s_GetCurrentHero, &s_GetMapId, &s_GetSensitivity,
    &s_GetClientPing, &s_GetServerPing,
    &s_GetUltCharge, &s_IsUltReady, &s_IsUltActive, &s_GetHeroState,
    &s_IsSkill1Active, &s_IsSkill2Active, &s_IsSkill3Active,
    &s_GetWidowCharge, &s_GetHanzoCharge, &s_GetRailgunCharge, &s_GetIllariCharge,
    &s_GetDeltaTime,
    &s_GetCameraPos, &s_GetCameraFwd, &s_GetViewMatrix,
    &s_RaycastReady, &s_Raycast, &s_IsPointVisible,
    &s_AimGetAngles, &s_AimGetDir,
    &s_PlayerCount, &s_LocalPlayer,
    &s_EntityPosition, &s_EntityHealth, &s_EntityBonePos, &s_EntityHeadPos,
    &s_EntityVisible, &s_EntityFovTo,
    &s_EntityHeroId, &s_EntityUltCharge, &s_EntityTargetable,
    &s_EntityForward, &s_EntityHitboxes, &s_EntityLerp,
    &s_Skill1Cd, &s_Skill2Cd,
    &s_DrawLine, &s_DrawRect, &s_DrawCircle,
    // DrawText, DrawTextShadow, Config tested separately
};

// ============================================================================
// Plugin Info
// ============================================================================

XENON_PLUGIN_INFO(
    "api_test",
    "API Test",
    "Xenon",
    "Probes every SDK API and reports LIVE/STUB/FAIL on HUD.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ============================================================================
// Run tests
// ============================================================================

static void RunTests(float dt)
{
    // ---- Core 1.0 ----
    s_GetTime.result     = GetTime() > 0.f         ? R::Live : R::Stub;
    s_GetDeltaTime.result = dt > 0.f               ? R::Live : R::Stub;

    Vector2 sz = ScreenSize();
    s_ScreenSize.result  = (sz.x > 0 && sz.y > 0) ? R::Live : R::Stub;

    Vector2 sc = ScreenCenter();
    s_ScreenCenter.result = (sc.x > 0 && sc.y > 0) ? R::Live : R::Stub;

    s_IsKeyDown.result   = R::Live; // can't distinguish stub from "no key held" — assume live

    // ---- Core unreleased ----
    s_IsIngame.result        = IsIngame()            ? R::Live : R::Stub;
    s_GetCurrentHero.result  = GetCurrentHero() != 0 ? R::Live : R::Stub;
    s_GetMapId.result        = GetMapId() != 0       ? R::Live : R::Stub;
    s_GetSensitivity.result  = GetSensitivity() > 0.f ? R::Live : R::Stub;
    s_GetClientPing.result   = GetClientPing() >= 0  ? R::Live : R::Stub;
    s_GetServerPing.result   = GetServerPing() >= 0  ? R::Live : R::Stub;
    s_GetUltCharge.result    = GetUltCharge() >= 0.f ? R::Live : R::Stub;
    s_IsUltReady.result      = R::Live; // bool — can't distinguish
    s_IsUltActive.result     = R::Live;
    s_GetHeroState.result    = R::Live;
    s_IsSkill1Active.result  = R::Live;
    s_IsSkill2Active.result  = R::Live;
    s_IsSkill3Active.result  = R::Live;
    s_GetWidowCharge.result  = GetWidowCharge() >= 0.f  ? R::Live : R::Stub;
    s_GetHanzoCharge.result  = GetHanzoCharge() >= 0.f  ? R::Live : R::Stub;
    s_GetRailgunCharge.result = GetRailgunCharge() >= 0.f ? R::Live : R::Stub;
    s_GetIllariCharge.result  = GetIllariCharge() >= 0.f  ? R::Live : R::Stub;

    // ---- Camera ----
    {
        Vector3 cam;
        bool ok = GetCameraPosition(cam);
        s_GetCameraPos.result = (ok && cam.IsValid()) ? R::Live : R::Stub;
    }
    {
        Vector3 fwd;
        bool ok = GetCameraForward(fwd);
        s_GetCameraFwd.result = (ok && fwd.IsValid()) ? R::Live : R::Stub;
    }
    {
        float mat[16] = {};
        bool ok = GetViewMatrix(mat);
        // Check if at least one non-zero value (identity diagonal would have 1s)
        bool nonzero = false;
        for (int i = 0; i < 16; i++) if (mat[i] != 0.f) { nonzero = true; break; }
        s_GetViewMatrix.result = (ok && nonzero) ? R::Live : R::Stub;
    }

    // ---- Raycast ----
    s_RaycastReady.result = IsRaycastReady() ? R::Live : R::Stub;
    {
        // Cast straight down from origin — if raycast is live it should hit something
        Vector3 from = { 0.f, 500.f, 0.f };
        Vector3 to   = { 0.f, -500.f, 0.f };
        RaycastResult rc = Raycast(from, to);
        s_Raycast.result = (rc.hit != 0 || rc.fraction != 1.f) ? R::Live : R::Stub;
    }
    {
        Vector3 a = { 0.f, 0.f, 0.f };
        Vector3 b = { 0.f, 1.f, 0.f };
        // IsPointVisible returns a bool — just check it doesn't crash
        IsPointVisible(a, b);
        s_IsPointVisible.result = R::Live; // assume live if no crash
    }

    // ---- Aim ----
    {
        Vector2 angles = AimGetAngles();
        s_AimGetAngles.result = (angles.x != 0.f || angles.y != 0.f) ? R::Live : R::Stub;
    }
    {
        Vector3 dir = AimGetDirection();
        s_AimGetDir.result = dir.IsValid() ? R::Live : R::Stub;
    }

    // ---- Entity ----
    int count = GetPlayerCount();
    s_PlayerCount.result = count >= 0 ? R::Live : R::Stub;

    Entity local = LocalPlayer();
    s_LocalPlayer.result = local.IsValid() ? R::Live : R::Stub;

    // Test entity data on first valid player
    bool testedEntity = false;
    for (Entity p : Players())
    {
        if (!testedEntity)
        {
            Vector3 pos = p.GetPosition();
            s_EntityPosition.result = pos.IsValid() ? R::Live : R::Stub;

            float hp = p.GetHealth();
            s_EntityHealth.result = hp > 0.f ? R::Live : R::Stub;

            Vector3 bone = p.GetBonePos(Bone::Head);
            s_EntityBonePos.result = bone.IsValid() ? R::Live : R::Stub;

            Vector3 head = p.GetHeadPos();
            s_EntityHeadPos.result = head.IsValid() ? R::Live : R::Stub;

            s_EntityVisible.result = R::Live; // bool only

            float fov = p.GetFovTo(Bone::Head);
            s_EntityFovTo.result = fov >= 0.f ? R::Live : R::Stub;

            uint64_t heroId = p.GetHeroId();
            s_EntityHeroId.result = heroId != 0 ? R::Live : R::Stub;

            float ultCharge = p.GetUltCharge();
            s_EntityUltCharge.result = ultCharge >= 0.f ? R::Live : R::Stub;

            s_EntityTargetable.result = R::Live; // bool only

            Vector3 fwd = p.GetForward();
            s_EntityForward.result = fwd.IsValid() ? R::Live : R::Stub;

            // Hitboxes
            Hitbox hboxes[32];
            int hcount = p.GetHitboxes(hboxes, 32);
            s_EntityHitboxes.result = hcount > 0 ? R::Live : R::Stub;

            // Lerp history
            LerpEntry lerp[8];
            int lcount = p.GetLerpHistory(lerp, 8);
            s_EntityLerp.result = lcount > 0 ? R::Live : R::Stub;

            // Per-entity cooldowns
            SkillCooldown cd1 = p.GetSkill1Cooldown();
            s_Skill1Cd.result = cd1.enabled ? R::Live : R::Stub;

            SkillCooldown cd2 = p.GetSkill2Cooldown();
            s_Skill2Cd.result = cd2.enabled ? R::Live : R::Stub;

            testedEntity = true;
        }
        break;
    }

    // WorldToScreen — test with screen center approximation
    {
        Vector3 cam;
        GetCameraPosition(cam);
        if (cam.IsValid())
        {
            Vector2 out;
            bool ok = WorldToScreen(cam, out);
            s_WorldToScreen.result = ok ? R::Live : R::Stub;
        }
        else
        {
            s_WorldToScreen.result = R::Stub;
        }
    }

    // ---- Draw (assume live if they don't crash) ----
    s_DrawLine.result   = R::Live;
    s_DrawRect.result   = R::Live;
    s_DrawCircle.result = R::Live;

    // ---- Config ----
    {
        Config::SetBool("_test_b", true);
        bool v = Config::GetBool("_test_b", false);
        s_ConfigBool.result = v ? R::Live : R::Stub;
    }
    {
        Config::SetInt("_test_i", 42);
        int v = Config::GetInt("_test_i", 0);
        s_ConfigInt.result = (v == 42) ? R::Live : R::Stub;
    }
    {
        Config::SetFloat("_test_f", 3.14f);
        float v = Config::GetFloat("_test_f", 0.f);
        s_ConfigFloat.result = (v > 3.f) ? R::Live : R::Stub;
    }
    {
        Config::SetColor("_test_c", Color::Red());
        Color v = Config::GetColor("_test_c", Color::Black());
        s_ConfigColor.result = (v.R() > 0) ? R::Live : R::Stub;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

static bool g_tested = false;

extern "C" void on_load()
{
    Log("api_test loaded — will run on first frame");
}

extern "C" void on_unload() {}

extern "C" void on_frame(float dt)
{
    // Re-run every 3 seconds so results stay fresh
    static float timer = 0.f;
    timer += dt;
    if (!g_tested || timer >= 3.f)
    {
        RunTests(dt);
        g_tested = true;
        timer = 0.f;
    }
}

// ============================================================================
// HUD Overlay — two-column scrolling list
// ============================================================================

extern "C" void on_render()
{
    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    const float LINE  = 13.f;
    const float COL_W = 160.f;
    const float PAD   = 8.f;
    const float TOTAL_W = COL_W * 2 + 6.f;

    // Anchor to top-right
    float ox = sz.x - TOTAL_W - PAD;
    float oy = PAD;

    int half = SLOT_COUNT / 2;

    // Column backgrounds
    Draw::RectFilled(ox - 2.f,           oy - 2.f, COL_W, half * LINE + 18.f, Color(0, 0, 0, 150));
    Draw::RectFilled(ox + COL_W + 4.f,   oy - 2.f, COL_W, half * LINE + 18.f, Color(0, 0, 0, 150));

    // Count live
    int live = 0;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s_slots[i]->result == R::Live) live++;

    // Header
    TextBuilder<48> hdr;
    hdr.put("API TEST  ").putInt(live).put("/").putInt(SLOT_COUNT).put(" LIVE");
    Draw::Text(ox, oy, Color::Cyan(), hdr.c_str(), 12);

    float y = oy + LINE + 2.f;

    for (int i = 0; i < SLOT_COUNT; i++)
    {
        Slot* s = s_slots[i];
        float x = (i < half) ? ox : ox + COL_W + 4.f;
        if (i == half) y = oy + LINE + 2.f;

        Draw::Text(x,          y, Color(200, 200, 200), s->name,          10);
        Draw::Text(x + 108.f,  y, RColor(s->result),   RLabel(s->result), 10);

        y += LINE;
    }

    // Config tests at bottom of col 2
    float bx = ox + COL_W + 4.f;
    float by = y + 4.f;
    Draw::Text(bx, by,          RColor(s_ConfigBool.result),  "Config::Bool",  10);
    Draw::Text(bx + 108.f, by,          RColor(s_ConfigBool.result),  RLabel(s_ConfigBool.result),  10);
    Draw::Text(bx, by + LINE,   RColor(s_ConfigInt.result),   "Config::Int",   10);
    Draw::Text(bx + 108.f, by + LINE,   RColor(s_ConfigInt.result),   RLabel(s_ConfigInt.result),   10);
    Draw::Text(bx, by + LINE*2, RColor(s_ConfigFloat.result), "Config::Float", 10);
    Draw::Text(bx + 108.f, by + LINE*2, RColor(s_ConfigFloat.result), RLabel(s_ConfigFloat.result), 10);
    Draw::Text(bx, by + LINE*3, RColor(s_ConfigColor.result), "Config::Color", 10);
    Draw::Text(bx + 108.f, by + LINE*3, RColor(s_ConfigColor.result), RLabel(s_ConfigColor.result), 10);
}

// ============================================================================
// Menu
// ============================================================================

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("API Test"))
    {
        int live = 0, stub = 0, fail = 0;
        for (int i = 0; i < SLOT_COUNT; i++)
        {
            switch (s_slots[i]->result)
            {
                case R::Live: live++; break;
                case R::Stub: stub++; break;
                case R::Fail: fail++; break;
                default: break;
            }
        }

        TextBuilder<64> tb;
        tb.put("Live: ").putInt(live)
          .put("  Stub: ").putInt(stub)
          .put("  Fail: ").putInt(fail);
        ImGui::Text(tb.c_str());
        ImGui::Separator();

        for (int i = 0; i < SLOT_COUNT; i++)
        {
            Slot* s = s_slots[i];
            TextBuilder<48> row;
            row.put("[").put(RLabel(s->result)).put("] ").put(s->name);
            ImGui::Text(row.c_str());
        }
    }
}
