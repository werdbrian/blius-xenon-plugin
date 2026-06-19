// Wu Yang Plugin
// LMB held = projectile fires and steers toward current aim.
// Plugin holds LMB, estimates projectile world position each frame,
// and raycasts from that position to the target. When LOS clears,
// snaps aim to target so the projectile curves in.

#include <xenon/SDK.hpp>
using namespace xenon;

static bool  g_enabled     = true;
static int   g_triggerKey  = 1;       // hold this key to activate
static float g_fovRadius   = 150.f;   // target scan FOV radius (px)
static float g_maxRange    = 40.f;    // max target distance (m)
static float g_stiffness   = 150.f;   // aim smoothing when steering to target
static bool  g_drawDebug   = true;

// Target cache (written on_render, read on_frame)
static int32_t g_targetIdx   = -1;
static bool    g_targetValid = false;
static Vector3 g_targetHead  = {};
static float   g_targetDist  = 0.f;
static float   g_targetHp    = 0.f;

// Projectile tracking
static bool    g_lmbHeld   = false;   // whether trigger is currently held
static float   g_fireTime  = 0.f;    // timestamp when trigger was pressed
static Vector3 g_firePos   = {};     // camera position at fire time
static Vector3 g_fireDir   = {};     // camera forward at fire time
static Vector3 g_projPos   = {};     // current estimated projectile world position
static bool    g_hasLOS    = false;  // elapsed >= snapTime (steering active)
static float   g_snapTime  = -1.f;  // seconds after fire when proj first gains LOS
static Vector3 g_snapPoint = {};    // world position where LOS first clears

static constexpr float kProjSpeed  = 60.f;   // m/s
static constexpr float kPathStep   = 0.5f;   // sample every 0.5m along path
static constexpr float kMaxAge     = 3.f;    // reset after 3s

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
    return Sqrt(dx * dx + dy * dy);
}
static float WorldDist(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return Sqrt(dx * dx + dy * dy + dz * dz);
}

