#pragma once
#include "Types.hpp"

// ============================================================================
// WASM Imports - Core Functions
// ============================================================================

extern "C"
{
    // Logging
    __attribute__((import_module("core"), import_name("log")))
    void xn_log(const char* msg);

    // Time
    __attribute__((import_module("core"), import_name("get_time")))
    float xn_get_time();

    __attribute__((import_module("core"), import_name("get_delta_time")))
    float xn_get_delta_time();

    // Input
    __attribute__((import_module("core"), import_name("is_key_down")))
    int32_t xn_is_key_down(int32_t vkey);

    // Skeleton
    __attribute__((import_module("core"), import_name("get_skeleton_connections")))
    int32_t xn_get_skeleton_connections(uint64_t heroId, void* outBonePairs);

    // Screen
    __attribute__((import_module("core"), import_name("get_screen_width")))
    int32_t xn_get_screen_width();

    __attribute__((import_module("core"), import_name("get_screen_height")))
    int32_t xn_get_screen_height();

    // World to screen (outPtr is a PluginVector2*)
    __attribute__((import_module("core"), import_name("world_to_screen")))
    int32_t xn_world_to_screen(float wx, float wy, float wz, void* outVec2);

    // World to screen (unclamped — allows off-screen coordinates for bounding box calculations)
    __attribute__((import_module("core"), import_name("world_to_screen_unclamped")))
    int32_t xn_world_to_screen_unclamped(float wx, float wy, float wz, void* outVec2);

    // Camera
    __attribute__((import_module("core"), import_name("get_camera_position")))
    int32_t xn_get_camera_position(void* outVec3);

    __attribute__((import_module("core"), import_name("get_camera_forward")))
    int32_t xn_get_camera_forward(void* outVec3);

    // View matrix (16 floats, row-major 4x4)
    __attribute__((import_module("core"), import_name("get_view_matrix")))
    int32_t xn_get_view_matrix(void* outFloat16);

    // Log levels
    __attribute__((import_module("core"), import_name("log_debug")))
    void xn_log_debug(const char* msg);

    __attribute__((import_module("core"), import_name("log_warning")))
    void xn_log_warning(const char* msg);

    __attribute__((import_module("core"), import_name("log_error")))
    void xn_log_error(const char* msg);

    // Current hero
    __attribute__((import_module("core"), import_name("get_current_hero")))
    uint64_t xn_get_current_hero();

    // Raycast
    __attribute__((import_module("core"), import_name("raycast")))
    int32_t xn_raycast(float fx, float fy, float fz, float tx, float ty, float tz, void* out);

    __attribute__((import_module("core"), import_name("is_point_visible")))
    int32_t xn_is_point_visible(float fx, float fy, float fz, float tx, float ty, float tz);

    __attribute__((import_module("core"), import_name("is_raycast_ready")))
    int32_t xn_is_raycast_ready();

    // Game state
    __attribute__((import_module("core"), import_name("is_ingame")))
    int32_t xn_is_ingame();

    __attribute__((import_module("core"), import_name("get_map_id")))
    int32_t xn_get_map_id();

    __attribute__((import_module("core"), import_name("get_match_type")))
    int32_t xn_get_match_type();

    __attribute__((import_module("core"), import_name("get_sensitivity")))
    float xn_get_sensitivity();

    // Connection / Input
    __attribute__((import_module("core"), import_name("get_client_ping")))
    int32_t xn_get_client_ping();

    __attribute__((import_module("core"), import_name("get_server_ping")))
    int32_t xn_get_server_ping();

    __attribute__((import_module("core"), import_name("press_game_button")))
    void xn_press_game_button(int32_t bit);

    __attribute__((import_module("core"), import_name("release_game_button")))
    void xn_release_game_button(int32_t bit);

    // Ability / StateScript queries
    __attribute__((import_module("core"), import_name("get_ult_charge")))
    float xn_get_ult_charge();

    __attribute__((import_module("core"), import_name("is_ult_ready")))
    int32_t xn_is_ult_ready();

    __attribute__((import_module("core"), import_name("is_ult_active")))
    int32_t xn_is_ult_active();

    __attribute__((import_module("core"), import_name("is_skill1_active")))
    float xn_is_skill1_active();

    __attribute__((import_module("core"), import_name("is_skill2_active")))
    float xn_is_skill2_active();

    __attribute__((import_module("core"), import_name("is_skill3_active")))
    float xn_is_skill3_active();

    __attribute__((import_module("core"), import_name("get_hero_state")))
    int32_t xn_get_hero_state();

    // Skill cooldown accessors (local player only)
    __attribute__((import_module("core"), import_name("skill1_cd_current")))
    float xn_skill1_cd_current();
    __attribute__((import_module("core"), import_name("skill1_cd_max")))
    float xn_skill1_cd_max();
    __attribute__((import_module("core"), import_name("skill1_cd_enabled")))
    float xn_skill1_cd_enabled();

    __attribute__((import_module("core"), import_name("skill2_cd_current")))
    float xn_skill2_cd_current();
    __attribute__((import_module("core"), import_name("skill2_cd_max")))
    float xn_skill2_cd_max();
    __attribute__((import_module("core"), import_name("skill2_cd_enabled")))
    float xn_skill2_cd_enabled();

    __attribute__((import_module("core"), import_name("skill3_cd_current")))
    float xn_skill3_cd_current();
    __attribute__((import_module("core"), import_name("skill3_cd_max")))
    float xn_skill3_cd_max();
    __attribute__((import_module("core"), import_name("skill3_cd_enabled")))
    float xn_skill3_cd_enabled();

    __attribute__((import_module("core"), import_name("ult_cd_current")))
    float xn_ult_cd_current();
    __attribute__((import_module("core"), import_name("ult_cd_max")))
    float xn_ult_cd_max();
    __attribute__((import_module("core"), import_name("ult_cd_enabled")))
    float xn_ult_cd_enabled();

    // Ability duration accessors (local player only)
    __attribute__((import_module("core"), import_name("skill1_dur_current")))
    float xn_skill1_dur_current();
    __attribute__((import_module("core"), import_name("skill1_dur_max")))
    float xn_skill1_dur_max();
    __attribute__((import_module("core"), import_name("skill1_dur_enabled")))
    float xn_skill1_dur_enabled();

    __attribute__((import_module("core"), import_name("skill2_dur_current")))
    float xn_skill2_dur_current();
    __attribute__((import_module("core"), import_name("skill2_dur_max")))
    float xn_skill2_dur_max();
    __attribute__((import_module("core"), import_name("skill2_dur_enabled")))
    float xn_skill2_dur_enabled();

    __attribute__((import_module("core"), import_name("skill3_dur_current")))
    float xn_skill3_dur_current();
    __attribute__((import_module("core"), import_name("skill3_dur_max")))
    float xn_skill3_dur_max();
    __attribute__((import_module("core"), import_name("skill3_dur_enabled")))
    float xn_skill3_dur_enabled();

    __attribute__((import_module("core"), import_name("ult_dur_current")))
    float xn_ult_dur_current();
    __attribute__((import_module("core"), import_name("ult_dur_max")))
    float xn_ult_dur_max();
    __attribute__((import_module("core"), import_name("ult_dur_enabled")))
    float xn_ult_dur_enabled();

    __attribute__((import_module("core"), import_name("get_railgun_charge")))
    float xn_get_railgun_charge();

    __attribute__((import_module("core"), import_name("get_illari_charge")))
    float xn_get_illari_charge();

    __attribute__((import_module("core"), import_name("get_hanzo_charge")))
    float xn_get_hanzo_charge();

    __attribute__((import_module("core"), import_name("get_widow_charge")))
    float xn_get_widow_charge();

    __attribute__((import_module("core"), import_name("get_lookup_skill")))
    float xn_get_lookup_skill(int32_t lookupId);

    // Aim control
    __attribute__((import_module("core"), import_name("aim_set_direction")))
    void xn_aim_set_direction(float x, float y, float z);

    __attribute__((import_module("core"), import_name("aim_get_direction")))
    int32_t xn_aim_get_direction(void* outVec3);

    __attribute__((import_module("core"), import_name("aim_set_angles")))
    void xn_aim_set_angles(float pitch, float yaw);

    __attribute__((import_module("core"), import_name("aim_get_angles")))
    int32_t xn_aim_get_angles(void* outVec2);

    __attribute__((import_module("core"), import_name("aim_to")))
    void xn_aim_at_position(float x, float y, float z, float stiffness, float unused);

    __attribute__((import_module("core"), import_name("aim_to_bone")))
    void xn_aim_at_bone(int32_t entityIndex, int32_t boneId, float stiffness, float unused);

    __attribute__((import_module("core"), import_name("aim_reset_smoothing")))
    void xn_aim_reset_smoothing();

    __attribute__((import_module("core"), import_name("aim_hits_hitbox")))
    int32_t xn_aim_hits_hitbox(int32_t entityIndex, float hitboxScale);
}

