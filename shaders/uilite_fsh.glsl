// v99 (perf, Yass: « focus optimisation maximale ») - the interface's SIMPLE quads, in a program of their own.
// The console measured ui_fsh at ~7 ms per full screen of pixels (GPU 307 MHz): the Discord QR's dark veil and plain
// rect (2.5 screens) took the Amis page from 6.4 to 25 ms. Every pixel of ui_fsh pays for its 20 modes (its code and
// registers), even a soft light. These modes are the ones that cover the most of the screen (the lights, the veils, the
// plain rects, the text, the stars), and here they are a few instructions each: the SAME maths as ui_fsh, value for value
// (keep both in step), so the picture does not change. Ui::Flush cuts the queue into runs (ui / verre / this one).
layout (location = 0) in vec2 vPos;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec4 vColor;
layout (location = 3) in vec4 vParam;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D uAtlas;

float RRect(vec2 p, vec2 sz, float r) { vec2 q = abs(p - sz * 0.5) - (sz * 0.5 - r); return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }

void main() {
    int mode = int(vParam.x + 0.5);
    vec4 o = vec4(0.0);
    if (mode == 27) {                                      // a soft light ADDED, no edge (ui_fsh mode 27)
        vec2 q = clamp(vUV / max(vParam.yz, vec2(1.0)), 0.0, 1.0);
        float k;
        if (vParam.w < 0.5) { float vy = 1.0 - abs(q.y * 2.0 - 1.0); k = sin(3.14159265 * q.x) * vy * vy; }
        else { vec2 c = q * 2.0 - 1.0; float d = clamp(1.0 - dot(c, c), 0.0, 1.0); k = d * d; }
        o = vec4(vColor.rgb * (k * vColor.a), 0.0);
    } else if (mode == 0 || mode == 3) {                   // text, and its glow (blurred atlas) (modes 0 / 3)
        float a = textureLod(uAtlas, vUV, 0.0).r * vColor.a;
        o = vec4(vColor.rgb * a, a);
    } else if (mode == 23) {                               // the soft dark veil (mode 23)
        vec2 sz = vParam.yz;
        float wx = vParam.w > 0.5 ? vParam.w / sz.x : 0.14, wy = vParam.w > 0.5 ? vParam.w / sz.y : 0.22;
        float fx = smoothstep(0.0, wx, vUV.x / sz.x) * smoothstep(0.0, wx, 1.0 - vUV.x / sz.x);
        float fy = smoothstep(0.0, wy, vUV.y / sz.y) * smoothstep(0.0, wy, 1.0 - vUV.y / sz.y);
        float a = min(0.92, 0.55 * fx * fy * vColor.a);
        o = vec4(vec3(0.016, 0.024, 0.094) * a, a);
    } else if (mode == 24) {                               // a plain rounded rect (mode 24)
        float a = (1.0 - smoothstep(-0.5, 0.5, RRect(vUV, vParam.yz, vParam.w))) * vColor.a;
        o = vec4(vColor.rgb * a, a);
    } else if (mode == 21) {                               // a star / a piece of a comet's trail, added (mode 21)
        if (vParam.y > 0.5) {
            float along = 1.0 - smoothstep(1.0 - vParam.z, 1.0, abs(vUV.x));
            o = vec4(vColor.rgb * exp(-vUV.y * vUV.y * 4.0) * along * vColor.a, 0.0);
        } else o = vec4(vColor.rgb * exp(-dot(vUV, vUV) * 4.0) * vColor.a, 0.0);
    } else if (mode == 28) {                               // a small solid triangle (mode 28)
        vec2 c = vUV;
        int dir = int(vParam.y + 0.5);
        if (dir == 1) c.x = -c.x; else if (dir == 2) c = vec2(c.y, c.x); else if (dir == 3) c = vec2(-c.y, c.x);
        float d = max(max(c.x - 0.7, abs(c.y) - (c.x + 1.0) * 0.5), -1.0 - c.x);
        float a = (1.0 - smoothstep(-vParam.z, vParam.z, d)) * vColor.a;
        o = vec4(vColor.rgb * a, a);
    }
    outColor = o;
}
