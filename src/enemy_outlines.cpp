// Enemy & Team Outlines Plugin
// Applies in-game outlines/glow to players with configurable colors.

#include <xenon/SDK.hpp>

using namespace xenon;

// ============================================================================
// Configuration
// ============================================================================

static bool g_enabled = true;

// Enemy
static bool g_enemyVisible = true;
static Color g_enemyVisColor(255, 0, 0);
static bool g_enemyOccluded = true;
static Color g_enemyOccColor(255, 100, 0);

// Team
static bool g_teamVisible = false;
static Color g_teamVisColor(0, 255, 0);
static bool g_teamOccluded = false;
static Color g_teamOccColor(0, 150, 255);

// Health-based glow (overrides static color with red→yellow→green gradient)
static bool g_healthEnemy = false;
static bool g_healthTeam  = false;

// ============================================================================
// Plugin Info
// ============================================================================

XENON_PLUGIN_INFO(
    "Outlines",
    "Outlines",
    "Xenon",
    "Configurable outline/glow for enemies and teammates.",
    "1.0",
    0,
    PluginFlags::HasMenu
)

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" void on_load()
{
    g_enabled       = Config::GetBool("enabled", true);

    g_enemyVisible  = Config::GetBool("enemyVisible", true);
    g_enemyVisColor = Config::GetColor("enemyVis", Color(255, 0, 0));

    g_enemyOccluded = Config::GetBool("enemyOccluded", true);
    g_enemyOccColor = Config::GetColor("enemyOcc", Color(255, 100, 0));

    g_teamVisible   = Config::GetBool("teamVisible", false);
    g_teamVisColor  = Config::GetColor("teamVis", Color(0, 255, 0));

    g_teamOccluded  = Config::GetBool("teamOccluded", false);
    g_teamOccColor  = Config::GetColor("teamOcc", Color(0, 150, 255));

    g_healthEnemy   = Config::GetBool("healthEnemy", false);
    g_healthTeam    = Config::GetBool("healthTeam", false);

    {
        TextBuilder<128> tb;
        tb.put("Config: enemyVis=").putInt(g_enemyVisible)
          .put(" enemyOcc=").putInt(g_enemyOccluded)
          .put(" teamVis=").putInt(g_teamVisible)
          .put(" teamOcc=").putInt(g_teamOccluded);
        LogDebug(tb.c_str());
    }
    Log("Outlines v1.0 loaded");
}

extern "C" void on_unload()
{
    LogDebug("Saving config...");

    Config::SetBool("enabled", g_enabled);

    Config::SetBool("enemyVisible", g_enemyVisible);
    Config::SetColor("enemyVis", g_enemyVisColor);

    Config::SetBool("enemyOccluded", g_enemyOccluded);
    Config::SetColor("enemyOcc", g_enemyOccColor);

    Config::SetBool("teamVisible", g_teamVisible);
    Config::SetColor("teamVis", g_teamVisColor);

    Config::SetBool("teamOccluded", g_teamOccluded);
    Config::SetColor("teamOcc", g_teamOccColor);

    Config::SetBool("healthEnemy", g_healthEnemy);
    Config::SetBool("healthTeam", g_healthTeam);

    Config::Save();
    Log("Outlines unloaded, config saved");
}

// ============================================================================
// Frame Logic
// ============================================================================

static float g_logTimer = 0.f;
static const float LOG_INTERVAL = 5.f; // log stats every 5 seconds

extern "C" void on_frame(float dt)
{
    if (!g_enabled)
        return;

    int enemyOutlined = 0;
    int teamOutlined  = 0;
    int processed     = 0;

    for (Entity player : Players())
    {
        if (!player.IsAlive() || player.IsLocal())
            continue;

        processed++;
        bool visible = player.IsVisible();

        if (player.IsEnemy())
        {
            bool useHealth = g_healthEnemy;
            Color hpColor = useHealth
                ? Color::HealthGradient(player.GetHealthPercent() / 100.f)
                : Color();

            if (visible && g_enemyVisible)
            {
                player.SetOutlineVisible(useHealth ? hpColor : g_enemyVisColor);
                enemyOutlined++;
            }
            else if (!visible && g_enemyOccluded)
            {
                player.SetOutlineOccluded(useHealth ? hpColor : g_enemyOccColor);
                enemyOutlined++;
            }
        }
        else if (player.IsAlly())
        {
            bool useHealth = g_healthTeam;
            Color hpColor = useHealth
                ? Color::HealthGradient(player.GetHealthPercent() / 100.f)
                : Color();

            if (visible && g_teamVisible)
            {
                player.SetOutlineVisible(useHealth ? hpColor : g_teamVisColor);
                teamOutlined++;
            }
            else if (!visible && g_teamOccluded)
            {
                player.SetOutlineOccluded(useHealth ? hpColor : g_teamOccColor);
                teamOutlined++;
            }
        }
    }

    g_logTimer += dt;
    if (g_logTimer >= LOG_INTERVAL)
    {
        g_logTimer = 0.f;
        TextBuilder<128> tb;
        tb.put("Frame stats: ").putInt(processed).put(" players, ")
          .putInt(enemyOutlined).put(" enemy outlined, ")
          .putInt(teamOutlined).put(" team outlined");
        LogDebug(tb.c_str());
    }
}

// ============================================================================
// Menu
// ============================================================================

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Outlines"))
    {
        ImGui::Checkbox("Enable", &g_enabled);
        ImGui::Separator();

        ImGui::Text("Enemy");
        ImGui::Checkbox("Enemy Visible", &g_enemyVisible);
        if (g_enemyVisible && !g_healthEnemy)
            ImGui::ColorPicker("EV", &g_enemyVisColor);

        ImGui::Checkbox("Enemy Occluded", &g_enemyOccluded);
        if (g_enemyOccluded && !g_healthEnemy)
            ImGui::ColorPicker("EO", &g_enemyOccColor);

        ImGui::Checkbox("Health-based (Enemy)", &g_healthEnemy);

        ImGui::Separator();
        ImGui::Text("Team");
        ImGui::Checkbox("Team Visible", &g_teamVisible);
        if (g_teamVisible && !g_healthTeam)
            ImGui::ColorPicker("TV", &g_teamVisColor);

        ImGui::Checkbox("Team Occluded", &g_teamOccluded);
        if (g_teamOccluded && !g_healthTeam)
            ImGui::ColorPicker("TO", &g_teamOccColor);

        ImGui::Checkbox("Health-based (Team)", &g_healthTeam);
    }
}
