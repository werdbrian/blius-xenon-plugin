// Keymap Scanner
// Shows which action array indices are DOWN when you press buttons.
// Press each key/button and watch which index lights up.

#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "keymap",
    "Keymap Scanner",
    "Xenon",
    "Displays OW action array indices — press a key to see its index.",
    "1.0",
    0,
    PluginFlags::HasOverlay
)

extern "C" void on_load()   { Log("keymap loaded"); }
extern "C" void on_unload() {}
extern "C" void on_frame(float dt) { (void)dt; }

extern "C" void on_render()
{
    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    const float X    = 10.f;
    const float LINE = 14.f;
    const int   COLS = 4;
    const int   SCAN = 32;
    const float COL_W = 90.f;

    float totalH = (SCAN / COLS + 1) * LINE + 20.f;
    Draw::RectFilled(X - 4.f, 10.f, COL_W * COLS + 8.f, totalH, Color(0, 0, 0, 180));

    Draw::TextShadow(X, 12.f, Color::Cyan(), "OW ACTION ARRAY  (press a key)", 12);

    // Print active indices on one line
    TextBuilder<128> active;
    active.put("DOWN: ");
    bool any = false;
    for (int i = 0; i < SCAN; i++)
    {
        if (IsKeyDown(i))
        {
            if (any) active.put(", ");
            active.putInt(i);
            any = true;
        }
    }
    if (!any) active.put("none");
    Draw::TextShadow(X, 12.f + LINE, Color::Yellow(), active.c_str(), 12);

    for (int i = 0; i < SCAN; i++)
    {
        bool down = IsKeyDown(i);
        int col = i % COLS;
        int row = i / COLS;
        float x = X + col * COL_W;
        float y = 12.f + LINE*2 + row * LINE;

        TextBuilder<24> tb;
        tb.put("[").putInt(i).put("] ").put(down ? "DOWN" : "----");
        Draw::Text(x, y, down ? Color::Green() : Color(120, 120, 120), tb.c_str(), 11);
    }
}