// ============================================================================
// C++ Wrapper Functions
// ============================================================================

namespace xenon
{
    // Logging
    inline void Log(const char* msg)
    {
        xn_log(msg);
    }

    inline void LogDebug(const char* msg)
    {
        xn_log_debug(msg);
    }

    inline void LogWarning(const char* msg)
    {
        xn_log_warning(msg);
    }

    inline void LogError(const char* msg)
    {
        xn_log_error(msg);
    }

    // Time
    inline float GetTime()
    {
        return xn_get_time();
    }

    // Delta time in seconds since last frame (same value as on_frame parameter)
    inline float GetDeltaTime()
    {
        return xn_get_delta_time();
    }

    // Input
    inline bool IsKeyDown(int vkey)
    {
        return xn_is_key_down(vkey) != 0;
    }

    // Skeleton — fills out with bone connection pairs for the given hero, returns count
    inline int GetSkeletonConnections(uint64_t heroId, BonePair* out)
    {
        return static_cast<int>(xn_get_skeleton_connections(heroId, out));
    }

    // Screen
    inline Vector2 ScreenSize()
    {
        return Vector2(static_cast<float>(xn_get_screen_width()),
                       static_cast<float>(xn_get_screen_height()));
    }

    inline Vector2 ScreenCenter()
    {
        return ScreenSize() * 0.5f;
    }

