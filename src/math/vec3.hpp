#pragma once
//
// vec3.hpp — Minimal 3-D / 2-D vector types
//
// Kept header-only and trivially constructible so they can be used
// directly as the target type in Memory::Read<Vec3>().
//

#include <cmath>
#include <algorithm>

// ── Vec3 ─────────────────────────────────────────────────────────────────────

struct Vec3
{
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3() = default;
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3  operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3  operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3  operator*(float s)       const { return {x*s,   y*s,   z*s};   }

    float LengthSq()  const { return x*x + y*y + z*z; }
    float Length()    const { return std::sqrt(LengthSq()); }
};

// ── Vec2 (screen-space) ───────────────────────────────────────────────────────

struct Vec2
{
    float x = 0.f, y = 0.f;

    Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}
};

// ── ScreenBox ─────────────────────────────────────────────────────────────────
// Result of projecting an entity's AABB onto the screen.

struct ScreenBox
{
    float left   = 0.f;   // min screen X
    float top    = 0.f;   // min screen Y  (top of head)
    float right  = 0.f;   // max screen X
    float bottom = 0.f;   // max screen Y  (feet)

    float Width()  const { return right  - left;   }
    float Height() const { return bottom - top;    }

    bool  Valid()  const { return Width() > 0.f && Height() > 0.f; }
};
