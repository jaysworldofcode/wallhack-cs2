#pragma once
//
// math_utils.hpp — View-matrix helper and world-to-screen projection
//
// CS2 (Source 2) exposes a 4×4 column-major view-projection matrix at a
// known client.dll offset.  We multiply a world-space position by that
// matrix and perform the perspective divide to obtain normalised device
// coordinates, then map those to pixel coordinates.
//
//  World → Clip:   clip = M * vec4(world, 1)
//  Clip  → NDC:    ndc  = clip.xyz / clip.w
//  NDC   → Screen: sx   = (ndc.x * 0.5 + 0.5) * screenW
//                  sy   = (1.0 - (ndc.y * 0.5 + 0.5)) * screenH
//                         ↑ Y is flipped: NDC +Y = up, screen +Y = down
//

#include "vec3.hpp"
#include <array>
#include <optional>
#include <limits>
#include <algorithm>

// ── ViewMatrix ───────────────────────────────────────────────────────────────

// 4×4 view-projection matrix stored as 16 contiguous floats.
// Memory::Read<ViewMatrix> works because the struct is trivially copyable.
struct ViewMatrix
{
    float m[4][4]{};  // m[row][col]

    // Convenience: access as a flat array matching the game's memory layout.
    const float* data() const { return &m[0][0]; }
};

// ── WorldToScreen ─────────────────────────────────────────────────────────────

/// Projects `world` through `vMatrix` into screen pixel coordinates.
///
/// @param screenW  Viewport width  in pixels.
/// @param screenH  Viewport height in pixels.
/// @returns        Screen-space Vec2 on success, std::nullopt if the point
///                 is behind the camera (clip.w ≤ 0).
[[nodiscard]] inline std::optional<Vec2> WorldToScreen(
    const Vec3&       world,
    const ViewMatrix& vMatrix,
    float             screenW,
    float             screenH)
{
    const float* M = vMatrix.data();  // row-major: M[row*4 + col]

    // Multiply homogeneous world position by the 4×4 matrix.
    // clip = M * [wx, wy, wz, 1]ᵀ
    float clipX = M[0]*world.x + M[1]*world.y + M[2]*world.z  + M[3];
    float clipY = M[4]*world.x + M[5]*world.y + M[6]*world.z  + M[7];
    // clipZ is not used for 2-D screen position, only for depth culling.
    float clipW = M[12]*world.x + M[13]*world.y + M[14]*world.z + M[15];

    // Points behind the near plane have a negative w; skip them.
    if (clipW <= 0.001f)
        return std::nullopt;

    // Perspective divide → NDC in [-1, 1].
    float ndcX =  clipX / clipW;
    float ndcY =  clipY / clipW;

    // Map NDC to pixel coordinates.
    float screenX = (ndcX *  0.5f + 0.5f) * screenW;
    float screenY = (ndcY * -0.5f + 0.5f) * screenH;  // flip Y

    return Vec2{ screenX, screenY };
}

// ── GetScreenBox ──────────────────────────────────────────────────────────────

/// Computes a 2-D axis-aligned screen bounding box for a standing player.
///
/// Approach:
///   • `origin` is the entity's world-space feet position.
///   • We sample 8 corners of an approximate capsule bounding box
///     (±halfW, ±halfW horizontally; 0 to `height` vertically).
///   • Project each corner and track the screen-space extents.
///
/// @param origin    Feet position from m_vecOrigin.
/// @param height    Approximate standing height in world units (default 72).
/// @param halfW     Approximate half-width in world units  (default 16).
[[nodiscard]] inline std::optional<ScreenBox> GetScreenBox(
    const Vec3&       origin,
    const ViewMatrix& vMatrix,
    float             screenW,
    float             screenH,
    float             height = 72.f,
    float             halfW  = 16.f)
{
    // 8 corners of the bounding box.
    const Vec3 corners[8] = {
        { origin.x - halfW, origin.y - halfW, origin.z          },
        { origin.x + halfW, origin.y - halfW, origin.z          },
        { origin.x - halfW, origin.y + halfW, origin.z          },
        { origin.x + halfW, origin.y + halfW, origin.z          },
        { origin.x - halfW, origin.y - halfW, origin.z + height },
        { origin.x + halfW, origin.y - halfW, origin.z + height },
        { origin.x - halfW, origin.y + halfW, origin.z + height },
        { origin.x + halfW, origin.y + halfW, origin.z + height },
    };

    float minX =  std::numeric_limits<float>::max();
    float minY =  std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();

    int projected = 0;
    for (const Vec3& corner : corners)
    {
        auto screen = WorldToScreen(corner, vMatrix, screenW, screenH);
        if (!screen) continue;

        minX = std::min(minX, screen->x);
        minY = std::min(minY, screen->y);
        maxX = std::max(maxX, screen->x);
        maxY = std::max(maxY, screen->y);
        ++projected;
    }

    if (projected == 0)
        return std::nullopt;

    return ScreenBox{ minX, minY, maxX, maxY };
}
