#pragma once
#include "Types.hpp"

// ============================================================================
// WASM Imports - Entity Functions
// ============================================================================

extern "C"
{
    // Entity enumeration
    __attribute__((import_module("core"), import_name("get_entity_count")))
    int32_t xn_get_entity_count();

    __attribute__((import_module("core"), import_name("get_entity")))
    void xn_get_entity(int32_t index, void* outEntity);

    __attribute__((import_module("core"), import_name("get_local_player")))
    void xn_get_local_player(void* outEntity);

    // Bone position (outVec3 is a Vector3*)
    __attribute__((import_module("core"), import_name("get_bone_pos")))
    int32_t xn_get_bone_pos(int32_t entityIndex, int32_t boneId, void* outVec3);

    // Visibility
    __attribute__((import_module("core"), import_name("is_entity_visible")))
    int32_t xn_is_entity_visible(int32_t entityIndex);

    // Team relationship
    __attribute__((import_module("core"), import_name("is_entity_enemy")))
    int32_t xn_is_entity_enemy(int32_t entityIndex);

    __attribute__((import_module("core"), import_name("is_entity_ally")))
    int32_t xn_is_entity_ally(int32_t entityIndex);

    // Distance
    __attribute__((import_module("core"), import_name("get_distance_to_entity")))
    float xn_get_distance_to_entity(int32_t entityIndex);

    // Targeting
    __attribute__((import_module("core"), import_name("find_best_target")))
    int32_t xn_find_best_target(int32_t flags);

    // Advanced targeting (FOV-based)
    __attribute__((import_module("core"), import_name("find_best_target_fov")))
    int32_t xn_find_best_target_fov(float fov, int32_t boneId, int32_t flags);

    __attribute__((import_module("core"), import_name("get_fov_to_entity")))
    float xn_get_fov_to_entity(int32_t index, int32_t boneId);

    __attribute__((import_module("core"), import_name("get_screen_offset_to_bone")))
    int32_t xn_get_screen_offset_to_bone(int32_t index, int32_t boneId, void* outVec2);

    __attribute__((import_module("core"), import_name("predict_position")))
    int32_t xn_predict_position(int32_t index, float time, void* outVec3);

    __attribute__((import_module("core"), import_name("get_closest_bone_in_fov")))
    int32_t xn_get_closest_bone_in_fov(int32_t index, float fov, void* outBoneId);

    __attribute__((import_module("core"), import_name("calc_angle")))
    int32_t xn_calc_angle(float x1, float y1, float z1, float x2, float y2, float z2, void* outVec3);

    // Outline / Glow
    __attribute__((import_module("core"), import_name("set_entity_outline")))
    void xn_set_entity_outline(int32_t entityIndex, int32_t outlineType, float r, float g, float b, float a);

    // Distance between two points
    __attribute__((import_module("core"), import_name("get_distance")))
    float xn_get_distance(float ax, float ay, float az, float bx, float by, float bz);

    // Weapon info for local player
    __attribute__((import_module("core"), import_name("get_weapon_info")))
    int32_t xn_get_weapon_info(int32_t inputFlags, void* outWeaponInfo);

    // Hitbox access
    __attribute__((import_module("core"), import_name("get_entity_hitboxes")))
    int32_t xn_get_entity_hitboxes(int32_t entityIndex, void* outArray);

    __attribute__((import_module("core"), import_name("get_entity_hitbox")))
    int32_t xn_get_entity_hitbox(int32_t entityIndex, int32_t hitboxIndex, void* out);

    // Lerp history
    __attribute__((import_module("core"), import_name("get_lerp_history")))
    int32_t xn_get_lerp_history(int32_t entityIndex, void* outArray);
}

// ============================================================================
// PluginEntity struct (matches runtime PluginEntity layout)
// ============================================================================

namespace xenon
{
#pragma pack(push, 1)
    struct PluginCooldown
    {
        float current;    // Time remaining (cooldown) or duration remaining
        float max;        // Total cooldown / duration length
        uint8_t enabled;  // 1 if actively ticking, 0 otherwise
        uint8_t _pad[3];
    };