XENON_PLUGIN_INFO(
    "wuyang",
    "Wu Yang",
    "Xenon",
    "Auto-steer projectile to target when LOS clears.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

static uint32_t g_frameCounter = 0;

// First-sample raycast diagnostics
static bool    g_dbgRcHit       = false;
static float   g_dbgRcFraction  = 1.f;
static Vector3 g_dbgRcHitPos    = {};
static float   g_dbgDistToTgt   = -1.f;
static int     g_dbgSamplesTried = 0;

// Independent raycast self-test (runs every frame)
static bool    g_testRcReady    = false;
static bool    g_testFwdHit     = false;
static float   g_testFwdFrac    = 0.f;
static bool    g_testCamValid   = false;
static Vector3 g_testCamPos     = {};
static Vector3 g_testCamFwd     = {};

extern "C" void on_load()
{
    g_enabled    = Config::GetBool("enabled",    true);
    g_triggerKey = Config::GetInt("triggerKey",  1);
    g_fovRadius  = Config::GetFloat("fovRadius", 150.f);
    g_maxRange   = Config::GetFloat("maxRange",  40.f);
    g_stiffness  = Config::GetFloat("stiffness", 150.f);
    g_drawDebug  = Config::GetBool("drawDebug",  true);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",    g_enabled);
    Config::SetInt("triggerKey",  g_triggerKey);
    Config::SetFloat("fovRadius", g_fovRadius);
    Config::SetFloat("maxRange",  g_maxRange);
    Config::SetFloat("stiffness", g_stiffness);
    Config::SetBool("drawDebug",  g_drawDebug);
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t)
{
    g_targetValid = false;
    g_targetIdx   = -1;
    g_lmbHeld     = false;
    g_fireTime    = 0.f;
    g_hasLOS      = false;
    g_projPos     = {};
    AimResetSmoothing();
}

extern "C" void on_frame(float /*dt*/)
{
    g_frameCounter++;
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Wuyang) return; }  // dormant on any other hero

    bool held = IsKeyDown(g_triggerKey);
    float now = GetTime();

    if (!held)
    {
        if (g_lmbHeld)
        {
            g_lmbHeld  = false;
            g_fireTime = 0.f;
            g_hasLOS   = false;
            g_projPos  = {};
            AimResetSmoothing();
        }
        return;
    }

    // First frame of activation — or fire dir not yet recorded — capture camera state
    // Use the cached values from on_render's self-test (confirmed working)
    if (!g_lmbHeld || !g_fireDir.IsValid())
    {
        if (g_testCamValid && g_testCamFwd.IsValid() && g_testCamPos.IsValid())
        {
            g_firePos  = { g_testCamPos.x + g_testCamFwd.x, g_testCamPos.y + g_testCamFwd.y, g_testCamPos.z + g_testCamFwd.z };
            g_fireDir  = g_testCamFwd;
            g_fireTime = now;
        }
        if (!g_lmbHeld)
        {
            g_lmbHeld = true;
            AimResetSmoothing();
        }
    }

    // Reset if projectile has expired
    float elapsed = now - g_fireTime;
    if (elapsed > kMaxAge)
    {
        if (g_testCamValid && g_testCamFwd.IsValid() && g_testCamPos.IsValid())
        {
            g_firePos  = { g_testCamPos.x + g_testCamFwd.x, g_testCamPos.y + g_testCamFwd.y, g_testCamPos.z + g_testCamFwd.z };
            g_fireDir  = g_testCamFwd;
            g_fireTime = now;
        }
        elapsed   = 0.f;
        g_snapTime = -1.f;
        g_snapPoint = {};
    }

    // Current estimated projectile position
    float travelDist = kProjSpeed * elapsed;
    g_projPos = {
        g_firePos.x + g_fireDir.x * travelDist,
        g_firePos.y + g_fireDir.y * travelDist,
        g_firePos.z + g_fireDir.z * travelDist
    };

    // Path scan result is computed in on_render (raycasts are safe there).
    // on_frame just reads the cached snap time.
    g_hasLOS = (g_snapTime >= 0.f && elapsed >= g_snapTime);
    if (g_hasLOS && g_targetValid && g_targetHead.IsValid())
        AimAtPosition(g_targetHead, g_stiffness);
}

extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Wuyang) return; }  // dormant on any other hero

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    // Raycast self-test: every frame, cast 50m forward from camera and report result
    g_testRcReady  = IsRaycastReady();
    g_testCamValid = GetCameraPosition(g_testCamPos) && GetCameraForward(g_testCamFwd);
    if (g_testRcReady && g_testCamValid)
    {
        Vector3 endPt = {
            g_testCamPos.x + g_testCamFwd.x * 50.f,
            g_testCamPos.y + g_testCamFwd.y * 50.f,
            g_testCamPos.z + g_testCamFwd.z * 50.f
        };
        RaycastResult rc = Raycast(g_testCamPos, endPt);
        g_testFwdHit  = rc.IsHit();
        g_testFwdFrac = rc.fraction;
    }

    // ── Target scan ──
    {
        int32_t bestIdx   = -1;
        float   bestScore = 99999.f;
        float   bestHp    = 0.f;
        Vector3 bestHead  = {};
        float   bestDist  = 0.f;

        Entity  local = LocalPlayer();
        Vector3 myPos = local.IsValid() ? local.GetPosition() : Vector3{};

        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
            // No IsVisible filter — Wu Yang can bend around walls to hit occluded targets
            Vector3 head = p.GetHeadPos();
            head.y -= 0.7f; // lower body offset below head
            if (!head.IsValid()) continue;
            float d3 = WorldDist(myPos, p.GetPosition());
            if (d3 > g_maxRange) continue;
            Vector2 hs;
            if (!WorldToScreen(head, hs)) continue;
            float sd = ScreenDist(hs, center);
            if (sd > g_fovRadius) continue;
            if (sd < bestScore)
            {
                bestScore = sd;
                bestIdx   = p.Index();
                bestHp    = p.GetHealth();
                bestHead  = head;
                bestDist  = d3;
            }
        }

        g_targetValid = (bestIdx >= 0);
        g_targetIdx   = bestIdx;
        g_targetHead  = bestHead;
        g_targetDist  = bestDist;
        g_targetHp    = bestHp;
    }

    // ── Path scan (raycasts must run in on_render) ──
    g_snapTime  = -1.f;
    g_snapPoint = {};
    g_dbgSamplesTried = 0;
    if (g_targetValid && g_targetHead.IsValid() && g_testRcReady && g_fireDir.IsValid())
    {
        int nSamples = (int)(g_maxRange / kPathStep);
        for (int i = 0; i < nSamples; i++)
        {
            float d = kPathStep * (i + 1);
            Vector3 pt = {
                g_firePos.x + g_fireDir.x * d,
                g_firePos.y + g_fireDir.y * d,
                g_firePos.z + g_fireDir.z * d
            };
            RaycastResult rc = Raycast(pt, g_targetHead);
            g_dbgSamplesTried = i + 1;
            if (i == 0)
            {
                g_dbgRcHit      = rc.IsHit();
                g_dbgRcFraction = rc.fraction;
                g_dbgRcHitPos   = rc.hitPos;
                g_dbgDistToTgt  = rc.hitPos.IsValid() ? WorldDist(rc.hitPos, g_targetHead) : -1.f;
            }
            bool clear = !rc.IsHit();
            if (!clear && rc.hitPos.IsValid())
                if (WorldDist(rc.hitPos, g_targetHead) < 2.0f) clear = true;
            if (clear) { g_snapTime = d / kProjSpeed; g_snapPoint = pt; break; }
        }
    }

    if (!g_drawDebug) return;

    // FOV ring
    Draw::Circle(center, g_fovRadius, Color(255, 255, 255, 40), 1.f);

    // On-screen status (top-left)
    {
        float x = 20.f;
        float y = sz.y * 0.30f;
        float lh = 18.f;

        TextBuilder<64> l1;
        l1.put("WY lmb:").put(g_lmbHeld ? "HELD" : "off")
          .put(" key:").put(IsKeyDown(g_triggerKey) ? "DOWN" : "up");
        Draw::TextShadow(x, y, Color::White(), l1.c_str(), 16);
        y += lh;

        // Always-on raycast self-test
        TextBuilder<80> lt;
        lt.put("[RC test] rdy:").put(g_testRcReady ? "Y" : "N")
          .put(" cam:").put(g_testCamValid ? "Y" : "N")
          .put(" fwdHit:").put(g_testFwdHit ? "Y" : "N")
          .put(" frac:").putFloat(g_testFwdFrac, 2);
        Draw::TextShadow(x, y, Color(255, 200, 100, 255), lt.c_str(), 16);
        y += lh;

        TextBuilder<64> l2;
        l2.put("Tgt:").put(g_targetValid ? "YES" : "NO")
          .put(" dist:").putFloat(g_targetDist, 1)
          .put(" hp:").putFloat(g_targetHp, 0);
        Draw::TextShadow(x, y, Color(255, 255, 0, 255), l2.c_str(), 16);
        y += lh;

        TextBuilder<64> l3;
        l3.put("RC:").put(IsRaycastReady() ? "RDY" : "WAIT")
          .put(" LOS:").put(g_hasLOS ? "CLEAR" : "BLOCK");
        Draw::TextShadow(x, y, g_hasLOS ? Color::Green() : Color(255,160,0,255), l3.c_str(), 16);
        y += lh;

        TextBuilder<64> l4;
        if (g_snapTime >= 0.f)
            l4.put("snap:").putFloat(g_snapTime, 2).put("s @").putFloat(g_snapTime * kProjSpeed, 1).put("m");
        else
            l4.put("snap: NONE (no LOS along path)");
        Draw::TextShadow(x, y, Color::Cyan(), l4.c_str(), 16);
        y += lh;

        if (g_lmbHeld)
        {
            TextBuilder<64> l5;
            float elapsed = GetTime() - g_fireTime;
            l5.put("age:").putFloat(elapsed, 2)
              .put("s  proj:").putFloat(kProjSpeed * elapsed, 1).put("m");
            Draw::TextShadow(x, y, Color::White(), l5.c_str(), 16);
            y += lh;

            TextBuilder<80> l6;
            l6.put("rc0 hit:").put(g_dbgRcHit ? "Y" : "N")
              .put(" frac:").putFloat(g_dbgRcFraction, 2)
              .put(" tgtDist:").putFloat(g_dbgDistToTgt, 2);
            Draw::TextShadow(x, y, Color(255,200,200,255), l6.c_str(), 16);
            y += lh;

            TextBuilder<80> l7;
            l7.put("samples:").putInt(g_dbgSamplesTried)
              .put(" fireDir.z:").putFloat(g_fireDir.z, 2);
            Draw::TextShadow(x, y, Color(200,200,255,255), l7.c_str(), 16);
            y += lh;

            // Gate conditions for the path-scan loop
            TextBuilder<80> l8;
            l8.put("gate: tgt:").put(g_targetValid ? "Y" : "N")
              .put(" tgtHead:").put(g_targetHead.IsValid() ? "Y" : "N")
              .put(" rcRdy:").put(IsRaycastReady() ? "Y" : "N")
              .put(" fireDir:").put(g_fireDir.IsValid() ? "Y" : "N");
            Draw::TextShadow(x, y, Color(255,255,200,255), l8.c_str(), 16);
            y += lh;

            // Show raw vectors
            TextBuilder<96> l9;
            l9.put("fPos:").putFloat(g_firePos.x, 1).put(",").putFloat(g_firePos.y, 1).put(",").putFloat(g_firePos.z, 1)
              .put(" fDir:").putFloat(g_fireDir.x, 2).put(",").putFloat(g_fireDir.y, 2).put(",").putFloat(g_fireDir.z, 2);
            Draw::TextShadow(x, y, Color(200,255,200,255), l9.c_str(), 14);
        }
    }
}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Wu Yang"))
    {
        ImGui::Checkbox("Enabled",    &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Draw Debug", &g_drawDebug);
        ImGui::SliderInt("Trigger Key",       &g_triggerKey, 0,    31);
        ImGui::SliderFloat("FOV Radius (px)", &g_fovRadius,  50.f, 400.f);
        ImGui::SliderFloat("Max Range (m)",   &g_maxRange,   5.f,  80.f);
        ImGui::SliderFloat("Aim Stiffness",   &g_stiffness,  10.f, 500.f);
        ImGui::Separator();
        {
            TextBuilder<80> db;
            db.put("Tgt:").put(g_targetValid ? "YES" : "NO")
              .put(" dist:").putFloat(g_targetDist, 1)
              .put(" hp:").putFloat(g_targetHp, 0);
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("RC:").put(IsRaycastReady() ? "RDY" : "WAIT")
              .put(" LOS:").put(g_hasLOS ? "CLEAR" : "BLOCK")
              .put(" lmb:").put(g_lmbHeld ? "HELD" : "off")
              .put(" f:").putInt((int)g_frameCounter);
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            db.put("KeyState:").put(IsKeyDown(g_triggerKey) ? "DOWN" : "up")
              .put(" Key#:").putInt(g_triggerKey)
              .put(" InGame:").put(IsIngame() ? "Y" : "N")
              .put(" En:").put(g_enabled ? "Y" : "N");
            ImGui::Text(db.c_str());
        }
        {
            TextBuilder<80> db;
            float elapsed = (g_fireTime > 0.f) ? (GetTime() - g_fireTime) : 0.f;
            db.put("Age:").putFloat(elapsed, 2)
              .put("s  snap:").put(g_snapTime >= 0.f ? "" : "none");
            if (g_snapTime >= 0.f)
                db.putFloat(g_snapTime, 2).put("s @").putFloat(g_snapTime * kProjSpeed, 1).put("m");
            ImGui::Text(db.c_str());
        }
    }
}
