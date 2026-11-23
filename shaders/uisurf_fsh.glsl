// v99 (perf) - the interface's « surfaces » and the popups' backdrop, in a program of their own (see uilite_fsh.glsl:
// ui_fsh cost ~7 ms per full screen of pixels on the console, whatever the mode). A popup's backdrop covers the whole
// screen and a panel / a stack of notifications a third of it: here they pay only for their own maths. The SAME maths as
// ui_fsh's modes 25 and 30, value for value (keep both in step). Ui::Flush cuts the queue into runs.
layout (location = 0) in vec2 vPos;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec4 vColor;
layout (location = 3) in vec4 vParam;
layout (location = 0) out vec4 outColor;
layout (binding = 2) uniform sampler2D uHazeT;     // the blurred fog

// a « surface »'s box - its corners are superellipses (n = 4, a squircle)
float SqBox(vec2 p, vec2 sz, float r) {
    vec2 q = abs(p - sz * 0.5) - (sz * 0.5 - r), m = max(q, 0.0), m2 = m * m;
    return sqrt(sqrt(m2.x * m2.x + m2.y * m2.y)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    int mode = int(vParam.x + 0.5);
    vec4 o = vec4(0.0);
    if (mode == 30) {                                      // a « surface » (ui_fsh mode 30)
        float d = SqBox(vUV, vParam.yz, vParam.w);
        float inside = 1.0 - smoothstep(-1.0, 1.0, d);
        if (inside < 0.004) {                              // around it: the shadow (14 px lower, wide) and the halo (added)
            float sh = 0.55 * (1.0 - smoothstep(-16.0, 40.0, SqBox(vUV - vec2(0.0, 14.0), vParam.yz, vParam.w)));
            o = vec4(vColor.rgb * (0.13 * exp(-d / 20.0)), sh) * vColor.a;
        } else {
            vec3 c = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb * 0.16;
            vec2 f = clamp(vUV / vParam.yz, 0.0, 1.0);
            float e = max(-d, 0.0), side = sin(3.14159265 * f.x);
            float top = 0.32 * exp(-e * e / 3.5) * (1.0 - smoothstep(0.0, 0.32, f.y)) * side * side;
            float lit = 0.11 * exp(-(f.x - 0.5) * (f.x - 0.5) * 2.25 - (f.y + 0.05) * (f.y + 0.05) * 6.76) + 0.045 * exp(-e / 6.0) + 0.55 * top;
            c += mix(vec3(0.050, 0.066, 0.150), vec3(0.010, 0.013, 0.040), f.y) + vColor.rgb * lit + vec3(0.45 * top);
            c *= 1.0 - 0.30 * exp(-e / 10.0) * smoothstep(0.55, 1.0, f.y);
            c += (fract(sin(dot(vPos, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.008;
            o = mix(vec4(vColor.rgb * 0.13, 0.42), vec4(c * 0.94, 0.94), inside) * vColor.a;
        }
    } else if (mode == 25) {                               // a popup's frosted backdrop (ui_fsh mode 25)
        vec3 back = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb;
        float vig = 1.0 - 0.45 * clamp(length((vPos - vec2(640.0, 360.0)) / vec2(700.0, 420.0)), 0.0, 1.0);
        o = vec4(back * vColor.rgb * vig * vColor.a, vColor.a);
    }
    outColor = o;
}