    // World to screen projection
    inline bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos)
    {
        return xn_world_to_screen(worldPos.x, worldPos.y, worldPos.z, &screenPos) != 0;
    }

    inline Vector2 WorldToScreen(const Vector3& worldPos)
    {
        Vector2 result;
        WorldToScreen(worldPos, result);
        return result;
    }

    // Unclamped world to screen — returns screen coordinates even if off-screen.
    // Use for bounding box calculations where you need all projected corners.
    inline bool WorldToScreenUnclamped(const Vector3& worldPos, Vector2& screenPos)
    {
        return xn_world_to_screen_unclamped(worldPos.x, worldPos.y, worldPos.z, &screenPos) != 0;
    }

    // Camera position (world-space, extracted from view-projection matrix)
    inline bool GetCameraPosition(Vector3& out)
    {
        return xn_get_camera_position(&out) != 0;
    }

    inline Vector3 GetCameraPosition()
    {
        Vector3 result;
        xn_get_camera_position(&result);
        return result;
    }

    // Camera forward direction (normalized, derived from view-projection matrix)
    inline bool GetCameraForward(Vector3& out)
    {
        return xn_get_camera_forward(&out) != 0;
    }

    inline Vector3 GetCameraForward()
    {
        Vector3 result;
        xn_get_camera_forward(&result);
        return result;
    }

    // Raw 4x4 view-projection matrix (row-major, 16 floats)
    inline bool GetViewMatrix(float* out16)
    {
        return xn_get_view_matrix(out16) != 0;
    }

    // Game state
    inline bool IsIngame()
    {
        return xn_is_ingame() != 0;
    }

    inline uint32_t GetMapId()
    {
        return static_cast<uint32_t>(xn_get_map_id());
    }

    // Returns true if the player is in any match (custom, queue, practice, etc.)
    // Fires early (before hero select). The full MatchType enum is not
    // available from persistent game state — only the boolean is readable.
    inline bool InMatch()
    {
        return xn_get_match_type() != 0;
    }

    // Practice range detection (MapId-based)
    inline bool IsPracticeRange()
    {
        return GetMapId() == 0x688;
    }

    inline float GetSensitivity()
    {
        return xn_get_sensitivity();
    }

    // Current hero
    inline uint64_t GetCurrentHero()
    {
        return xn_get_current_hero();
    }

    // Raycast — trace a line through the world and return hit info
    inline RaycastResult Raycast(const Vector3& from, const Vector3& to)
    {
        RaycastResult result{};
        xn_raycast(from.x, from.y, from.z, to.x, to.y, to.z, &result);
        return result;
    }

    // Quick visibility check between two world points
    inline bool IsPointVisible(const Vector3& from, const Vector3& to)
    {
        return xn_is_point_visible(from.x, from.y, from.z, to.x, to.y, to.z) != 0;
    }

    // Check if the raycast system is initialized and ready
    inline bool IsRaycastReady()
    {
        return xn_is_raycast_ready() != 0;
    }

    // Connection — get client-side ping in ms
    inline int GetClientPing()
    {
        return static_cast<int>(xn_get_client_ping());
    }

    // Connection — get server-side ping in ms
    inline int GetServerPing()
    {
        return static_cast<int>(xn_get_server_ping());
    }

    // Press a game button (see GameButton namespace)
    inline void PressGameButton(uint32_t bit)
    {
        xn_press_game_button(static_cast<int32_t>(bit));
    }

    // Release a game button (see GameButton namespace)
    inline void ReleaseGameButton(uint32_t bit)
    {
        xn_release_game_button(static_cast<int32_t>(bit));
    }

    // --- Ability / StateScript queries (local player only) ---

    // Returns ult charge percentage (0-100)
    inline float GetUltCharge()
    {
        return xn_get_ult_charge();
    }

    // Returns true if ult charge >= 100%
    inline bool IsUltReady()
    {
        return xn_is_ult_ready() != 0;
    }

    // Returns true if ultimate is currently active
    inline bool IsUltActive()
    {
        return xn_is_ult_active() != 0;
    }

    // Returns true if Skill 1 (Shift) is currently active
    inline bool IsSkill1Active()
    {
        return xn_is_skill1_active() > 0.5f;
    }

    // Returns true if Skill 2 (E) is currently active
    inline bool IsSkill2Active()
    {
        return xn_is_skill2_active() > 0.5f;
    }

    // Returns true if Skill 3 is currently active
    inline bool IsSkill3Active()
    {
        return xn_is_skill3_active() > 0.5f;
    }

    // Returns hero state bitmask (see HeroState namespace)
    inline uint32_t GetHeroState()
    {
        return static_cast<uint32_t>(xn_get_hero_state());
    }

    // Skill cooldown state (local player, hero-specific)
    // SkillCooldown is defined in Types.hpp
    // For per-entity cooldowns, use Entity::GetSkill1Cooldown() etc.

    inline SkillCooldown GetSkill1Cooldown()
    {
        return { xn_skill1_cd_current(), xn_skill1_cd_max(), xn_skill1_cd_enabled() > 0.5f };
    }

    inline SkillCooldown GetSkill2Cooldown()
    {
        return { xn_skill2_cd_current(), xn_skill2_cd_max(), xn_skill2_cd_enabled() > 0.5f };
    }

    inline SkillCooldown GetSkill3Cooldown()
    {
        return { xn_skill3_cd_current(), xn_skill3_cd_max(), xn_skill3_cd_enabled() > 0.5f };
    }

    inline SkillCooldown GetUltCooldown()
    {
        return { xn_ult_cd_current(), xn_ult_cd_max(), xn_ult_cd_enabled() > 0.5f };
    }

    // Ability duration state (local player, hero-specific)
    // Uses the same SkillCooldown struct: current = time remaining, max = total duration, enabled = ability active.
    // For per-entity cooldowns/durations, use Entity::GetSkill1Cooldown() etc.
    inline SkillCooldown GetSkill1Duration()
    {
        return { xn_skill1_dur_current(), xn_skill1_dur_max(), xn_skill1_dur_enabled() > 0.5f };
    }

    inline SkillCooldown GetSkill2Duration()
    {
        return { xn_skill2_dur_current(), xn_skill2_dur_max(), xn_skill2_dur_enabled() > 0.5f };
    }

    inline SkillCooldown GetSkill3Duration()
    {
        return { xn_skill3_dur_current(), xn_skill3_dur_max(), xn_skill3_dur_enabled() > 0.5f };
    }

    inline SkillCooldown GetUltDuration()
    {
        return { xn_ult_dur_current(), xn_ult_dur_max(), xn_ult_dur_enabled() > 0.5f };
    }

    // Hero-specific charge values (Sojourn railgun, Illari solar rifle, Hanzo bow, Widow scope)
    inline float GetRailgunCharge()  { return xn_get_railgun_charge(); }
    inline float GetIllariCharge()   { return xn_get_illari_charge(); }
    inline float GetHanzoCharge()    { return xn_get_hanzo_charge(); }
    inline float GetWidowCharge()    { return xn_get_widow_charge(); }

    // Generic skill lookup by ID (returns cached value for known IDs, 0 otherwise)
    inline float GetLookupSkill(uint16_t lookupId)
    {
        return xn_get_lookup_skill(static_cast<int32_t>(lookupId));
    }

    // Known skill IDs
    namespace SkillId
    {
        constexpr uint16_t UltCharge      = 0x00F8;  // Ultimate charge percentage
        constexpr uint16_t SojournCharge  = 0x00F6;  // Sojourn railgun charge
        constexpr uint16_t IllariCharge   = 0x0651;  // Illari solar rifle charge
        constexpr uint16_t HanzoCharge    = 0x00C9;  // Hanzo bow draw charge
        constexpr uint16_t SombraAmmo     = 0x0C41;  // Sombra ammo count
        constexpr uint16_t VendettaPEAmmo = 0x0000;  // TODO: Vendetta Projected Edge charges — ID unknown, find via debug panel
    }

    // Hero state bitmask flags
    namespace HeroState
    {
        constexpr uint32_t Ulting    = 0x1;  // Ultimate is active
        constexpr uint32_t Reloading = 0x2;  // Currently reloading
    }

    // =========================================================================
    // Aim Control
    // =========================================================================

    // Set the local player's view direction vector (instant, no smoothing).
    // Resets any active spring smoothing state.
    inline void AimSetDirection(const Vector3& dir)
    {
        xn_aim_set_direction(dir.x, dir.y, dir.z);
    }

    // Get the local player's current view direction vector.
    inline bool AimGetDirection(Vector3& out)
    {
        return xn_aim_get_direction(&out) != 0;
    }

    inline Vector3 AimGetDirection()
    {
        Vector3 result;
        xn_aim_get_direction(&result);
        return result;
    }

    // Set the local player's view angles in radians (instant, no smoothing).
    // pitch: vertical angle (-1.55 to 1.55), yaw: horizontal angle.
    // Resets any active spring smoothing state.
    inline void AimSetAngles(float pitch, float yaw)
    {
        xn_aim_set_angles(pitch, yaw);
    }

    // Get the local player's current view angles (pitch, yaw) in radians.
    inline bool AimGetAngles(float& pitch, float& yaw)
    {
        Vector2 out;
        if (xn_aim_get_angles(&out) == 0) return false;
        pitch = out.x;
        yaw   = out.y;
        return true;
    }

    inline Vector2 AimGetAngles()
    {
        Vector2 result;
        xn_aim_get_angles(&result);
        return result;
    }

    // Aim at a world-space position with critically-damped spring smoothing.
    // stiffness: 0 = instant snap, 10-200 = human-like, 500+ = near-instant.
    // Call every frame for smooth tracking.
    inline void AimAtPosition(const Vector3& target, float stiffness = 0.f)
    {
        xn_aim_at_position(target.x, target.y, target.z, stiffness, 0.f);
    }

    // Aim at a bone on an entity with critically-damped spring smoothing.
    // entityIndex: player array index, boneId: skeleton bone ID (see Bone namespace).
    // stiffness: 0 = instant snap, 10-200 = human-like, 500+ = near-instant.
    // Call every frame for smooth tracking.
    inline void AimAtBone(int entityIndex, int boneId, float stiffness = 0.f)
    {
        xn_aim_at_bone(entityIndex, boneId, stiffness, 0.f);
    }

    // Reset spring smoothing velocity. Call when switching targets to prevent
    // velocity carry-over from the previous target.
    inline void AimResetSmoothing()
    {
        xn_aim_reset_smoothing();
    }

    // Check if the current aim direction hits any hitbox on the given entity.
    // hitboxScale: multiplier on hitbox radius (1.0 = exact, >1.0 = forgiving).
    // Returns the BodySlot index of the first hit hitbox (0=Head, etc.), or -1 if no hit.
    inline int AimHitsHitbox(int entityIndex, float hitboxScale = 1.0f)
    {
        return static_cast<int>(xn_aim_hits_hitbox(entityIndex, hitboxScale));
    }
}
