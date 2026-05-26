#pragma once
#include "Types.hpp"
#include "String.hpp"

// ============================================================================
// WASM Imports - Draw Functions
// ============================================================================

extern "C"
{
    __attribute__((import_module("core"), import_name("draw_line")))
    void xn_draw_line(float x1, float y1, float x2, float y2,
                      float r, float g, float b, float a, float thickness);

    __attribute__((import_module("core"), import_name("draw_circle")))
    void xn_draw_circle(float x, float y, float radius,
                        float r, float g, float b, float a, float thickness);

    __attribute__((import_module("core"), import_name("draw_rect")))
    void xn_draw_rect(float x1, float y1, float x2, float y2,
                      float r, float g, float b, float a, float thickness);

    __attribute__((import_module("core"), import_name("draw_text")))
    void xn_draw_text(float x, float y, const char* text,
                      float r, float g, float b, float a, int32_t size);
}

// ============================================================================
// C++ Draw Namespace
// ============================================================================

namespace xenon
{
    namespace Draw
    {
        // Line
        inline void Line(float x1, float y1, float x2, float y2, Color color, float thickness = 1.f)
        {
            xn_draw_line(x1, y1, x2, y2,
                         color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                         thickness);
        }

        inline void Line(const Vector2& start, const Vector2& end, Color color, float thickness = 1.f)
        {
            Line(start.x, start.y, end.x, end.y, color, thickness);
        }

        // Circle
        inline void Circle(float x, float y, float radius, Color color, float thickness = 1.f)
        {
            xn_draw_circle(x, y, radius,
                           color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                           thickness);
        }

        inline void Circle(const Vector2& center, float radius, Color color, float thickness = 1.f)
        {
            Circle(center.x, center.y, radius, color, thickness);
        }

        inline void CircleFilled(float x, float y, float radius, Color color)
        {
            // Use negative thickness to indicate filled
            xn_draw_circle(x, y, radius,
                           color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                           0.f);
        }

        inline void CircleFilled(const Vector2& center, float radius, Color color)
        {
            CircleFilled(center.x, center.y, radius, color);
        }

        // Rectangle (min/max corners for Xenon API)
        inline void Rect(float x, float y, float w, float h, Color color, float thickness = 1.f)
        {
            xn_draw_rect(x, y, x + w, y + h,
                         color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                         thickness);
        }

        inline void Rect(const Vector2& pos, const Vector2& size, Color color, float thickness = 1.f)
        {
            Rect(pos.x, pos.y, size.x, size.y, color, thickness);
        }

        inline void RectFilled(float x, float y, float w, float h, Color color)
        {
            xn_draw_rect(x, y, x + w, y + h,
                         color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                         0.f);
        }

        inline void RectFilled(const Vector2& pos, const Vector2& size, Color color)
        {
            RectFilled(pos.x, pos.y, size.x, size.y, color);
        }

        // Text
        // size: font size in pixels (0 = default ~13px)
        inline void Text(float x, float y, Color color, const char* text, int size = 0)
        {
            xn_draw_text(x, y, text,
                         color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f,
                         size);
        }

        inline void Text(const Vector2& pos, Color color, const char* text, int size = 0)
        {
            Text(pos.x, pos.y, color, text, size);
        }

        // ============================================================================
        // Higher-level drawing helpers
        // ============================================================================

        inline void HealthBar(float x, float y, float width, float height, float healthPercent,
                              Color bgColor = Color(50, 50, 50),
                              Color healthColor = Color::Green(),
                              Color borderColor = Color::White())
        {
            RectFilled(x, y, width, height, bgColor);

            float fillWidth = width * (healthPercent / 100.f);
            if (fillWidth > 0)
            {
                uint8_t r = static_cast<uint8_t>((100.f - healthPercent) * 2.55f);
                uint8_t g = static_cast<uint8_t>(healthPercent * 2.55f);
                RectFilled(x, y, fillWidth, height, Color(r, g, 0));
            }

            Rect(x, y, width, height, borderColor, 1.f);
        }

        // Text with 1px shadow for readability
        inline void TextShadow(float x, float y, Color color, const char* text, int size = 0, Color shadowColor = Color::Black())
        {
            if (!text) return;
            Text(x + 1.f, y + 1.f, shadowColor.WithAlpha(color.A()), text, size);
            Text(x, y, color, text, size);
        }

        inline void TextShadow(const Vector2& pos, Color color, const char* text, int size = 0, Color shadowColor = Color::Black())
        {
            TextShadow(pos.x, pos.y, color, text, size, shadowColor);
        }

        // Roughly centered text with shadow (centers via strlen*3 offset)
        inline void TextCentered(float x, float y, Color color, const char* text, int size = 0, Color shadowColor = Color::Black())
        {
            if (!text) return;
            float offset = strlen(text) * 3.f;
            TextShadow(x - offset, y, color, text, size, shadowColor);
        }

        inline void TextCentered(const Vector2& pos, Color color, const char* text, int size = 0, Color shadowColor = Color::Black())
        {
            TextCentered(pos.x, pos.y, color, text, size, shadowColor);
        }

        // Corner bracket rectangle (25% corner marks)
        inline void RectCorners(float x, float y, float w, float h, Color color, float thickness = 1.5f)
        {
            float cornerW = w * 0.25f;
            float cornerH = h * 0.25f;
            float x2 = x + w;
            float y2 = y + h;

            Line(x, y, x + cornerW, y, color, thickness); Line(x, y, x, y + cornerH, color, thickness);
            Line(x2, y, x2 - cornerW, y, color, thickness); Line(x2, y, x2, y + cornerH, color, thickness);
            Line(x, y2, x + cornerW, y2, color, thickness); Line(x, y2, x, y2 - cornerH, color, thickness);
            Line(x2, y2, x2 - cornerW, y2, color, thickness); Line(x2, y2, x2, y2 - cornerH, color, thickness);
        }

        inline void RectCorners(const Vector2& pos, const Vector2& size, Color color, float thickness = 1.5f)
        {
            RectCorners(pos.x, pos.y, size.x, size.y, color, thickness);
        }
    }
}
