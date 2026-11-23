// v74 - the settings icons of THICK LIQUID GLASS, the PS3 XMB's (reports/Icônes verre liquide XMB PS3.md), in their own
// program. v73 had them as mode 29 of ui_fsh: that tripled the registers of EVERY interface pixel (25 -> 68, the text
// ran at under half speed on the console: interface 1.2 -> 3.3-6.7 ms). Drawn with ui_vsh, only for these quads
// (Ui::Flush switches program around them). Sony painted that glass into each icon; here it is computed, so the light
// moves: the shape's distance to its edge (blue, tools/ui_sdf.py: b = .5 - d/16 in drawing px, < 0 inside) gives the
// glass a rounded thickness, its edges light up on the light's side (never an even outline), a wet crescent sits under
// the top edges, the body lets the fog through bent by its lens, a glow gathers on the far side.
// v74, cheaper than v73: 4 reads (the crescent comes from the edge's direction, no 5th read), no XMB wave recomputed per
// pixel (the fog already holds it, blurred), and the costly layers (glint, rainbow, inner light) only on the chosen icon.
// p1, p2 = the sprite's atlas v range; p3 = how chosen (0..1, continuous)
// vColor: r = menu px per drawing px, g = a flash of light inside (A), b = the icon's own phase (glints), a = opacity
layout (location = 0) in vec2 vPos;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec4 vColor;
layout (location = 3) in vec4 vParam;
layout (location = 0) out vec4 outColor;
layout (binding = 1) uniform sampler2D uUiAtlas;   // the interface sheet (tools/bake-assets.html), its blue = the distance
layout (binding = 2) uniform sampler2D uHazeT;     // the blurred fog: what the glass shows behind it

void main() {
    vec2 ts = 1.0 / vec2(textureSize(uUiAtlas, 0));    // 1 texel = 1 drawing px (only the 720p sheet is kept)
    float b0 = textureLod(uUiAtlas, vUV, 0.0).b;
    float d = (0.5 - b0) * 16.0;
    float k = max(vColor.r, 0.25) * uScreen.x / 1280.0; // screen px per drawing px: a sharp edge without fwidth
    float cov = clamp(0.5 - d * k, 0.0, 1.0);
    if (cov <= 0.0) { outColor = vec4(0.0); return; }
    float f = clamp(vParam.w, 0.0, 1.0), ap = 0.35 + 0.65 * clamp(uGlass.w, 0.0, 1.0);
    vec2 L = dot(uGlass.xy, uGlass.xy) > 0.01 ? normalize(uGlass.xy) : vec2(-0.6, -0.8);   // towards the light
    // the edge's direction (outwards) from two more reads of the distance
    vec2 g = vec2(b0 - textureLod(uUiAtlas, vUV + vec2(ts.x, 0.0), 0.0).b, b0 - textureLod(uUiAtlas, vUV + vec2(0.0, ts.y), 0.0).b);
    g *= inversesqrt(max(dot(g, g), 1e-10));
    // the thickness: a bezel 3 px wide, flat on top; thin strokes become tubes
    float t = clamp(-d / 3.0, 0.0, 1.0), u = 1.0 - t, u2 = u * u;
    float lg = dot(g, L), lit = max(lg, 0.0), far = max(-lg, 0.0);
    float edge = smoothstep(0.55, 1.0, u) * (0.35 + 0.65 * u2 * u2);                   // Fresnel-like: brightest on the rim
    float rim = edge * (pow(lit, mix(3.5, 2.2, f)) + 0.12 * pow(far, 2.5)) * ap;      // the light's side, a little the other
    // the wet crescent under the edges that face the light (where the nearest edge looks towards the light)
    float gloss = smoothstep(0.9, 1.6, -d) * (1.0 - smoothstep(3.2, 4.4, -d)) * smoothstep(0.30, 0.80, lg) * 0.34;
    // the body: the fog behind, bent inwards by the rounded edges (a lens), pale like the mockup's .gi (brightness 1.3,
    // saturate ~.45); the lens is stronger when the XMB wave is in the fog to bend
    float lens = (1.0 - sqrt(max(1.0 - u2, 0.0))) * 15.0 * max(vColor.r, 0.25) * uGlass.z * (0.35 + 0.65 * uHaze.w) * ap;
    vec3 back = textureLod(uHazeT, (vPos - g * lens) / vec2(1280.0, 720.0), 0.0).rgb;
    back = mix(vec3(dot(back, vec3(0.2126, 0.7152, 0.0722))), back, 0.45) * 1.3;
    // the mockup's white veil (linear-gradient 168deg .30 / .12 / .05 / .13), x1.33 when chosen
    float vt = clamp((vUV.y - vParam.y) / max(vParam.z - vParam.y, 1e-5), 0.0, 1.0), s = 1.0 + 0.33 * f;
    float ga = (vt < 0.4 ? mix(0.30, 0.12, vt / 0.4) : vt < 0.64 ? mix(0.12, 0.05, (vt - 0.4) / 0.24) : mix(0.05, 0.13, (vt - 0.64) / 0.36)) * s;
    vec3 c = mix(back, vec3(0.90, 0.93, 1.0), 0.20 + ga * 1.15) * mix(0.93, 1.0, u2);   // silvery; darker core: thickness
    vec3 amb = mix(uAmbient.rgb, vec3(1.0), 0.35);
    c += amb * (smoothstep(0.05, 0.45, u) * (1.0 - smoothstep(0.80, 1.0, u)) * far * far * 0.30 * ap);   // light on the far side
    c = mix(c, vec3(1.0), min(rim * 0.78, 0.78));
    c += vec3(gloss * s);
    if (f > 0.01) {                                    // the chosen icon (and the one arriving / leaving): the costly layers
        c += amb * (f * (0.08 + 0.14 * u) * (0.86 + 0.14 * sin(uScreen.z * 2.2440)) + clamp(vColor.g, 0.0, 1.0) * (0.20 + 0.25 * u));   // lit from inside (2.8 s)
        // a glint that slides on the bezel (each icon its own beat): the bezel's normal (squircle profile)
        float u4 = u2 * u2, sl = u2 * u / max(pow(max(1.0 - u4, 0.0), 0.75), 0.06);
        vec3 N = normalize(vec3(g * sl * 0.55, 1.0));
        float ph = sin(uScreen.z * 0.45 + vColor.b * 6.2831853) * 0.35;
        vec2 Lg = vec2(L.x * cos(ph) - L.y * sin(ph), L.x * sin(ph) + L.y * cos(ph));
        float nh = max(dot(N, normalize(vec3(Lg * 0.75, 1.66))), 0.0), nh2 = nh * nh, nh8 = nh2 * nh2; nh8 *= nh8;
        c += vec3(0.95, 0.97, 1.0) * (nh8 * nh8 * nh8 * smoothstep(0.15, 0.6, u) * (0.55 + 0.45 * f) * ap);   // ~pow 24
        c += (0.5 + 0.5 * cos(6.2831853 * (u * 1.6 + vec3(0.0, 0.33, 0.67)))) * (u2 * u * (0.25 + lit) * 0.04 * ap * f);
    }
    float a = cov * mix(0.80, 1.0, clamp(rim + gloss * 2.0, 0.0, 1.0)) * vColor.a;   // the body lets things through
    outColor = vec4(c * a, a);
}
