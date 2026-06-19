// Mei Plugin
// Auto Ice Block (Cryo-Freeze / Skill1) when local HP drops to a threshold.

#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "mei", "Mei", "Xenon",
    "Auto Ice Block (Cryo-Freeze) when HP drops below a threshold.",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ─────────────────────────────────────────────
//  Settings
// ─────────────────────────────────────────────
static bool  g_enabled     = true;
static bool  g_autoIce     = true;
static float g_iceHpPct    = 35.f;   // ice block at or below this HP %
static bool  g_showDebug   = true;   // show the on-screen status/debug overlay

// Press hold-window (a press+release in the same frame fires too fast to register)
static float g_iceHoldEnd  = 0.f;
static constexpr float kIceHold = 0.12f;

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
extern "C" void on_load()
{
    g_enabled  = Config::GetBool("enabled",   true);
    g_autoIce  = Config::GetBool("autoIce",   true);
    g_iceHpPct = Config::GetFloat("iceHpPct", 35.f);
    g_showDebug = Config::GetBool("showDebug", true);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",   g_enabled);
    Config::SetBool("autoIce",   g_autoIce);
    Config::SetFloat("iceHpPct", g_iceHpPct);
    Config::SetBool("showDebug", g_showDebug);
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t)
{
    g_iceHoldEnd = 0.f;
}

// ─────────────────────────────────────────────
//  on_frame — trigger Ice Block on low HP
// ─────────────────────────────────────────────
extern "C" void on_frame(float)
{
    if (!g_enabled || !IsIngame()) return;
    Entity lp = LocalPlayer();
    if (!lp.IsValid() || lp.GetHeroId() != HeroId::Mei) return;  // dormant on any other hero

    float now = GetTime();

    // Maintain / release the press window
    if (g_iceHoldEnd > 0.f)
    {
        if (now < g_iceHoldEnd) PressGameButton(GameButton::Skill1);
        else { ReleaseGameButton(GameButton::Skill1); g_iceHoldEnd = 0.f; }
        return;
    }

    if (!g_autoIce || !lp.IsAlive()) return;

    // Ready = not already in Cryo-Freeze and not on cooldown
    bool ready = !IsSkill1Active() && !GetSkill1Cooldown().IsOnCooldown();
    if (!ready) return;

    float hpPct = lp.GetHealthPercent();
    if (hpPct > 0.f && hpPct <= g_iceHpPct)
        g_iceHoldEnd = now + kIceHold;
}

// ─────────────────────────────────────────────
//  on_render — small status readout
// ─────────────────────────────────────────────
extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    Entity lp = LocalPlayer();
    if (!lp.IsValid() || lp.GetHeroId() != HeroId::Mei) return;
    if (!g_showDebug) return;

    SkillCooldown cd = GetSkill1Cooldown();
    bool active = IsSkill1Active();
    bool ready  = !active && !cd.IsOnCooldown();
    TextBuilder<128> tb;
    tb.put("[Mei] HP:").putFloat(lp.GetHealthPercent(), 0)
      .put("/").putFloat(g_iceHpPct, 0)
      .put("%  IceBlock:").put(g_autoIce ? (ready ? "ready" : "cd/active") : "off");
    Draw::TextShadow(12.f, 90.f, ready ? Color::Green() : Color(255,180,0,255), tb.c_str());

    TextBuilder<128> tb2;
    tb2.put("  s1active:").put(active ? "Y" : "n")
       .put(" cdEnabled:").put(cd.enabled ? "Y" : "n")
       .put(" cdCur:").putFloat(cd.current, 2)
       .put(" cdMax:").putFloat(cd.max, 2)
       .put(" hold:").put(g_iceHoldEnd > 0.f ? "Y" : "n");
    Draw::TextShadow(12.f, 108.f, Color::White(), tb2.c_str());
}

// ─────────────────────────────────────────────
//  on_menu
// ─────────────────────────────────────────────
extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Mei"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Auto Ice Block", &g_autoIce);
        ImGui::SliderFloat("Ice Block at HP % (or below)", &g_iceHpPct, 1.f, 99.f);
        ImGui::Checkbox("Show Debug Overlay", &g_showDebug);
    }
}
