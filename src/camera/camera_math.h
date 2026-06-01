#pragma once

// Pure camera math: matrix/quaternion primitives and the head-tracking
// transforms applied to the game's world-to-camera matrix. Deliberately free
// of any REFramework or Mod dependency so it can be unit-tested in isolation
// and so the side-effecting hook code stays separate from the math.
//
// All angles are radians and pre-signed by the caller (camera_hook negates yaw
// and converts from degrees before calling in).

namespace RE2HT {

struct Matrix4x4f { float m[4][4]; };
struct alignas(16) REQuat { float x, y, z, w; };

REQuat MatrixToQuat(const Matrix4x4f& m);
void QuatToMatrix3x3(const REQuat& q, float out[3][3]);
REQuat QuatMul(const REQuat& a, const REQuat& b);
REQuat QuatNorm(const REQuat& q);

// Horizon-locked yaw plus camera-local pitch/roll. Prevents leaning artifacts
// at extreme angles by rotating the world-space basis about the vertical axis.
void ApplyWorldSpaceHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad);

// Camera-local YPR composed as a shortest-arc quaternion (gimbal-lock-free).
void ApplyCameraLocalHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad);

// Translate the camera in the body-oriented basis captured before head
// rotation, so the offset follows body orientation rather than the head-turned
// view. Offsets are in meters; X is inverted to match the engine's handedness.
void ApplyViewSpacePositionOffset(Matrix4x4f& worldMat, const Matrix4x4f& preRotationAxes,
                                  float offsetX, float offsetY, float offsetZ);

// Project the clean aim point (camera forward * aimDist) into the head-tracked
// view to get the screen-space tangents the GUI compensation reads. Returns
// false when the aim point falls behind the head-tracked camera.
bool ProjectAimToViewTangents(const Matrix4x4f& clean, const Matrix4x4f& head,
                              float aimDist, float& tanRight, float& tanUp);

} // namespace RE2HT