    struct PluginEntity
    {
        uint32_t id;
        uint64_t heroId;
        uint32_t entityType;
        uint32_t team;
        Vector3 position;
        Vector3 velocity;
        Vector3 rotation;
        float health;
        float maxHealth;
        float armor;
        float barrier;
        uint8_t alive;
        uint8_t visible;
        uint8_t isLocalPlayer;
        uint8_t isInvulnerable;
        float ultCharge;    // Ult charge % (0-100), populated for all hero entities
        uint8_t ultActive;  // 1 if ult is active
        Vector2 skill1Active;  // .x = 1.0 if Skill 1 (Shift) active, 0.0 otherwise
        Vector2 skill2Active;  // .x = 1.0 if Skill 2 (E) active, 0.0 otherwise
        Vector2 skill3Active;  // .x = 1.0 if Skill 3 active, 0.0 otherwise

        Vector3 delta1;  // AABB min (local space)
        Vector3 delta2;  // AABB max (local space)
        float nametagYPos;

        float overhealth;
        uint8_t hitboxCount;
        uint8_t isTargetable;
        uint8_t isReloading;
        uint8_t _pad0;
        Vector3 forward;

        // Per-entity ability cooldowns (populated for all hero entities)
        PluginCooldown skill1Cd;
        PluginCooldown skill1Duration;
        PluginCooldown skill2Cd;
        PluginCooldown skill2Duration;
        PluginCooldown skill3Cd;
        PluginCooldown skill3Duration;
        PluginCooldown ultCd;
    };
#pragma pack(pop)

    // ============================================================================
    // WeaponInfo struct (matches runtime PluginWeaponInfo layout)
    // ============================================================================

#pragma pack(push, 1)
    struct WeaponInfo
    {
        uint8_t valid;
        uint8_t useable;
        uint8_t shootable;
        uint8_t hasGravity;     // projectile affected by gravity (arc)
        float projectileSpeed;  // m/s, 0 = hitscan
        float maxRange;         // hard max range in meters, 0 = unlimited
        uint8_t reloading;      // currently in reload animation
        uint8_t skillBlocked;   // ability animation preventing fire
        uint8_t _pad0[2];
    };
#pragma pack(pop)

    // ============================================================================
    // Hitbox struct
    // ============================================================================

#pragma pack(push, 1)
    struct Hitbox
    {
        uint16_t boneIndex;
        float radius;
        int8_t bodySlot;        // BoneSlot index, -1 = unclassified
        uint8_t isCapsule;
        Vector3 worldPos;
        Vector3 capsuleEnd;     // only valid if isCapsule
    };
#pragma pack(pop)

    // ============================================================================
    // Lerp history entry
    // ============================================================================

#pragma pack(push, 1)
    struct LerpEntry
    {
        int32_t tick;
        Vector3 position;
    };
#pragma pack(pop)

    // Input flags for GetWeaponInfo
    namespace InputFlag
    {
        constexpr int32_t PrimaryFire   = 0x0001;  // Left mouse
        constexpr int32_t SecondaryFire = 0x0002;  // Right mouse
        constexpr int32_t ScopedShoot   = 0x0003;  // Scoped fire (Ana, Ashe, Widow)
        constexpr int32_t Skill1        = 0x0008;
        constexpr int32_t Skill2        = 0x0010;
        constexpr int32_t Ultimate      = 0x0020;
        constexpr int32_t PrimaryRelease = 0x1000; // Left mouse release (Hanzo)
    }

    // Get weapon info for the local player's current hero.
    // Returns true if the input flag is valid.
    inline bool GetWeaponInfo(int32_t inputFlags, WeaponInfo& out)
    {
        return xn_get_weapon_info(inputFlags, &out) != 0;
    }

    // ============================================================================
    // Entity Class (wraps index into entity array)
    // ============================================================================

    class Entity
    {
    private:
        int32_t m_index = -1;
        PluginEntity m_data{};
        bool m_valid = false;

    public:
        Entity() = default;
        explicit Entity(int32_t index) : m_index(index)
        {
            if (index >= 0)
            {
                xn_get_entity(index, &m_data);
                m_valid = m_data.alive || m_data.health > 0;
            }
        }

        // Create from local player. m_valid mirrors the index-constructor logic
        // so callers can skip work on frames where the host snapshot isn't ready
        // (e.g. early in the render path before the entity list is populated).
        static Entity Local()
        {
            Entity e;
            e.m_index = 0;
            xn_get_local_player(&e.m_data);
            e.m_valid = e.m_data.alive || e.m_data.health > 0;
            return e;
        }

        // Handle access
        int32_t Index() const { return m_index; }
        bool IsValid() const { return m_valid && m_index >= 0; }
        explicit operator bool() const { return IsValid(); }

