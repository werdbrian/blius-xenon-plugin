#pragma once

// Freestanding type definitions for WASM
using int8_t = signed char;
using uint8_t = unsigned char;
using int16_t = short;
using uint16_t = unsigned short;
using int32_t = int;
using uint32_t = unsigned int;
using int64_t = long long;
using uint64_t = unsigned long long;
using size_t = decltype(sizeof(0));

// Math intrinsics (imported from "core" module)
extern "C" {
    __attribute__((import_module("core"), import_name("math_sqrt")))
    double __xn_sqrt(double x);
    __attribute__((import_module("core"), import_name("math_sin")))
    double __xn_sin(double x);
    __attribute__((import_module("core"), import_name("math_cos")))
    double __xn_cos(double x);
    __attribute__((import_module("core"), import_name("math_fabs")))
    double __xn_fabs(double x);
    __attribute__((import_module("core"), import_name("math_atan2")))
    double __xn_atan2(double y, double x);
}
inline float sqrtf(float x) { return static_cast<float>(__xn_sqrt(static_cast<double>(x))); }
inline float sinf(float x) { return static_cast<float>(__xn_sin(static_cast<double>(x))); }
inline float cosf(float x) { return static_cast<float>(__xn_cos(static_cast<double>(x))); }
inline float fabsf(float x) { return static_cast<float>(__xn_fabs(static_cast<double>(x))); }
inline float atan2f(float y, float x) { return static_cast<float>(__xn_atan2(static_cast<double>(y), static_cast<double>(x))); }

namespace xenon
{
    // 2D Vector
    struct Vector2
    {
        float x = 0.f;
        float y = 0.f;

        Vector2() = default;
        Vector2(float x_, float y_) : x(x_), y(y_) {}

        Vector2 operator+(const Vector2& other) const { return {x + other.x, y + other.y}; }
        Vector2 operator-(const Vector2& other) const { return {x - other.x, y - other.y}; }
        Vector2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
        Vector2 operator/(float scalar) const { return {x / scalar, y / scalar}; }

        bool IsValid() const { return x != 0.f || y != 0.f; }
        float Length() const { return sqrtf(x * x + y * y); }
        float LengthSquared() const { return x * x + y * y; }
        Vector2 Normalized() const
        {
            float len = Length();
            if (len > 0.f)
                return {x / len, y / len};
            return {0.f, 0.f};
        }
    };

