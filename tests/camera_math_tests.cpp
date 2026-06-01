// Characterization tests for the pure head-tracking math extracted from
// camera_hook.cpp. These lock the transforms applied to the game's
// world-to-camera matrix: a transcription error in the extraction (or any
// future "cleanup" of the arithmetic) breaks the core look-vs-aim behaviour
// in a way no in-game playtest would catch automatically.

#include "camera/camera_math.h"

#include <cmath>
#include <cstdio>
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

RE2HT::Matrix4x4f Identity() {
    RE2HT::Matrix4x4f m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

// Rows of the 3x3 block must stay orthonormal after any rigid rotation.
bool RotationIsOrthonormal(const RE2HT::Matrix4x4f& m) {
    for (int r = 0; r < 3; ++r) {
        float len = std::sqrt(m.m[r][0]*m.m[r][0] + m.m[r][1]*m.m[r][1] + m.m[r][2]*m.m[r][2]);
        if (!NearEqual(len, 1.0f, 1e-3f)) return false;
    }
    auto dot = [&](int a, int b) {
        return m.m[a][0]*m.m[b][0] + m.m[a][1]*m.m[b][1] + m.m[a][2]*m.m[b][2];
    };
    return NearEqual(dot(0,1), 0.f, 1e-3f) && NearEqual(dot(0,2), 0.f, 1e-3f) && NearEqual(dot(1,2), 0.f, 1e-3f);
}

const float kHalfPi = 1.57079632679f;

}  // namespace

int RunCameraMathTests() {
    using namespace RE2HT;
    std::cout << "Camera math tests\n";

    // Quaternion primitives.
    {
        REQuat n = QuatNorm(REQuat{0, 0, 0, 2});
        Check(NearEqual(n.x, 0) && NearEqual(n.y, 0) && NearEqual(n.z, 0) && NearEqual(n.w, 1),
              "QuatNorm scales to unit length");

        REQuat degenerate = QuatNorm(REQuat{0, 0, 0, 0});
        Check(NearEqual(degenerate.w, 1) && NearEqual(degenerate.x, 0),
              "QuatNorm of near-zero returns identity");

        REQuat id{0, 0, 0, 1};
        REQuat prod = QuatMul(id, REQuat{0.1f, 0.2f, 0.3f, 0.9f});
        Check(NearEqual(prod.x, 0.1f) && NearEqual(prod.y, 0.2f) && NearEqual(prod.z, 0.3f) && NearEqual(prod.w, 0.9f),
              "QuatMul by identity is a no-op");

        float r[3][3];
        QuatToMatrix3x3(id, r);
        Check(NearEqual(r[0][0], 1) && NearEqual(r[1][1], 1) && NearEqual(r[2][2], 1) &&
              NearEqual(r[0][1], 0) && NearEqual(r[2][0], 0),
              "QuatToMatrix3x3 of identity quat is identity");

        REQuat q = MatrixToQuat(Identity());
        Check(NearEqual(q.x, 0) && NearEqual(q.y, 0) && NearEqual(q.z, 0) && NearEqual(q.w, 1),
              "MatrixToQuat of identity is identity quat");
    }

    // Zero rotation must not perturb the matrix in either yaw mode.
    {
        Matrix4x4f m = Identity();
        ApplyWorldSpaceHeadRotation(m, 0, 0, 0);
        Check(NearEqual(m.m[0][0], 1) && NearEqual(m.m[1][1], 1) && NearEqual(m.m[2][2], 1) &&
              NearEqual(m.m[0][2], 0) && NearEqual(m.m[2][0], 0),
              "world-space zero rotation leaves identity unchanged");

        Matrix4x4f m2 = Identity();
        ApplyCameraLocalHeadRotation(m2, 0, 0, 0);
        Check(NearEqual(m2.m[0][0], 1) && NearEqual(m2.m[1][1], 1) && NearEqual(m2.m[2][2], 1) &&
              NearEqual(m2.m[0][2], 0) && NearEqual(m2.m[2][0], 0),
              "camera-local zero rotation leaves identity unchanged");
    }

    // 90-degree pure yaw produces the same known basis in both modes (pitch =
    // roll = 0): m[0][2] = -1, m[2][0] = +1, m[1][1] = 1.
    {
        Matrix4x4f m = Identity();
        ApplyWorldSpaceHeadRotation(m, kHalfPi, 0, 0);
        Check(NearEqual(m.m[0][2], -1.f) && NearEqual(m.m[2][0], 1.f) && NearEqual(m.m[1][1], 1.f),
              "world-space 90deg yaw maps to expected basis");
        Check(RotationIsOrthonormal(m), "world-space 90deg yaw stays orthonormal");

        Matrix4x4f m2 = Identity();
        ApplyCameraLocalHeadRotation(m2, kHalfPi, 0, 0);
        Check(NearEqual(m2.m[0][2], -1.f) && NearEqual(m2.m[2][0], 1.f) && NearEqual(m2.m[1][1], 1.f),
              "camera-local 90deg yaw maps to expected basis");
        Check(RotationIsOrthonormal(m2), "camera-local 90deg yaw stays orthonormal");
    }

    // Combined yaw+pitch+roll keeps the basis orthonormal (no shear/scale leak).
    {
        Matrix4x4f m = Identity();
        ApplyCameraLocalHeadRotation(m, 0.3f, -0.4f, 0.2f);
        Check(RotationIsOrthonormal(m), "camera-local combined rotation stays orthonormal");

        Matrix4x4f m2 = Identity();
        ApplyWorldSpaceHeadRotation(m2, 0.3f, -0.4f, 0.2f);
        Check(RotationIsOrthonormal(m2), "world-space combined rotation stays orthonormal");
    }

    // Position offset translates along the pre-rotation basis; X is inverted.
    {
        Matrix4x4f m = Identity();
        ApplyViewSpacePositionOffset(m, Identity(), 0.5f, 0.2f, -0.3f);
        Check(NearEqual(m.m[3][0], -0.5f) && NearEqual(m.m[3][1], 0.2f) && NearEqual(m.m[3][2], -0.3f),
              "position offset applies inverted X along identity basis");

        Matrix4x4f none = Identity();
        ApplyViewSpacePositionOffset(none, Identity(), 0, 0, 0);
        Check(NearEqual(none.m[3][0], 0) && NearEqual(none.m[3][1], 0) && NearEqual(none.m[3][2], 0),
              "zero position offset leaves translation unchanged");
    }

    // Aim-tangent projection: aligned clean/head views put the reticle at center;
    // a degenerate (behind-camera) aim point reports failure.
    {
        float tanR = 9.f, tanU = 9.f;
        bool ok = ProjectAimToViewTangents(Identity(), Identity(), 50.f, tanR, tanU);
        Check(ok && NearEqual(tanR, 0.f) && NearEqual(tanU, 0.f),
              "aligned views project aim to screen center");

        float tr = 0.f, tu = 0.f;
        bool behind = ProjectAimToViewTangents(Identity(), Identity(), 0.f, tr, tu);
        Check(!behind, "degenerate aim point (zero depth) reports failure");
    }

    if (g_failures == 0) {
        std::cout << "Camera math tests: all passed\n";
    } else {
        std::cout << "Camera math tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