        // Identity
        uint64_t GetHeroId() const { return m_data.heroId; }
        EntityType GetEntityType() const { return static_cast<EntityType>(m_data.entityType); }
        Team GetTeam() const { return static_cast<Team>(m_data.team); }

        // State
        bool IsAlive() const { return m_data.alive != 0; }
        bool IsLocal() const { return m_data.isLocalPlayer != 0; }
        bool IsVisible() const { return m_data.visible != 0; }
        bool IsInvulnerable() const { return m_data.isInvulnerable != 0; }

        bool IsEnemy() const { return xn_is_entity_enemy(m_index) != 0; }
        bool IsAlly() const { return xn_is_entity_ally(m_index) != 0; }

        // Ability state
        float GetUltCharge() const { return m_data.ultCharge; }
        bool IsUltActive() const { return m_data.ultActive != 0; }
        bool IsSkill1Active() const { return m_data.skill1Active.x > 0.5f; }
        bool IsSkill2Active() const { return m_data.skill2Active.x > 0.5f; }
        bool IsSkill3Active() const { return m_data.skill3Active.x > 0.5f; }

        // Per-entity cooldown/duration accessors
        SkillCooldown GetSkill1Cooldown() const { return { m_data.skill1Cd.current, m_data.skill1Cd.max, m_data.skill1Cd.enabled != 0 }; }
        SkillCooldown GetSkill2Cooldown() const { return { m_data.skill2Cd.current, m_data.skill2Cd.max, m_data.skill2Cd.enabled != 0 }; }
        SkillCooldown GetSkill3Cooldown() const { return { m_data.skill3Cd.current, m_data.skill3Cd.max, m_data.skill3Cd.enabled != 0 }; }
        SkillCooldown GetUltCooldown() const { return { m_data.ultCd.current, m_data.ultCd.max, m_data.ultCd.enabled != 0 }; }
        SkillCooldown GetSkill1Duration() const { return { m_data.skill1Duration.current, m_data.skill1Duration.max, m_data.skill1Duration.enabled != 0 }; }
        SkillCooldown GetSkill2Duration() const { return { m_data.skill2Duration.current, m_data.skill2Duration.max, m_data.skill2Duration.enabled != 0 }; }
        SkillCooldown GetSkill3Duration() const { return { m_data.skill3Duration.current, m_data.skill3Duration.max, m_data.skill3Duration.enabled != 0 }; }

        // Health
        float GetHealth() const { return m_data.health; }
        float GetTotalHealth() const { return m_data.health; }
        float GetHealthMax() const { return m_data.maxHealth; }
        float GetHealthPercent() const
        {
            return m_data.maxHealth > 0 ? (m_data.health / m_data.maxHealth) * 100.f : 0.f;
        }
        bool IsFullHealth() const { return m_data.health >= m_data.maxHealth; }
        float GetOverhealth() const { return m_data.overhealth; }
        float GetArmor() const { return m_data.armor; }
        float GetBarrier() const { return m_data.barrier; }

        // Forward vector (entity facing direction)
        Vector3 GetForward() const { return m_data.forward; }

        // Targeting / state
        bool IsTargetable() const { return m_data.isTargetable != 0; }
        bool IsReloading() const { return m_data.isReloading != 0; }

        // Hitboxes
        int GetHitboxCount() const { return m_data.hitboxCount; }

        bool GetHitbox(int index, Hitbox& out) const
        {
            return xn_get_entity_hitbox(m_index, index, &out) != 0;
        }

        int GetHitboxes(Hitbox* out, int maxCount) const
        {
            int count = xn_get_entity_hitboxes(m_index, out);
            return (count > maxCount) ? maxCount : count;
        }

        // Lerp history (server tick positions, newest first)
        int GetLerpHistory(LerpEntry* out, int maxCount) const
        {
            int count = xn_get_lerp_history(m_index, out);
            return (count > maxCount) ? maxCount : count;
        }

        // Position / Rotation
        Vector3 GetPosition() const { return m_data.position; }
        float GetYaw() const { return m_data.rotation.x; }
        Vector3 GetDelta1() const { return m_data.delta1; }
        Vector3 GetDelta2() const { return m_data.delta2; }
        Vector3 GetBoundsMin() const { return m_data.delta1; }
        Vector3 GetBoundsMax() const { return m_data.delta2; }
        float GetNametagYPos() const { return m_data.nametagYPos; }

