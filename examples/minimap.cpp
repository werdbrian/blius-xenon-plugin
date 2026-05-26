// minimap.cpp
// 2D overhead wall map built from raycasts. Settled buckets are never recast.
// Geometry saved per map ID and reloaded on next session.

#include <xenon/SDK.hpp>
using namespace xenon;

static const int   NUM_ANGLES     = 180;   // 2° resolution
static const float RAY_LEN        = 60.f;
static const float MOVE_THRESH    = 5.f;   // meters before new cast origin
static const int   RAYS_PER_FRAME = 8;
static const int   MAX_ORIGINS    = 64;
static const int   MAX_HITS       = 1024;
static const float MAP_SIZE       = 220.f; // panel pixels

struct Hit2D { float x, z; };

struct CastOrigin {
    Vector3 pos;
    bool    settled[NUM_ANGLES];
    bool    done;
};

static Hit2D      g_hits[MAX_HITS];
static int        g_hitCount    = 0;
static CastOrigin g_origins[MAX_ORIGINS];
static int        g_originCount = 0;
static uint32_t   g_mapId       = 0;
static bool       g_loaded      = false;
static bool       g_enabled     = true;
static bool       g_showMap     = true;
static float      g_mapScale    = 2.f;   // pixels per meter

XENON_PLUGIN_INFO(
    "minimap",
    "Minimap",
    "Xenon",
    "Overhead wall map from raycasts, saved per map ID.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

static void LoadMapData()
{
    g_hitCount    = 0;
    g_originCount = 0;
    if (g_mapId == 0) return;
    TextBuilder<32> key;
    key.put("m").putInt((int32_t)g_mapId).put("_n");
    int n = Config::GetInt(key.c_str(), 0);
    if (n > MAX_HITS) n = MAX_HITS;
    for (int i = 0; i < n; i++)
    {
        key.clear(); key.put("m").putInt((int32_t)g_mapId).put("_").putInt(i).put("x");
        float x = Config::GetFloat(key.c_str(), 0.f);
        key.clear(); key.put("m").putInt((int32_t)g_mapId).put("_").putInt(i).put("z");
        float z = Config::GetFloat(key.c_str(), 0.f);
        g_hits[g_hitCount++] = { x, z };
    }
}

static void SaveMapData()
{
    if (g_mapId == 0) return;
    TextBuilder<32> key;
    key.put("m").putInt((int32_t)g_mapId).put("_n");
    Config::SetInt(key.c_str(), g_hitCount);
    for (int i = 0; i < g_hitCount; i++)
    {
        key.clear(); key.put("m").putInt((int32_t)g_mapId).put("_").putInt(i).put("x");
        Config::SetFloat(key.c_str(), g_hits[i].x);
        key.clear(); key.put("m").putInt((int32_t)g_mapId).put("_").putInt(i).put("z");
        Config::SetFloat(key.c_str(), g_hits[i].z);
    }
}

extern "C" void on_load()
{
    g_enabled  = Config::GetBool("enabled",   true);
    g_showMap  = Config::GetBool("showMap",   true);
    g_mapScale = Config::GetFloat("mapScale", 2.f);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",   g_enabled);
    Config::SetBool("showMap",   g_showMap);
    Config::SetFloat("mapScale", g_mapScale);
    SaveMapData();
    Config::Save();
}

extern "C" void on_frame(float dt)
{
    (void)dt;
    if (!g_enabled || !IsIngame()) return;

    // Detect map change and reload stored geometry
    uint32_t curMap = GetMapId();
    if (!g_loaded || curMap != g_mapId)
    {
        if (g_loaded && g_mapId != 0) { SaveMapData(); Config::Save(); }
        g_mapId  = curMap;
        g_loaded = true;
        LoadMapData();
    }

    if (!IsRaycastReady()) return;

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;
    Vector3 lpos = local.GetPosition();

    // Add new cast origin if player moved far enough from the last one
    bool needOrigin = (g_originCount == 0);
    if (!needOrigin && g_originCount < MAX_ORIGINS)
    {
        CastOrigin& last = g_origins[g_originCount - 1];
        float dx = lpos.x - last.pos.x;
        float dz = lpos.z - last.pos.z;
        needOrigin = (dx*dx + dz*dz > MOVE_THRESH * MOVE_THRESH);
    }
    if (needOrigin && g_originCount < MAX_ORIGINS)
    {
        CastOrigin& o = g_origins[g_originCount++];
        o.pos  = lpos;
        o.done = false;
        for (int i = 0; i < NUM_ANGLES; i++) o.settled[i] = false;
    }

    // Cast up to RAYS_PER_FRAME rays across all unsettled origins
    int rays = 0;
    for (int oi = 0; oi < g_originCount && rays < RAYS_PER_FRAME; oi++)
    {
        CastOrigin& o = g_origins[oi];
        if (o.done) continue;

        bool allSettled = true;
        for (int ai = 0; ai < NUM_ANGLES; ai++)
            if (!o.settled[ai]) { allSettled = false; break; }
        if (allSettled) { o.done = true; continue; }

        for (int ai = 0; ai < NUM_ANGLES && rays < RAYS_PER_FRAME; ai++)
        {
            if (o.settled[ai]) continue;

            float angle = (float)ai * 2.f * 3.14159265f / 180.f;
            Vector3 to  = { o.pos.x + cosf(angle) * RAY_LEN,
                            o.pos.y,
                            o.pos.z + sinf(angle) * RAY_LEN };

            RaycastResult r = Raycast(o.pos, to);
            o.settled[ai] = true;
            rays++;

            if (r.IsHit() && g_hitCount < MAX_HITS)
                g_hits[g_hitCount++] = { r.hitPos.x, r.hitPos.z };
        }
    }
}

extern "C" void on_render()
{
    if (!g_enabled || !g_showMap) return;

    Vector2 sz = ScreenSize();
    if (sz.x <= 0.f || sz.y <= 0.f) return;

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;
    Vector3 lpos = local.GetPosition();

    // Panel: top-right corner
    float px = sz.x - MAP_SIZE - 10.f;
    float py = 10.f;
    float cx = px + MAP_SIZE * 0.5f;
    float cy = py + MAP_SIZE * 0.5f;

    Draw::RectFilled(px, py, MAP_SIZE, MAP_SIZE, Color(0, 0, 0, 160));
    Draw::Rect(px, py, MAP_SIZE, MAP_SIZE, Color(80, 80, 80, 200));

    // Wall hits
    for (int i = 0; i < g_hitCount; i++)
    {
        float sx = cx + (g_hits[i].x - lpos.x) * g_mapScale;
        float sy = cy + (g_hits[i].z - lpos.z) * g_mapScale;
        if (sx < px || sx > px + MAP_SIZE || sy < py || sy > py + MAP_SIZE) continue;
        Draw::CircleFilled(sx, sy, 1.5f, Color(180, 180, 180, 220));
    }

    // Players
    for (Entity p : Players())
    {
        if (!p.IsAlive()) continue;
        Vector3 ppos = p.GetPosition();
        float sx = cx + (ppos.x - lpos.x) * g_mapScale;
        float sy = cy + (ppos.z - lpos.z) * g_mapScale;
        if (sx < px || sx > px + MAP_SIZE || sy < py || sy > py + MAP_SIZE) continue;
        Color  c = p.IsLocal() ? Color::Green() : (p.IsEnemy() ? Color::Red() : Color(0, 200, 255));
        float  r = p.IsLocal() ? 4.f : 3.f;
        Draw::CircleFilled(sx, sy, r, c);
    }

    // Scan progress
    int pending = 0;
    for (int oi = 0; oi < g_originCount; oi++)
        if (!g_origins[oi].done) pending++;

    TextBuilder<48> tb;
    if (pending > 0)
    {
        tb.put("scanning...").putInt(pending).put(" left");
        Draw::TextShadow(px + 4.f, py + 4.f, Color(255, 200, 60), tb.c_str(), 10);
    }

    tb.clear();
    tb.put("hits:").putInt(g_hitCount);
    Draw::TextShadow(px + 4.f, py + MAP_SIZE - 14.f, Color(120, 120, 120), tb.c_str(), 10);
}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Minimap"))
    {
        ImGui::Checkbox("Enabled",  &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Show Map", &g_showMap);
        ImGui::SliderFloat("Scale (px/m)", &g_mapScale, 0.5f, 8.f);
        ImGui::Separator();
        TextBuilder<48> tb;
        tb.put("Hits: ").putInt(g_hitCount).put(" / ").putInt(MAX_HITS);
        ImGui::Text(tb.c_str());
        tb.clear(); tb.put("Origins: ").putInt(g_originCount).put(" / ").putInt(MAX_ORIGINS);
        ImGui::Text(tb.c_str());
        tb.clear(); tb.put("Map ID: ").putInt((int32_t)g_mapId);
        ImGui::Text(tb.c_str());
    }
}