    // 3D Vector
    struct Vector3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        Vector3() = default;
        Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        Vector3 operator+(const Vector3& other) const { return {x + other.x, y + other.y, z + other.z}; }
        Vector3 operator-(const Vector3& other) const { return {x - other.x, y - other.y, z - other.z}; }
        Vector3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
        Vector3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }

        bool IsValid() const { return x != 0.f || y != 0.f || z != 0.f; }
        float Length() const { return sqrtf(x * x + y * y + z * z); }
        float LengthSquared() const { return x * x + y * y + z * z; }
        Vector3 Normalized() const
        {
            float len = Length();
            if (len > 0.f)
                return {x / len, y / len, z / len};
            return {0.f, 0.f, 0.f};
        }
        Vector3 RotatedY(float yaw) const
        {
            float c = cosf(yaw);
            float s = sinf(yaw);
            return Vector3(x * c + z * s, y, -x * s + z * c);
        }
        float Dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
        Vector3 Cross(const Vector3& other) const
        {
            return {
                y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x
            };
        }
    };

    // Color with ARGB format (0xAARRGGBB)
    struct Color
    {
        uint32_t value = 0xFFFFFFFF;

        Color() = default;
        Color(uint32_t argb) : value(argb) {}
        Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
            : value(((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b) {}

        uint8_t A() const { return (value >> 24) & 0xFF; }
        uint8_t R() const { return (value >> 16) & 0xFF; }
        uint8_t G() const { return (value >> 8) & 0xFF; }
        uint8_t B() const { return value & 0xFF; }

        Color WithAlpha(uint8_t a) const { return Color(R(), G(), B(), a); }

        // Interpolation
        static Color Lerp(Color a, Color b, float t)
        {
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            return Color(
                static_cast<uint8_t>(a.R() + (b.R() - a.R()) * t),
                static_cast<uint8_t>(a.G() + (b.G() - a.G()) * t),
                static_cast<uint8_t>(a.B() + (b.B() - a.B()) * t),
                static_cast<uint8_t>(a.A() + (b.A() - a.A()) * t)
            );
        }

        // Health color gradient: red (0%) → yellow (50%) → green (100%)
        static Color HealthGradient(float percent, uint8_t alpha = 255)
        {
            if (percent < 0.f) percent = 0.f;
            if (percent > 1.f) percent = 1.f;
            Color c = (percent > 0.5f)
                ? Lerp(Yellow(), Green(), (percent - 0.5f) * 2.f)
                : Lerp(Red(), Yellow(), percent * 2.f);
            return c.WithAlpha(alpha);
        }

        // Common colors
        static Color Red() { return Color(255, 0, 0); }
        static Color Green() { return Color(0, 255, 0); }
        static Color Blue() { return Color(0, 0, 255); }
        static Color White() { return Color(255, 255, 255); }
        static Color Black() { return Color(0, 0, 0); }
        static Color Yellow() { return Color(255, 255, 0); }
        static Color Cyan() { return Color(0, 255, 255); }
        static Color Magenta() { return Color(255, 0, 255); }
        static Color Orange() { return Color(255, 165, 0); }
        static Color Purple() { return Color(128, 0, 128); }
        static Color Transparent() { return Color(0, 0, 0, 0); }
    };

    // Skill cooldown / duration state
    struct SkillCooldown
    {
        float current;  // Current cooldown/duration time remaining
        float max;      // Total cooldown/duration length
        bool enabled;   // True if actively ticking

        bool IsOnCooldown() const { return enabled && current > 0.f; }
        bool IsActive() const { return enabled && current > 0.f; }
        float GetPercent() const { return max > 0.f ? current / max : 0.f; }
    };

    // Team enum
    enum class Team : int32_t
    {
        Unknown = 0,
        Red = 1,
        Blue = 2,
        Unknown1 = 3,
        Unknown2 = 4,
        Deathmatch = 5
    };

    // Entity type enum (matches host EntityType values)
    enum class EntityType : int32_t
    {
        Hero = 0,
        Turret = 1,
        Throwable = 2,
        Shield = 3,
        Bot = 4,
        Misc = 5,
        Unknown = 6
    };

    // Body slot classification indices (used in Hitbox::bodySlot)
    namespace BodySlot
    {
        constexpr int Head      = 0;
        constexpr int Neck      = 1;
        constexpr int Body      = 2;
        constexpr int BodyBot   = 3;
        constexpr int Pelvis    = 4;
        constexpr int LPelvis   = 5;
        constexpr int RPelvis   = 6;
        constexpr int Chest     = 7;
        constexpr int LShoulder = 8;
        constexpr int RShoulder = 9;
        constexpr int LElbow    = 10;
        constexpr int RElbow    = 11;
        constexpr int LAnkle    = 12;
        constexpr int RAnkle    = 13;
        constexpr int LShank    = 14;
        constexpr int RShank    = 15;
        constexpr int LHand     = 16;
        constexpr int RHand     = 17;
        constexpr int RKnee     = 18;
        constexpr int LKnee     = 19;
        constexpr int RFoot     = 20;
        constexpr int LFoot     = 21;
        constexpr int Count     = 22;
        constexpr int Unclassified = -1;
    }

    // Bone IDs (actual game skeleton indices)
    namespace Bone
    {
        constexpr int Head = 17;
        constexpr int Neck = 16;
        constexpr int Chest = 2;
        constexpr int Body = 81;
        constexpr int BodyBot = 82;
        constexpr int Pelvis = 3;
        constexpr int LPelvis = 85;
        constexpr int RPelvis = 95;
        constexpr int LShoulder = 49;
        constexpr int RShoulder = 54;
        constexpr int LElbow = 14;
        constexpr int RElbow = 51;
        constexpr int LHand = 41;
        constexpr int RHand = 71;
        constexpr int LShank = 87;
        constexpr int RShank = 97;
        constexpr int LAnkle = 86;
        constexpr int RAnkle = 96;
        constexpr int LKnee = 89;
        constexpr int RKnee = 99;
        constexpr int LFoot = 90;
        constexpr int RFoot = 100;

        // Hero-specific overrides
        constexpr int BastionLHand = 45;
        constexpr int BastionRHand = 55;
    }

    // Bone connection pair for skeleton rendering
    struct BonePair { int a; int b; };

    // Virtual key codes (subset)
    namespace VK
    {
        constexpr int LButton = 0x01;
        constexpr int RButton = 0x02;
        constexpr int MButton = 0x04;
        constexpr int XButton1 = 0x05;
        constexpr int XButton2 = 0x06;
        constexpr int Shift = 0x10;
        constexpr int Control = 0x11;
        constexpr int Alt = 0x12;
        constexpr int Space = 0x20;
        constexpr int Insert = 0x2D;
        constexpr int Delete = 0x2E;
        constexpr int A = 0x41;
        constexpr int B = 0x42;
        constexpr int C = 0x43;
        constexpr int D = 0x44;
        constexpr int E = 0x45;
        constexpr int F = 0x46;
        constexpr int G = 0x47;
        constexpr int H = 0x48;
        constexpr int I = 0x49;
        constexpr int J = 0x4A;
        constexpr int K = 0x4B;
        constexpr int L = 0x4C;
        constexpr int M = 0x4D;
        constexpr int N = 0x4E;
        constexpr int O = 0x4F;
        constexpr int P = 0x50;
        constexpr int Q = 0x51;
        constexpr int R = 0x52;
        constexpr int S = 0x53;
        constexpr int T = 0x54;
        constexpr int U = 0x55;
        constexpr int V = 0x56;
        constexpr int W = 0x57;
        constexpr int X = 0x58;
        constexpr int Y = 0x59;
        constexpr int Z = 0x5A;
        constexpr int F1 = 0x70;
        constexpr int F2 = 0x71;
        constexpr int F3 = 0x72;
        constexpr int F4 = 0x73;
        constexpr int F5 = 0x74;
        constexpr int F6 = 0x75;
        constexpr int F7 = 0x76;
        constexpr int F8 = 0x77;
        constexpr int F9 = 0x78;
        constexpr int F10 = 0x79;
        constexpr int F11 = 0x7A;
        constexpr int F12 = 0x7B;
    }

    // Outline types for Entity::SetOutline()
    namespace OutlineType
    {
        constexpr int Visible = 1;   // Outline when entity is in line of sight
        constexpr int Occluded = 2;  // Outline when entity is behind walls (wallhack)
    }

    // Raycast result from world-space line traces
    struct RaycastResult
    {
        Vector3 hitPos;
        float fraction;      // 0.0 = hit at start, 1.0 = no hit
        int32_t hit;         // 1 = hit geometry, 0 = clear

        bool IsHit() const { return hit != 0; }
        bool IsVisible() const { return !IsHit() || fraction > 0.98f; }
    };

    // Game-level button bits for PressGameButton/ReleaseGameButton
    namespace GameButton
    {
        constexpr uint32_t LMouse      = 0x1;
        constexpr uint32_t RMouse      = 0x2;
        constexpr uint32_t ScopedShoot = 0x3;
        constexpr uint32_t Interact    = 0x4;
        constexpr uint32_t Skill1      = 0x8;
        constexpr uint32_t Skill2      = 0x10;
        constexpr uint32_t Ult         = 0x20;
        constexpr uint32_t Jump        = 0x40;
        constexpr uint32_t Crouch      = 0x80;
        constexpr uint32_t Reload      = 0x400;
        constexpr uint32_t Melee       = 0x800;
    }

    // Plugin flags
    namespace PluginFlags
    {
        constexpr uint32_t None = 0;
        constexpr uint32_t HasOverlay = 1 << 0;
        constexpr uint32_t HasMenu = 1 << 1;
        constexpr uint32_t HeroSpecific = 1 << 2;
    }

    // Hero IDs (pool IDs)
    namespace HeroId
    {
        constexpr uint64_t Reaper = 0x02E0000000000002;
        constexpr uint64_t Tracer = 0x02E0000000000003;
        constexpr uint64_t Mercy = 0x02E0000000000004;
        constexpr uint64_t Hanzo = 0x02E0000000000005;
        constexpr uint64_t Torbjorn = 0x02E0000000000006;
        constexpr uint64_t Reinhardt = 0x02E0000000000007;
        constexpr uint64_t Pharah = 0x02E0000000000008;
        constexpr uint64_t Winston = 0x02E0000000000009;
        constexpr uint64_t Widowmaker = 0x02E000000000000A;
        constexpr uint64_t Bastion = 0x02E0000000000015;
        constexpr uint64_t Symmetra = 0x02E0000000000016;
        constexpr uint64_t Zenyatta = 0x02E0000000000020;
        constexpr uint64_t Genji = 0x02E0000000000029;
        constexpr uint64_t Roadhog = 0x02E0000000000040;
        constexpr uint64_t Cassidy = 0x02E0000000000042;
        constexpr uint64_t Junkrat = 0x02E0000000000065;
        constexpr uint64_t Zarya = 0x02E0000000000068;
        constexpr uint64_t Soldier76 = 0x02E000000000006E;
        constexpr uint64_t Lucio = 0x02E0000000000079;
        constexpr uint64_t Dva = 0x02E000000000007A;
        constexpr uint64_t Mei = 0x02E00000000000DD;
        constexpr uint64_t Ana = 0x02E000000000013B;
        constexpr uint64_t Sombra = 0x02E000000000012E;
        constexpr uint64_t Orisa = 0x02E000000000013E;
        constexpr uint64_t Doomfist = 0x02E000000000012F;
        constexpr uint64_t Moira = 0x02E00000000001A2;
        constexpr uint64_t Brigitte = 0x02E0000000000195;
        constexpr uint64_t WreckingBall = 0x02E00000000001CA;
        constexpr uint64_t Ashe = 0x02E0000000000200;
        constexpr uint64_t Baptiste = 0x02E0000000000221;
        constexpr uint64_t Sigma = 0x02E000000000023B;
        constexpr uint64_t Echo = 0x02E0000000000206;
        constexpr uint64_t Kiriko = 0x02E0000000000231;
        constexpr uint64_t Sojourn = 0x02E00000000001EC;
        constexpr uint64_t JunkerQueen = 0x02E0000000000236;
        constexpr uint64_t Ramattra = 0x02E000000000028D;
        constexpr uint64_t Lifeweaver = 0x02E0000000000291;
        constexpr uint64_t Illari = 0x02E000000000031C;
        constexpr uint64_t Mauga = 0x02E000000000030A;
        constexpr uint64_t Venture = 0x02E000000000032B;
        constexpr uint64_t Juno = 0x02E0000000000370;
        constexpr uint64_t Hazard = 0x02E0000000000380;
        constexpr uint64_t Freja = 0x02E0000000000390;
        constexpr uint64_t Wuyang = 0x02E00000000003A0;
        constexpr uint64_t Vendetta = 0x02E00000000003B0;
        constexpr uint64_t Emre = 0x02E00000000003C0;
        constexpr uint64_t Domina = 0x02E00000000003D0;
        constexpr uint64_t Anran = 0x02E00000000003E0;
        constexpr uint64_t Mizuki = 0x02E00000000003F0;
        constexpr uint64_t JetpackCat = 0x02E0000000000400;
        constexpr uint64_t Sierra = 0x02E0000000000410;

        // Training bots
        constexpr uint64_t TrainingBot1 = 0x02E000000000033C;
        constexpr uint64_t TrainingBot2 = 0x02E0000000000337;
        constexpr uint64_t TrainingBot3 = 0x02E000000000035A;
        constexpr uint64_t TrainingBot4 = 0x02E000000000016C;
    }

    // Hero name lookup from HeroId
    inline const char* GetHeroName(uint64_t heroId)
    {
        if (heroId == HeroId::Reaper)       return "Reaper";
        if (heroId == HeroId::Tracer)       return "Tracer";
        if (heroId == HeroId::Mercy)        return "Mercy";
        if (heroId == HeroId::Hanzo)        return "Hanzo";
        if (heroId == HeroId::Torbjorn)     return "Torbjorn";
        if (heroId == HeroId::Reinhardt)    return "Reinhardt";
        if (heroId == HeroId::Pharah)       return "Pharah";
        if (heroId == HeroId::Winston)      return "Winston";
        if (heroId == HeroId::Widowmaker)   return "Widowmaker";
        if (heroId == HeroId::Bastion)      return "Bastion";
        if (heroId == HeroId::Symmetra)     return "Symmetra";
        if (heroId == HeroId::Zenyatta)     return "Zenyatta";
        if (heroId == HeroId::Genji)        return "Genji";
        if (heroId == HeroId::Roadhog)      return "Roadhog";
        if (heroId == HeroId::Cassidy)      return "Cassidy";
        if (heroId == HeroId::Junkrat)      return "Junkrat";
        if (heroId == HeroId::Zarya)        return "Zarya";
        if (heroId == HeroId::Soldier76)    return "Soldier:76";
        if (heroId == HeroId::Lucio)        return "Lucio";
        if (heroId == HeroId::Dva)          return "D.Va";
        if (heroId == HeroId::Mei)          return "Mei";
        if (heroId == HeroId::Ana)          return "Ana";
        if (heroId == HeroId::Sombra)       return "Sombra";
        if (heroId == HeroId::Orisa)        return "Orisa";
        if (heroId == HeroId::Doomfist)     return "Doomfist";
        if (heroId == HeroId::Moira)        return "Moira";
        if (heroId == HeroId::Brigitte)     return "Brigitte";
        if (heroId == HeroId::WreckingBall) return "Wrecking Ball";
        if (heroId == HeroId::Ashe)         return "Ashe";
        if (heroId == HeroId::Baptiste)     return "Baptiste";
        if (heroId == HeroId::Sigma)        return "Sigma";
        if (heroId == HeroId::Echo)         return "Echo";
        if (heroId == HeroId::Kiriko)       return "Kiriko";
        if (heroId == HeroId::Sojourn)      return "Sojourn";
        if (heroId == HeroId::JunkerQueen)  return "Junker Queen";
        if (heroId == HeroId::Ramattra)     return "Ramattra";
        if (heroId == HeroId::Lifeweaver)   return "Lifeweaver";
        if (heroId == HeroId::Illari)       return "Illari";
        if (heroId == HeroId::Mauga)        return "Mauga";
        if (heroId == HeroId::Venture)      return "Venture";
        if (heroId == HeroId::Juno)         return "Juno";
        if (heroId == HeroId::Hazard)       return "Hazard";
        if (heroId == HeroId::Freja)        return "Freja";
        if (heroId == HeroId::Wuyang)       return "Wuyang";
        if (heroId == HeroId::Vendetta)     return "Vendetta";
        if (heroId == HeroId::Emre)         return "Emre";
        if (heroId == HeroId::Domina)       return "Domina";
        if (heroId == HeroId::Anran)        return "Anran";
        if (heroId == HeroId::Mizuki)       return "Mizuki";
        if (heroId == HeroId::JetpackCat)   return "Jetpack Cat";
        if (heroId == HeroId::Sierra)       return "Sierra";
        if (heroId == HeroId::TrainingBot1 || heroId == HeroId::TrainingBot2 ||
            heroId == HeroId::TrainingBot3 || heroId == HeroId::TrainingBot4) return "Training Bot";
        return "Unknown";
    }
}
