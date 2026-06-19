#include <xenon/SDK.hpp>

using namespace xenon;

// ── Configuration ────────────────────────────────────────
static bool g_enabled = true;
static int  g_boxStyle = 2;
static bool g_showSkeleton = true;
static bool g_showJoints = true;
static bool g_showHealthBar = true;
static bool g_showSnaplines = false;
static bool g_showHeroName = true;
static bool g_showDistance = true;
static bool g_distanceFade = true;
static bool g_visibleOnly = false;
static float g_maxDistance = 150.f;

// ── Drawing Extensions ───────────────────────────────────
static void DrawSleekHealthBar(float x, float y, float w, float h,
                               float pct, uint8_t alpha)
{
    Draw::RectFilled(x - 1.f, y - 1.f, w + 2.f, h + 2.f,
                     Color(10, 10, 10, static_cast<uint8_t>(alpha * 0.8f)));
    float fillH = h * pct;
    Color healthCol = Color::HealthGradient(pct, alpha);
    Draw::RectFilled(x, y + (h - fillH), w, fillH, healthCol);
}

// ── Lifecycle & Info ─────────────────────────────────────
XENON_PLUGIN_INFO(
    "Visuals", "Visuals", "Xenon",
    "Official base visuals", "1.0",
    0, PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()
{
    g_enabled       = Config::GetBool("esp_enabled", true);
    g_boxStyle      = Config::GetInt("esp_boxStyle", 2);
    g_showSkeleton  = Config::GetBool("esp_showSkeleton", true);
    g_showJoints    = Config::GetBool("esp_showJoints", true);
    g_showHealthBar = Config::GetBool("esp_showHealthBar", true);
    g_showSnaplines = Config::GetBool("esp_showSnaplines", false);
    g_showHeroName  = Config::GetBool("esp_showHeroName", true);
    g_showDistance  = Config::GetBool("esp_showDistance", true);
    g_distanceFade  = Config::GetBool("esp_distanceFade", true);
    g_visibleOnly   = Config::GetBool("esp_visibleOnly", false);
    g_maxDistance   = Config::GetFloat("esp_maxDistance", 150.f);
}

extern "C" void on_unload()
{
    Config::SetBool("esp_enabled", g_enabled);
    Config::SetInt("esp_boxStyle", g_boxStyle);
    Config::SetBool("esp_showSkeleton", g_showSkeleton);
    Config::SetBool("esp_showJoints", g_showJoints);
    Config::SetBool("esp_showHealthBar", g_showHealthBar);
    Config::SetBool("esp_showSnaplines", g_showSnaplines);
    Config::SetBool("esp_showHeroName", g_showHeroName);
    Config::SetBool("esp_showDistance", g_showDistance);
    Config::SetBool("esp_distanceFade", g_distanceFade);
    Config::SetBool("esp_visibleOnly", g_visibleOnly);
    Config::SetFloat("esp_maxDistance", g_maxDistance);
    Config::Save();
}

// ── Rendering ────────────────────────────────────────────
extern "C" void on_render()
{
    if (!g_enabled) return;

    Vector2 screenSz = ScreenSize();
    Vector2 snaplineOrigin(screenSz.x * 0.5f, screenSz.y);

    for (Entity enemy : Players())
    {
        if (!enemy.IsAlive() || !enemy.IsEnemy()) continue;

        float dist = enemy.GetDistance();
        if (dist > g_maxDistance) continue;

        bool visible = enemy.IsVisible();
        if (g_visibleOnly && !visible) continue;

        // Distance fading
        float alphaMult = 1.0f;
        if (g_distanceFade && dist > (g_maxDistance * 0.4f)) {
            float fadeRange = g_maxDistance * 0.6f;
            if (fadeRange < 1.0f) fadeRange = 1.0f;
            alphaMult = 1.0f - ((dist - (g_maxDistance * 0.4f)) / fadeRange);
            if (alphaMult < 0.05f) continue;
        }
        uint8_t alpha = static_cast<uint8_t>(255.f * alphaMult);

        float pct = enemy.GetHealthPercent() / 100.f;
        Color baseColor = visible
            ? Color(255, 255, 255, alpha)
            : Color(180, 180, 180, static_cast<uint8_t>(alpha * 0.6f));

        Vector3 rootPos = enemy.GetPosition();
        float yaw = enemy.GetYaw();

        // Box rendering (2D or 3D)
        bool boxValid = false;
        float minX = 0, minY = 0, maxX = 0, maxY = 0;

        if (g_boxStyle == 3) {
            // 3D OBB from AABB bounds
            Vector3 dMin = enemy.GetBoundsMin();
            Vector3 dMax = enemy.GetBoundsMax();
            Vector3 corners[8] = {
                {dMin.x, dMin.y, dMin.z}, {dMax.x, dMin.y, dMin.z},
                {dMax.x, dMax.y, dMin.z}, {dMin.x, dMax.y, dMin.z},
                {dMin.x, dMin.y, dMax.z}, {dMax.x, dMin.y, dMax.z},
                {dMax.x, dMax.y, dMax.z}, {dMin.x, dMax.y, dMax.z}
            };
            Vector2 s[8];
            boxValid = true;
            for (int i = 0; i < 8; ++i) {
                if (!WorldToScreen(rootPos + corners[i].RotatedY(yaw), s[i])) {
                    boxValid = false; break;
                }
            }
            if (boxValid) {
                Draw::Line(s[0],s[1],baseColor,1.2f); Draw::Line(s[1],s[2],baseColor,1.2f);
                Draw::Line(s[2],s[3],baseColor,1.2f); Draw::Line(s[3],s[0],baseColor,1.2f);
                Draw::Line(s[4],s[5],baseColor,1.2f); Draw::Line(s[5],s[6],baseColor,1.2f);
                Draw::Line(s[6],s[7],baseColor,1.2f); Draw::Line(s[7],s[4],baseColor,1.2f);
                Draw::Line(s[0],s[4],baseColor,1.2f); Draw::Line(s[1],s[5],baseColor,1.2f);
                Draw::Line(s[2],s[6],baseColor,1.2f); Draw::Line(s[3],s[7],baseColor,1.2f);
                minX = 99999.f; minY = 99999.f; maxX = -99999.f; maxY = -99999.f;
                for (int i = 0; i < 8; i++) {
                    if (s[i].x < minX) minX = s[i].x; if (s[i].y < minY) minY = s[i].y;
                    if (s[i].x > maxX) maxX = s[i].x; if (s[i].y > maxY) maxY = s[i].y;
                }
            }
        }
        else if (g_boxStyle == 1 || g_boxStyle == 2) {
            Vector2 rootScr, headScr;
            if (WorldToScreen(rootPos, rootScr) &&
                WorldToScreen(enemy.GetBonePos(Bone::Head), headScr)) {
                boxValid = true;
                float height = fabsf(rootScr.y - headScr.y) * 1.15f;
                float width = height * 0.65f;
                minX = rootScr.x - (width * 0.5f);
                maxX = rootScr.x + (width * 0.5f);
                minY = rootScr.y - height;
                maxY = rootScr.y;

                if (g_boxStyle == 1)
                    Draw::RectCorners(minX, minY, maxX-minX, maxY-minY, baseColor, 1.5f);
                else
                    Draw::Rect(minX, minY, maxX-minX, maxY-minY, baseColor, 1.0f);
            }
        }

        if (boxValid) {
            if (g_showHealthBar)
                DrawSleekHealthBar(minX - 7.f, minY, 3.0f, maxY - minY, pct, alpha);
            if (g_showSnaplines)
                Draw::Line(snaplineOrigin, Vector2((minX+maxX)*0.5f, maxY),
                           baseColor.WithAlpha(static_cast<uint8_t>(alpha*0.4f)), 1.0f);
            float centerX = minX + (maxX - minX) * 0.5f;
            if (g_showHeroName)
                Draw::TextCentered(centerX, minY - 15.f,
                    Color::White().WithAlpha(alpha), GetHeroName(enemy.GetHeroId()));
            if (g_showDistance) {
                TextBuilder<32> buf;
                buf.putInt(static_cast<int>(dist)).put("m");
                Draw::TextCentered(centerX, maxY + 4.f,
                    Color(200, 200, 200, alpha), buf.c_str());
            }
        }

        if (g_showSkeleton) {
            BonePair conn[21];
            int count = GetSkeletonConnections(enemy.GetHeroId(), conn);
            for (int i = 0; i < count; ++i) {
                Vector2 sFrom, sTo;
                if (WorldToScreen(enemy.GetBonePos(conn[i].a), sFrom) &&
                    WorldToScreen(enemy.GetBonePos(conn[i].b), sTo)) {
                    Draw::Line(sFrom, sTo,
                        baseColor.WithAlpha(static_cast<uint8_t>(alpha*0.7f)), 1.5f);
                    if (g_showJoints) {
                        Draw::CircleFilled(sFrom, 2.0f, baseColor);
                        Draw::CircleFilled(sTo, 2.0f, baseColor);
                    }
                }
            }
        }
    }
}

// ── Menu ─────────────────────────────────────────────────
extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Visuals"))
    {
        ImGui::Checkbox("Enable ESP", &g_enabled);
        ImGui::Combo("Box Style", &g_boxStyle,
                     "Off\0" "2D Corner Box\0" "2D Full Box\0" "3D Bounding Box\0");
        ImGui::Separator();
        ImGui::Checkbox("Draw Skeleton", &g_showSkeleton);
        ImGui::Separator();
        ImGui::Checkbox("Draw Health Bar", &g_showHealthBar);
        ImGui::Checkbox("Draw Snaplines", &g_showSnaplines);
        ImGui::Separator();
        ImGui::Checkbox("Show Hero Name", &g_showHeroName);
        ImGui::Checkbox("Show Distance", &g_showDistance);
        ImGui::Separator();
        ImGui::Checkbox("Dynamic Distance Fading", &g_distanceFade);
        ImGui::Checkbox("Visible Only", &g_visibleOnly);
        ImGui::SliderFloat("Max Render Distance", &g_maxDistance, 20.f, 300.f);
    }
}
