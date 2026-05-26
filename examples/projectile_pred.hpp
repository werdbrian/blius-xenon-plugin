#pragma once
#include <xenon/SDK.hpp>

// Projectile intercept prediction with airborne-aware ballistic tracking.
//
// When a target is grounded:  linear prediction (accel ≈ 0).
// When a target is ascending: full ballistic arc — computes exact landing time
//                             so if the projectile arrives after they land,
//                             we aim at the landing spot + post-landing drift.
// When a target is descending: ballistic continues until landing (imminent).
//
// Usage:
//   ProjPred::Result r = ProjPred::Solve(tgt, localPos, projectileSpeed);
//   if (r.valid) {
//       if (wi.hasGravity)
//           r.aimPos.y += r.flightTime * r.flightTime * arcFactor;
//       AimAtPosition(r.aimPos, stiffness);
//   }

namespace ProjPred
{
    using namespace xenon;

    // Threshold for detecting gravity in measured acceleration (m/s²).
    // OW2 gravity ≈ 28–32 m/s²; grounded entities have net accel ≈ 0.
    constexpr float kAirborneThreshold = 8.f;

    struct MotionState
    {
        Vector3 pos;
        Vector3 vel;    // velocity (m/s)
        Vector3 accel;  // acceleration (m/s²) — negative Y when airborne
        bool    valid      = false;
        bool    airborne   = false;
        bool    ascending  = false; // vel.y > 0
        float   landingTime = 0.f;  // seconds until they return to launch height (ascending only)
    };

    // Derive position, velocity, and acceleration from lerp history.
    // Needs 3 ticks for acceleration; falls back to linear with 2.
    inline MotionState GetMotion(const Entity& tgt, float tickRate = 63.f)
    {
        MotionState m;
        LerpEntry hist[16];
        int n = tgt.GetLerpHistory(hist, 16);
        if (n < 2) return m;

        float dt = 1.f / tickRate;

        float dtTicks01 = (float)(hist[0].tick - hist[1].tick);
        if (dtTicks01 <= 0.f) return m;
        float dtSec01 = dtTicks01 * dt;

        m.pos = hist[0].position;
        m.vel = {
            (hist[0].position.x - hist[1].position.x) / dtSec01,
            (hist[0].position.y - hist[1].position.y) / dtSec01,
            (hist[0].position.z - hist[1].position.z) / dtSec01,
        };

        if (n >= 3)
        {
            float dtTicks12 = (float)(hist[1].tick - hist[2].tick);
            if (dtTicks12 > 0.f)
            {
                float dtSec12 = dtTicks12 * dt;
                Vector3 velPrev = {
                    (hist[1].position.x - hist[2].position.x) / dtSec12,
                    (hist[1].position.y - hist[2].position.y) / dtSec12,
                    (hist[1].position.z - hist[2].position.z) / dtSec12,
                };
                float midDt = (dtSec01 + dtSec12) * 0.5f;
                m.accel = {
                    (m.vel.x - velPrev.x) / midDt,
                    (m.vel.y - velPrev.y) / midDt,
                    (m.vel.z - velPrev.z) / midDt,
                };
            }
        }

        m.airborne  = (m.accel.y < -kAirborneThreshold);
        m.ascending = (m.vel.y > 0.f);

        // Grounded targets have no real acceleration — noise in the lerp history
        // produces huge spurious accel values that make the prediction jump wildly.
        // Only keep acceleration when actually airborne.
        if (!m.airborne)
            m.accel = { 0.f, 0.f, 0.f };

        // If ascending and airborne: solve t when y(t) = y₀ (symmetric arc).
        // y₀ + vy*t + 0.5*ay*t² = y₀  =>  t = -2*vy/ay
        // Valid only when airborne + ascending; assumes they land at launch height.
        if (m.airborne && m.ascending && m.accel.y < 0.f)
            m.landingTime = -2.f * m.vel.y / m.accel.y;

        m.valid = true;
        return m;
    }

    // Evaluate ballistic position at time t seconds from now.
    inline Vector3 BallisticAt(const MotionState& m, float t)
    {
        float ht2 = 0.5f * t * t;
        return {
            m.pos.x + m.vel.x * t + m.accel.x * ht2,
            m.pos.y + m.vel.y * t + m.accel.y * ht2,
            m.pos.z + m.vel.z * t + m.accel.z * ht2,
        };
    }

    struct Result
    {
        bool    valid            = false;
        Vector3 aimPos;                   // world-space position to aim at
        float   flightTime       = 0.f;   // predicted seconds until impact
        bool    targetAirborne   = false; // target was airborne when solved
        bool    aimsAtLanding    = false; // orb arrives after target lands — aimed at landing spot
        float   landingTime      = 0.f;   // target's time to land (0 if grounded/descending)
    };

    // Solve intercept.
    //   tgt             — target entity
    //   shooterPos      — projectile spawn point
    //   projectileSpeed — m/s (must be > 0)
    //   iterations      — refinement passes (2–3 is enough for OW ranges)
    inline Result Solve(const Entity& tgt, const Vector3& shooterPos,
                        float projectileSpeed, int iterations = 3)
    {
        Result r;
        if (projectileSpeed <= 0.f) return r;

        MotionState m = GetMotion(tgt);
        if (!m.valid) return r;

        auto Dist3 = [](Vector3 a, Vector3 b) -> float {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return sqrtf(dx*dx + dy*dy + dz*dz);
        };

        float t = Dist3(shooterPos, m.pos) / projectileSpeed;

        for (int i = 0; i < iterations; ++i)
        {
            Vector3 predicted = BallisticAt(m, t);
            t = Dist3(shooterPos, predicted) / projectileSpeed;
        }

        r.valid          = true;
        r.flightTime     = t;
        r.targetAirborne = m.airborne;
        r.landingTime    = m.landingTime;

        // Special case: target is ascending and we can compute landing time.
        // If orb arrives AFTER they land, aim at landing position + horizontal drift.
        if (m.airborne && m.ascending && m.landingTime > 0.f && t > m.landingTime)
        {
            r.aimsAtLanding = true;

            Vector3 landPos  = BallisticAt(m, m.landingTime);
            float   postLand = t - m.landingTime;

            // After landing: Y locked, horizontal velocity continues (no accel on ground)
            r.aimPos = {
                landPos.x + m.vel.x * postLand,
                landPos.y,
                landPos.z + m.vel.z * postLand,
            };
        }
        else
        {
            r.aimPos = BallisticAt(m, t);
        }

        return r;
    }
}
