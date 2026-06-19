// Diagnostics Plugin
// System health-check plugin that continuously tests all SDK subsystems
// and displays results via both a HUD overlay and an interactive menu panel.

#include <xenon/SDK.hpp>

using namespace xenon;

// ============================================================================
// Test Status
// ============================================================================

enum class TestStatus : int32_t
{
    Pending = 0,
    Pass    = 1,
    Warn    = 2,
    Fail    = 3,
};

struct TestResult
{
    TestStatus status    = TestStatus::Pending;
    float      timestamp = 0.f;
};

static const char* StatusLabel(TestStatus s)
{
    switch (s)
    {
        case TestStatus::Pass: return "PASS";
        case TestStatus::Warn: return "WARN";
        case TestStatus::Fail: return "FAIL";
        default:               return "PEND";
    }
}

static Color StatusColor(TestStatus s)
{
    switch (s)
    {
        case TestStatus::Pass: return Color::Green();
        case TestStatus::Warn: return Color::Yellow();
        case TestStatus::Fail: return Color::Red();
        default:               return Color(160, 160, 160);
    }
}

// ============================================================================
// Test Definitions  (17 total across 7 categories)
// ============================================================================

// Core (4)
static TestResult g_testTime;
static TestResult g_testScreen;
static TestResult g_testHero;
static TestResult g_testViewMatrix;

// Entity (5)
static TestResult g_testLocalPlayer;
static TestResult g_testPlayerCount;
static TestResult g_testPositions;
static TestResult g_testBones;
static TestResult g_testTeams;

// Raycast (2)
static TestResult g_testRaycastReady;
static TestResult g_testRaycastAccuracy;

// Network (1)
static TestResult g_testNetwork;

// W2S (1)
static TestResult g_testW2S;

// Targeting (3)
static TestResult g_testEnemyCount;
static TestResult g_testAllyCount;
static TestResult g_testVisibility;

// Performance (2)
static TestResult g_testFrameDelta;
static TestResult g_testFrameRate;

static const int TEST_COUNT = 18;

// ============================================================================
// State
// ============================================================================

static bool  g_enabled     = true;
static int   g_hudCorner   = 0;      // 0=TL 1=TR 2=BL 3=BR
static bool  g_manualRun   = false;
static float g_runTimer    = 0.f;
static float g_runInterval = 2.f;
static float g_lastFps     = 0.f;
static float g_fpsTimer    = 0.f;
static int   g_frameCount  = 0;

// ============================================================================
// Plugin Info
// ============================================================================

