// Brume - small 3D helpers shared by what is drawn in the consoles' scene (v31: the game boxes beside the wheel).
// Column-major matrices like GLSL, the same conventions as Wheel.cpp (which keeps its own private copies).
#pragma once
#include <cmath>

namespace brume {
    namespace m3 {

        constexpr float kPi = 3.14159265f;

        struct M4 { float m[16]; };
        inline M4 Mul(const M4 &a, const M4 &b) {
            M4 r;
            for (int c = 0; c < 4; c++)
                for (int rr = 0; rr < 4; rr++) {
                    float s = 0;
                    for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
                    r.m[c * 4 + rr] = s;
                }
            return r;
        }
        inline M4 Ident() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
        inline M4 Translate(float x, float y, float z) { M4 r = Ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
        inline M4 Scale3(float x, float y, float z) { M4 r = Ident(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
        inline M4 RotX(float a) { M4 r = Ident(); r.m[5] = cosf(a); r.m[6] = sinf(a); r.m[9] = -sinf(a); r.m[10] = cosf(a); return r; }
        inline M4 RotY(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[2] = -sinf(a); r.m[8] = sinf(a); r.m[10] = cosf(a); return r; }
        inline M4 RotZ(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[1] = sinf(a); r.m[4] = -sinf(a); r.m[5] = cosf(a); return r; }
        inline void Xform(const M4 &m, const float *p, float *o) {
            for (int r = 0; r < 3; r++) o[r] = m.m[r] * p[0] + m.m[4 + r] * p[1] + m.m[8 + r] * p[2] + m.m[12 + r];
        }
        // PerspectiveCamera(fov, aspect): depth 0..1 (deko3d), right-handed, looking down -z
        inline M4 Persp(float fovDeg, float aspect, float n, float f) {
            M4 r{}; const float t = 1.0f / tanf(fovDeg * kPi / 360.0f);
            r.m[0] = t / aspect; r.m[5] = t; r.m[10] = f / (n - f); r.m[11] = -1; r.m[14] = n * f / (n - f);
            return r;
        }
        inline M4 LookAt(const float *e, const float *c) {   // camera.lookAt, up (0, 1, 0)
            float z[3] = { e[0] - c[0], e[1] - c[1], e[2] - c[2] };
            float l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
            for (float &v : z) v /= l;
            float x[3] = { z[2], 0, -z[0] }; l = sqrtf(x[0] * x[0] + x[2] * x[2]); x[0] /= l; x[2] /= l;
            float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
            M4 r = Ident();
            for (int i = 0; i < 3; i++) { r.m[i * 4 + 0] = x[i]; r.m[i * 4 + 1] = y[i]; r.m[i * 4 + 2] = z[i]; }
            r.m[12] = -(x[0] * e[0] + x[1] * e[1] + x[2] * e[2]);
            r.m[13] = -(y[0] * e[0] + y[1] * e[1] + y[2] * e[2]);
            r.m[14] = -(z[0] * e[0] + z[1] * e[1] + z[2] * e[2]);
            return r;
        }
        // world point -> menu pixels (1280 x 720); false if behind the camera
        inline bool Project(const M4 &vp, const float *p, float &sx, float &sy) {
            const float x = vp.m[0] * p[0] + vp.m[4] * p[1] + vp.m[8] * p[2] + vp.m[12];
            const float y = vp.m[1] * p[0] + vp.m[5] * p[1] + vp.m[9] * p[2] + vp.m[13];
            const float w = vp.m[3] * p[0] + vp.m[7] * p[1] + vp.m[11] * p[2] + vp.m[15];
            if (w < 0.01f) return false;
            sx = 640 + x / w * 640; sy = 360 - y / w * 360;
            return true;
        }

        // shaders/mesh_vsh.glsl's block (std140) - also what box_fsh.glsl reads
        struct alignas(16) MeshUbo {
            float mvp[16], model[16], base[4], mat[4], tint[4], emi[4], cam[4];
            float keyDir[4], keyCol[4], rimDir[4], rimCol[4], driftPos[4], driftCol[4], sky[4], ground[4], sh[9][4];
            float joyL[4], joyR[4];             // v40 (mesh_fsh: the Switch's Joy-Con colours)
        };
        static_assert(sizeof(MeshUbo) <= 0x200, "MeshUbo must fit the mesh UBO (Gpu.cpp kMeshUboSize)");

    } // namespace m3
} // namespace brume