        Vector3 GetHeadPos() const
        {
            Vector3 result;
            if (xn_get_bone_pos(m_index, Bone::Head, &result))
                return result;
            return m_data.position;
        }

        Vector3 GetBonePos(int boneId) const
        {
            Vector3 result;
            xn_get_bone_pos(m_index, boneId, &result);
            return result;
        }

        // Distance to local player
        float GetDistance() const
        {
            return xn_get_distance_to_entity(m_index);
        }

        // FOV distance (pixels from crosshair to bone on screen)
        float GetFovTo(int boneId) const
        {
            return xn_get_fov_to_entity(m_index, boneId);
        }

        // Screen offset from crosshair to bone
        Vector2 GetScreenOffsetTo(int boneId) const
        {
            Vector2 result{};
            xn_get_screen_offset_to_bone(m_index, boneId, &result);
            return result;
        }

        // Predict future position using velocity
        Vector3 PredictPosition(float time) const
        {
            Vector3 result{};
            xn_predict_position(m_index, time, &result);
            return result;
        }

        // Find closest bone to crosshair within FOV radius
        int GetClosestBoneInFov(float fovRadius) const
        {
            int32_t boneId = -1;
            if (xn_get_closest_bone_in_fov(m_index, fovRadius, &boneId))
                return boneId;
            return -1;
        }

        // Calculate angle (pitch/yaw) from source position to this entity's position
        Vector3 GetAngleFrom(const Vector3& src) const
        {
            Vector3 result{};
            xn_calc_angle(src.x, src.y, src.z, m_data.position.x, m_data.position.y, m_data.position.z, &result);
            return result;
        }

        // Outline / Glow
        void SetOutline(int outlineType, Color color) const
        {
            xn_set_entity_outline(m_index, outlineType, color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f);
        }
        void SetOutlineVisible(Color color) const { SetOutline(1, color); }
        void SetOutlineOccluded(Color color) const { SetOutline(2, color); }

        // Comparison
        bool operator==(const Entity& other) const { return m_index == other.m_index; }
        bool operator!=(const Entity& other) const { return m_index != other.m_index; }
    };

    // ============================================================================
    // Entity Enumeration Helpers
    // ============================================================================

    inline Entity LocalPlayer() { return Entity::Local(); }
    inline Entity GetPlayer(int index) { return Entity(index); }
    inline int GetPlayerCount() { return xn_get_entity_count(); }

    // Flags for FindBestTarget / FindBestTargetInFov
    namespace TargetFlags
    {
        constexpr int Enemy   = 0x01;  // Only enemies
        constexpr int Team    = 0x02;  // Only teammates
        constexpr int Visible = 0x04;  // Only visible entities
        constexpr int LowHP   = 0x08;  // Prioritize low health
    }

    // Find best target (distance-based)
    inline Entity FindBestTarget(int flags)
    {
        int32_t idx = xn_find_best_target(flags);
        return (idx >= 0 && idx != 0xFFFFFFFF) ? Entity(idx) : Entity();
    }

    // Calculate angle (pitch, yaw, 0) from pos1 to pos2 in radians
    inline Vector3 CalcAngle(const Vector3& from, const Vector3& to)
    {
        Vector3 result{};
        xn_calc_angle(from.x, from.y, from.z, to.x, to.y, to.z, &result);
        return result;
    }

    // Find best target within FOV radius (screen-space, closest to crosshair)
    inline Entity FindBestTargetInFov(float fovRadius, int boneId, int flags)
    {
        int32_t idx = xn_find_best_target_fov(fovRadius, boneId, flags);
        return (idx >= 0 && idx != 0xFFFFFFFF) ? Entity(idx) : Entity();
    }

    // ============================================================================
    // Iterator for range-based for loops
    // ============================================================================


    class PlayerRange
    {
    public:
        class Iterator
        {
            int m_index;
        public:
            Iterator(int index) : m_index(index) {}
            Entity operator*() const { return Entity(m_index); }
            Iterator& operator++() { ++m_index; return *this; }
            bool operator!=(const Iterator& other) const { return m_index != other.m_index; }
        };

        Iterator begin() const { return Iterator(0); }
        Iterator end() const { return Iterator(GetPlayerCount()); }
    };

    inline PlayerRange Players() { return PlayerRange(); }
}