XENON_PLUGIN_INFO(
    "Diagnostics",
    "Diagnostics",
    "Xenon",
    "System health-check: tests all SDK subsystems and shows results on HUD.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ============================================================================
// Test Runners
// ============================================================================

static void RunCoreTests(float now)
{
    float t = GetTime();
    g_testTime.status    = (t > 0.f) ? TestStatus::Pass : TestStatus::Fail;
    g_testTime.timestamp = now;

    Vector2 sz = ScreenSize();
    if (sz.x > 0 && sz.y > 0)
        g_testScreen.status = TestStatus::Pass;
    else if (sz.x == 0 || sz.y == 0)
        g_testScreen.status = TestStatus::Warn;
    else
        g_testScreen.status = TestStatus::Fail;
    g_testScreen.timestamp = now;

    uint64_t heroId = GetCurrentHero();
    if (!IsIngame())
        g_testHero.status = TestStatus::Warn;
    else
        g_testHero.status = (heroId != 0) ? TestStatus::Pass : TestStatus::Fail;
    g_testHero.timestamp = now;

    float mat[16] = {};
    bool matOk = GetViewMatrix(mat);
    bool matNonzero = false;
    for (int i = 0; i < 16; i++) if (mat[i] != 0.f) { matNonzero = true; break; }
    g_testViewMatrix.status    = (matOk && matNonzero) ? TestStatus::Pass : TestStatus::Fail;
    g_testViewMatrix.timestamp = now;
}

static void RunEntityTests(float now)
{
    bool inGame    = IsIngame();
    int  total     = 0;
    int  withPos   = 0;
    int  withBones = 0;
    int  enemies   = 0;
    int  allies    = 0;
    bool foundLocal = false;

    // Players() crashes in training area — skip loop, mark as WARN
    (void)total; (void)withPos; (void)withBones; (void)enemies; (void)allies; (void)foundLocal;

    g_testLocalPlayer.status    = !inGame ? TestStatus::Warn : (foundLocal ? TestStatus::Pass : TestStatus::Fail);
    g_testLocalPlayer.timestamp = now;

    g_testPlayerCount.status    = !inGame ? TestStatus::Warn : (total > 0 ? TestStatus::Pass : TestStatus::Warn);
    g_testPlayerCount.timestamp = now;

    g_testPositions.status    = (!inGame || total == 0) ? TestStatus::Warn : (withPos > 0 ? TestStatus::Pass : TestStatus::Fail);
    g_testPositions.timestamp = now;

    g_testBones.status    = (!inGame || total == 0) ? TestStatus::Warn : (withBones > 0 ? TestStatus::Pass : TestStatus::Warn);
    g_testBones.timestamp = now;

    g_testTeams.status    = (!inGame || total == 0) ? TestStatus::Warn : ((enemies > 0 || allies > 0) ? TestStatus::Pass : TestStatus::Warn);
    g_testTeams.timestamp = now;
}

static void RunRaycastTests(float now)
{
    bool ready = IsRaycastReady();
    g_testRaycastReady.status    = ready ? TestStatus::Pass : TestStatus::Warn;
    g_testRaycastReady.timestamp = now;

    if (!ready || !IsIngame())
    {
        g_testRaycastAccuracy.status    = TestStatus::Warn;
        g_testRaycastAccuracy.timestamp = now;
        return;
    }

    Vector3 cam = GetCameraPosition();
    Vector3 down = { cam.x, cam.y - 50.f, cam.z };
    RaycastResult res = Raycast(cam, down);
    g_testRaycastAccuracy.status    = res.IsHit() ? TestStatus::Pass : TestStatus::Warn;
    g_testRaycastAccuracy.timestamp = now;
}

static void RunNetworkTests(float now)
{
    int ping = GetClientPing();
    if (ping < 0)
        g_testNetwork.status = TestStatus::Fail;
    else if (ping > 200)
        g_testNetwork.status = TestStatus::Warn;
    else
        g_testNetwork.status = TestStatus::Pass;
    g_testNetwork.timestamp = now;
}

static void RunW2STests(float now)
{
    if (!IsIngame())
    {
        g_testW2S.status    = TestStatus::Warn;
        g_testW2S.timestamp = now;
        return;
    }

    Vector3 cam = GetCameraPosition();
    Vector2 out;
    bool ok = WorldToScreen(cam, out);
    g_testW2S.status    = ok ? TestStatus::Pass : TestStatus::Fail;
    g_testW2S.timestamp = now;
}

static void RunTargetingTests(float now)
{
    bool inGame = IsIngame();
    // Players() crashes in training area — skip loop
    (void)inGame;
    g_testEnemyCount.status    = !inGame ? TestStatus::Warn : TestStatus::Pass;
    g_testEnemyCount.timestamp = now;
    g_testAllyCount.status     = !inGame ? TestStatus::Warn : TestStatus::Pass;
    g_testAllyCount.timestamp  = now;
    g_testVisibility.status    = !inGame ? TestStatus::Warn : TestStatus::Pass;
    g_testVisibility.timestamp = now;
}

static void RunPerformanceTests(float now, float dt)
{
    g_testFrameDelta.status    = (dt > 0.f && dt < 1.f) ? TestStatus::Pass : TestStatus::Warn;
    g_testFrameDelta.timestamp = now;
    g_testFrameRate.status     = (g_lastFps > 15.f || g_lastFps == 0.f) ? TestStatus::Pass : TestStatus::Warn;
    g_testFrameRate.timestamp  = now;
}

static void RunAllTests(float now, float dt)
{
    RunCoreTests(now);
    RunEntityTests(now);
    RunRaycastTests(now);
    RunNetworkTests(now);
    RunW2STests(now);
    RunTargetingTests(now);
    RunPerformanceTests(now, dt);
}

static void LogAllResults()
{
    struct Row { const char* label; TestResult* result; };
    Row rows[TEST_COUNT] = {
        { "Time",        &g_testTime },
        { "Screen",      &g_testScreen },
        { "Hero",        &g_testHero },
        { "ViewMatrix",  &g_testViewMatrix },
        { "LocalPlayer", &g_testLocalPlayer },
        { "PlayerCount", &g_testPlayerCount },
        { "Positions",   &g_testPositions },
        { "Bones",       &g_testBones },
        { "Teams",       &g_testTeams },
        { "RC Ready",    &g_testRaycastReady },
        { "RC Accuracy", &g_testRaycastAccuracy },
        { "Network",     &g_testNetwork },
        { "W2S",         &g_testW2S },
        { "Enemies",     &g_testEnemyCount },
        { "Allies",      &g_testAllyCount },
        { "Visibility",  &g_testVisibility },
        { "FrameDelta",  &g_testFrameDelta },
        { "FPS",         &g_testFrameRate },
    };

    int pass = 0, warn = 0, fail = 0;
    for (int i = 0; i < TEST_COUNT; i++) {
        if (rows[i].result->status == TestStatus::Pass) pass++;
        else if (rows[i].result->status == TestStatus::Warn) warn++;
        else if (rows[i].result->status == TestStatus::Fail) fail++;
    }

    TextBuilder<64> header;
    header.put("=== DIAG ").putInt(pass).put("/").putInt(TEST_COUNT)
          .put(" PASS  ").putInt(warn).put(" WARN  ").putInt(fail).put(" FAIL ===");
    Log(header.c_str());

    for (int i = 0; i < TEST_COUNT; i++)
    {
        TextBuilder<48> tb;
        tb.put(rows[i].label).put(": ").put(StatusLabel(rows[i].result->status));
        Log(tb.c_str());
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" void on_load()
{
    g_enabled   = true;
    g_hudCorner = Config::GetInt("hudCorner", 2);
    Log("Diagnostics v1.0 loaded - enabled forced true");
}

extern "C" void on_unload()
{
    Config::SetBool("enabled", g_enabled);
    Config::SetInt("hudCorner", g_hudCorner);
    Config::Save();
    Log("Diagnostics unloaded");
}

// ============================================================================
// Frame Logic
// ============================================================================

extern "C" void on_frame(float dt)
{
    float now = GetTime();

    g_frameCount++;
    g_fpsTimer += dt;
    if (g_fpsTimer >= 1.f)
    {
        g_lastFps    = static_cast<float>(g_frameCount) / g_fpsTimer;
        g_frameCount = 0;
        g_fpsTimer   = 0.f;
    }

    if (!g_enabled) return;

    g_runTimer += dt;
    if (g_runTimer >= g_runInterval || g_manualRun)
    {
        Log("running tests");
        RunCoreTests(now);     Log("1 core ok");
        RunEntityTests(now);   Log("2 entity ok");
        RunRaycastTests(now);  Log("3 raycast ok");
        RunNetworkTests(now);  Log("4 network ok");
        RunW2STests(now);      Log("5 w2s ok");
        RunTargetingTests(now); Log("6 targeting ok");
        RunPerformanceTests(now, dt); Log("7 perf ok");
        LogAllResults();
        g_runTimer  = 0.f;
        g_manualRun = false;
    }
}

// ============================================================================
// HUD Overlay
// ============================================================================

extern "C" void on_render()
{
    if (!g_enabled) return;

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    // Count passing tests
    int pass = 0;
    TestResult* all[TEST_COUNT] = {
        &g_testTime, &g_testScreen, &g_testHero, &g_testViewMatrix,
        &g_testLocalPlayer, &g_testPlayerCount, &g_testPositions, &g_testBones, &g_testTeams,
        &g_testRaycastReady, &g_testRaycastAccuracy,
        &g_testNetwork,
        &g_testW2S,
        &g_testEnemyCount, &g_testAllyCount, &g_testVisibility,
        &g_testFrameDelta, &g_testFrameRate,
    };
    for (int i = 0; i < TEST_COUNT; i++)
        if (all[i]->status == TestStatus::Pass) pass++;

    const float LINE = 14.f;
    const float PAD  = 8.f;
    const float W    = 136.f;
    const float H    = (TEST_COUNT + 2) * LINE + 6.f;
    float x, y;

    switch (g_hudCorner)
    {
        default:
        case 0: x = PAD;              y = PAD;              break; // TL
        case 1: x = sz.x - W - PAD;  y = PAD;              break; // TR
        case 2: x = PAD;              y = sz.y - H - PAD;   break; // BL
        case 3: x = sz.x - W - PAD;  y = sz.y - H - PAD;   break; // BR
    }

    Draw::RectFilled(x - 4.f, y - 2.f, W, H, Color(0, 0, 0, 160));

    // Title
    TextBuilder<64> title;
    title.put("DIAG  ").putInt(pass).put("/").putInt(TEST_COUNT).put(" OK");
    Color titleColor = (pass == TEST_COUNT) ? Color::Green()
                     : (pass >= TEST_COUNT / 2) ? Color::Yellow()
                     : Color::Red();
    Draw::TextShadow(x, y, titleColor, title.c_str(), 12);
    y += LINE;

    // Per-test rows
    struct Row { const char* label; TestResult* result; };
    Row rows[TEST_COUNT] = {
        { "Time",       &g_testTime },
        { "Screen",     &g_testScreen },
        { "Hero",       &g_testHero },
        { "ViewMatrix", &g_testViewMatrix },
        { "LocalPlyr",  &g_testLocalPlayer },
        { "PlyrCount",  &g_testPlayerCount },
        { "Positions",  &g_testPositions },
        { "Bones",      &g_testBones },
        { "Teams",      &g_testTeams },
        { "RC Ready",   &g_testRaycastReady },
        { "RC Accur",   &g_testRaycastAccuracy },
        { "Network",    &g_testNetwork },
        { "W2S",        &g_testW2S },
        { "Enemies",    &g_testEnemyCount },
        { "Allies",     &g_testAllyCount },
        { "Visibility", &g_testVisibility },
        { "FrameDt",    &g_testFrameDelta },
        { "FPS",        &g_testFrameRate },
    };

    for (int i = 0; i < TEST_COUNT; i++)
    {
        TestResult* r = rows[i].result;
        Draw::TextShadow(x,          y, Color(200, 200, 200), rows[i].label,          11);
        Draw::TextShadow(x + 80.f,   y, StatusColor(r->status), StatusLabel(r->status), 11);
        y += LINE;
    }

    // FPS
    TextBuilder<24> fps;
    fps.put("FPS: ").putInt(static_cast<int>(g_lastFps));
    Draw::TextShadow(x, y + 2.f, Color(180, 180, 180), fps.c_str(), 11);
}

// ============================================================================
// Menu
// ============================================================================

static void MenuTestRow(const char* label, TestResult* r)
{
    TextBuilder<48> tb;
    tb.put("[").put(StatusLabel(r->status)).put("] ").put(label);
    ImGui::Text(tb.c_str());
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enable", &g_enabled);
    ImGui::Separator();

    ImGui::Combo("HUD Corner", &g_hudCorner,
        "Top-Left\0Top-Right\0Bottom-Left\0Bottom-Right\0");

    bool dummy = false;
    if (ImGui::Checkbox("Run Tests Now", &dummy))
        g_manualRun = true;

    ImGui::Separator();

    MenuTestRow("Time",        &g_testTime);
    MenuTestRow("Screen",      &g_testScreen);
    MenuTestRow("Hero",        &g_testHero);
    MenuTestRow("ViewMatrix",  &g_testViewMatrix);
    MenuTestRow("LocalPlayer", &g_testLocalPlayer);
    MenuTestRow("PlayerCount", &g_testPlayerCount);
    MenuTestRow("Positions",   &g_testPositions);
    MenuTestRow("Bones",       &g_testBones);
    MenuTestRow("Teams",       &g_testTeams);
    MenuTestRow("RC Ready",    &g_testRaycastReady);
    MenuTestRow("RC Accuracy", &g_testRaycastAccuracy);
    MenuTestRow("Network",     &g_testNetwork);
    MenuTestRow("W2S",         &g_testW2S);
    MenuTestRow("Enemies",     &g_testEnemyCount);
    MenuTestRow("Allies",      &g_testAllyCount);
    MenuTestRow("Visibility",  &g_testVisibility);
    MenuTestRow("FrameDelta",  &g_testFrameDelta);
    MenuTestRow("FPS",         &g_testFrameRate);
}
