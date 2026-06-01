#include "camera_math.h"

#include <cmath>

namespace RE2HT {

REQuat MatrixToQuat(const Matrix4x4f& m) {
    float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    REQuat q;
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m[2][1] - m.m[1][2]) / s;
        q.y = (m.m[0][2] - m.m[2][0]) / s;
        q.z = (m.m[1][0] - m.m[0][1]) / s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        float s = sqrtf(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
        q.w = (m.m[2][1] - m.m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        float s = sqrtf(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
        q.w = (m.m[0][2] - m.m[2][0]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        float s = sqrtf(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
        q.w = (m.m[1][0] - m.m[0][1]) / s;
        q.x = (m.m[0][2] + m.m[2][0]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

void QuatToMatrix3x3(const REQuat& q, float out[3][3]) {
    float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    out[0][0]=1-2*(yy+zz); out[0][1]=2*(xy+wz);   out[0][2]=2*(xz-wy);
    out[1][0]=2*(xy-wz);   out[1][1]=1-2*(xx+zz);  out[1][2]=2*(yz+wx);
    out[2][0]=2*(xz+wy);   out[2][1]=2*(yz-wx);    out[2][2]=1-2*(xx+yy);
}

REQuat QuatMul(const REQuat& a, const REQuat& b) {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

REQuat QuatNorm(const REQuat& q) {
    float l = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (l < 0.0001f) return {0,0,0,1};
    return {q.x/l, q.y/l, q.z/l, q.w/l};
}

void ApplyWorldSpaceHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad) {
    float cy = cosf(yawRad), sy = -sinf(yawRad);
    for (int r = 0; r < 3; r++) {
        float x = worldMat.m[r][0];
        float z = worldMat.m[r][2];
        worldMat.m[r][0] = x * cy - z * sy;
        worldMat.m[r][2] = x * sy + z * cy;
    }

    float cp = cosf(pitchRad), sp = sinf(pitchRad);
    float cr = cosf(rollRad), sr = sinf(rollRad);
    float prRot[3][3] = {
        { cr,      sr,      0   },
        {-cp*sr,   cp*cr,   sp  },
        { sp*sr,  -sp*cr,   cp  }
    };

    for (int c = 0; c < 3; c++) {
        float c0 = worldMat.m[0][c];
        float c1 = worldMat.m[1][c];
        float c2 = worldMat.m[2][c];
        worldMat.m[0][c] = prRot[0][0]*c0 + prRot[0][1]*c1 + prRot[0][2]*c2;
        worldMat.m[1][c] = prRot[1][0]*c0 + prRot[1][1]*c1 + prRot[1][2]*c2;
        worldMat.m[2][c] = prRot[2][0]*c0 + prRot[2][1]*c1 + prRot[2][2]*c2;
    }
}

void ApplyCameraLocalHeadRotation(Matrix4x4f& worldMat, float yawRad, float pitchRad, float rollRad) {
    float hy = yawRad * 0.5f, hp = pitchRad * 0.5f, hr = rollRad * 0.5f;
    REQuat qy = {0, sinf(hy), 0, cosf(hy)};
    REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
    REQuat qz = {0, 0, sinf(hr), cosf(hr)};
    REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));

    float headRot[3][3];
    QuatToMatrix3x3(q, headRot);

    for (int c = 0; c < 3; c++) {
        float c0 = worldMat.m[0][c];
        float c1 = worldMat.m[1][c];
        float c2 = worldMat.m[2][c];
        worldMat.m[0][c] = headRot[0][0]*c0 + headRot[0][1]*c1 + headRot[0][2]*c2;
        worldMat.m[1][c] = headRot[1][0]*c0 + headRot[1][1]*c1 + headRot[1][2]*c2;
        worldMat.m[2][c] = headRot[2][0]*c0 + headRot[2][1]*c1 + headRot[2][2]*c2;
    }
}

void ApplyViewSpacePositionOffset(Matrix4x4f& worldMat, const Matrix4x4f& preRotationAxes,
                                  float offsetX, float offsetY, float offsetZ) {
    float px = -offsetX;
    float py = offsetY;
    float pz = offsetZ;
    const Matrix4x4f& gm = preRotationAxes;
    worldMat.m[3][0] += px * gm.m[0][0] + py * gm.m[1][0] + pz * gm.m[2][0];
    worldMat.m[3][1] += px * gm.m[0][1] + py * gm.m[1][1] + pz * gm.m[2][1];
    worldMat.m[3][2] += px * gm.m[0][2] + py * gm.m[1][2] + pz * gm.m[2][2];
}

bool ProjectAimToViewTangents(const Matrix4x4f& clean, const Matrix4x4f& head,
                              float aimDist, float& tanRight, float& tanUp) {
    float aimPtX = clean.m[3][0] + aimDist * clean.m[2][0];
    float aimPtY = clean.m[3][1] + aimDist * clean.m[2][1];
    float aimPtZ = clean.m[3][2] + aimDist * clean.m[2][2];

    float dx = aimPtX - head.m[3][0];
    float dy = aimPtY - head.m[3][1];
    float dz = aimPtZ - head.m[3][2];

    float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

    if (vz <= 1e-4f) return false;

    tanRight = vx / vz;
    tanUp = vy / vz;
    return true;
}

} // namespace RE2HT
