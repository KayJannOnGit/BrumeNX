// The room: near black, a blue FOG (not a halo) lit from behind, a few soft
// drifting lights and fine dust. Drawn into a SMALL target (see kHazeW/H): fog
// is soft, so bilinear upscaling loses nothing, and the budget here is big
// (~1/20 of the pixels of a full-screen pass) - so this pass can afford rich noise.
//
// What makes it read as fog rather than a glow:
//  - domain-warped noise (the coordinates are pushed around by another noise)
//    gives wisps and curls instead of round blobs;
//  - two layers at different scales and speeds (parallax: the near one drifts faster);
//  - the outline is ragged (the mask is eaten by the noise), never a clean disc;
//  - light comes from BEHIND: thin parts glow, thick wisps are darker on their core
//    and bright on their edges.
// v81 - the themes (common.glsl uThA..uThD, src/Theme.cpp): the fog's density, pace, curl, the lights' number, pace and
// strength, the sparkle, the room's darkness; and the theme Chrome: LIQUID CHROME instead of the fog (computed only where
// it is: it floods out from the console, both looks only along its front).
layout (location = 0) in vec2 vUV;
layout (location = 0) out vec4 outColor;

float H2(vec2 p) {                       // cheap hash, no sin (precision-safe)
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float N2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(H2(i), H2(i + vec2(1.0, 0.0)), f.x),
               mix(H2(i + vec2(0.0, 1.0)), H2(i + vec2(1.0, 1.0)), f.x), f.y);
}
const mat2 kRot = mat2(0.80, 0.60, -0.60, 0.80);
float Fbm(vec2 p) {                      // 5 octaves, rotated each time (no grid look)
    float a = 0.5, s = 0.0;
    for (int k = 0; k < 5; k++) { s += a * N2(p); p = kRot * p * 2.02 + vec2(11.3, 7.1); a *= 0.5; }
    return s;
}
// One fog layer: warped fbm, 0..1 density. v81: warp = the curl (1.9 = Brume; Nuit profonde smoother)
float Fog(vec2 q, float t, float speed, float warp) {
    vec2 w = vec2(Fbm(q + vec2(0.0, t * 0.05 * speed)),
                  Fbm(q + vec2(5.2, 1.3) - vec2(t * 0.04 * speed, 0.0)));
    return Fbm(q + warp * w + vec2(t * 0.02 * speed, -t * 0.012 * speed));
}

// v52 - the XMB wave: Wave() and XmbWave() live in common.glsl (v73: the icons' glass shows it too)

// v81 - the theme Chrome: LIQUID CHROME instead of the fog. A slow liquid-metal surface: its height is a few sines that
// fold into each other (no noise: ~10 sines a sample), A's ripple runs on it; its slope (3 samples) bends the view, and
// the metal shows the studio (common.glsl ChromeEnv). Mercury drops float over it, merge and split as their paths cross,
// and lean in the wheel's wind; the drifting lights are seen IN the metal (bent by its waves), the dust becomes glints.
float LiquidH(vec2 q, float t) {
    for (int i = 1; i < 5; i++) {
        float fi = float(i);
        q += vec2(0.70 / fi * sin(fi * 1.15 * q.y + t * 0.21 + fi * 0.8),
                  0.60 / fi * sin(fi * 0.95 * q.x - t * 0.17 + fi * 2.1));
    }
    return sin(q.x) * cos(q.y);
}
float Ripple(vec2 p, float age) {                    // A pressed on the wheel: a ring runs out from the console, fading
    if (age <= 0.0) return 0.0;
    float d = length(p - vec2(640.0, 400.0)), R = age * 240.0, x = (d - R) / 60.0;
    return 0.42 * exp(-age * 0.9) * exp(-x * x) * sin((d - R) * 0.05);
}
vec3 LiquidChrome(vec2 p, float t, float S, vec3 A, float glints, vec4 lights) {
    vec2  q = p / 175.0;
    float age = uThC.w, e = 0.06;
    float h0 = LiquidH(q, t) + Ripple(p, age);
    float hx = LiquidH(q + vec2(e, 0.0), t) + Ripple(p + vec2(e * 175.0, 0.0), age);
    float hy = LiquidH(q + vec2(0.0, e), t) + Ripple(p + vec2(0.0, e * 175.0), age);
    vec2  g = vec2(hx - h0, hy - h0) / e;                                  // the slope (y down)
    vec3  n = normalize(vec3(-g.x * 0.60, g.y * 0.60, -1.0));              // towards the viewer
    vec3  v = normalize(vec3((p.x - 640.0) / 900.0, (360.0 - p.y) / 900.0, 1.0));
    vec3  r = reflect(v, n);
    float fr = 0.62 + 0.38 * pow(clamp(1.0 - dot(-v, n), 0.0, 1.0), 5.0);  // metal: much light even face-on (v86: never pow of < 0)
    vec3  col = ChromeEnv(r, A) * fr;
    col *= 0.80 + 0.35 * smoothstep(-0.8, 0.9, h0);                        // crests catch more, troughs sink
    // the drifting lights, seen in the metal: bent by the waves (their place moves with the slope)
    vec2 pr = p + n.xy * vec2(-260.0, 260.0);
    for (int k = 0; k < 6; k++) {
        float wk = clamp(lights.x - float(k), 0.0, 1.0);
        if (wk <= 0.0) break;
        float fk = float(k);
        vec2 m = vec2(640.0 + 470.0 * sin(lights.y * (0.045 + 0.017 * fk) + fk * 2.1),
                      330.0 + 190.0 * sin(lights.y * (0.063 + 0.013 * fk) + fk * 4.0));
        float dd = length(pr - m) / (80.0 + 22.0 * fk);
        float m3 = mod(fk, 3.0);
        vec3 mc = m3 < 0.5 ? vec3(0.62, 0.92, 1.0) : (m3 < 1.5 ? vec3(0.92, 0.94, 1.0) : vec3(0.78, 0.72, 1.0));
        col += mc * exp(-dd * dd) * 0.20 * lights.z * wk;
    }
    // mercury drops: four, on slow crossing paths (they merge, they split), leaning in the wind
    float F = 0.0; vec2 G = vec2(0.0);
    for (int k = 0; k < 4; k++) {
        float fk = float(k);
        vec2 c = vec2(640.0 + (330.0 + 60.0 * fk) * sin(t * (0.050 + 0.011 * fk) + fk * 1.7) + uThC.z * (40.0 + 25.0 * fk),
                      345.0 + (100.0 + 25.0 * fk) * sin(t * (0.071 + 0.009 * fk) + fk * 2.9));   // (never over the top bar)
        float rr = 26.0 + 9.0 * fk;
        vec2 dv = p - c; float d2 = dot(dv, dv) + 1.0;
        F += rr * rr / d2;
        G += -2.0 * rr * rr * dv / (d2 * d2);
    }
    float cover = smoothstep(0.92, 1.08, F);
    col *= 1.0 - 0.45 * smoothstep(0.30, 1.0, F) * (1.0 - cover);         // they sit in the liquid: a soft dark rim
    if (cover > 0.0) {
        vec2  gn = length(G) > 1e-6 ? normalize(G) : vec2(0.0);
        vec2  nxy = gn * vec2(-1.0, 1.0) * min(inversesqrt(max(F, 1.0)), 0.98);   // a ball: outwards at the rim, facing us in the middle
        vec3  nd = vec3(nxy, -sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
        vec3  rd = reflect(v, nd);
        float frd = 0.70 + 0.30 * pow(clamp(1.0 - dot(-v, nd), 0.0, 1.0), 5.0);
        vec3  dc = ChromeEnv(rd, A) * frd * 1.7 + vec3(0.05, 0.06, 0.08);
        dc += vec3(0.95, 0.97, 1.0) * pow(max(dot(rd, normalize(vec3(-0.45, 0.55, -0.70))), 0.0), 60.0) * 1.2;   // its sharp light
        col = mix(col, dc, cover);
    }
    col += vec3(0.85, 0.92, 1.0) * glints;                                 // the dust: glints on the metal
    vec2  bd = (p - vec2(640.0, 360.0)) / vec2(760.0, 470.0);
    float body = exp(-dot(bd, bd) * 1.6);
    return col * (0.55 + 0.45 * body) * clamp(S / 0.84, 0.0, 1.6);
}

void main() {
    vec2  p = vUV * vec2(1280.0, 720.0);
    float t = uScreen.z;
    // Strength: the theme value x the lock screen steps (mockup: sky .25 + .35 wake + .5 flash).
    float S = uHaze.y * (0.25 + 0.35 * uOrigin.w + 0.5 * uOrigin.z) / 0.6;
    vec3  A = uAmbient.rgb;
    vec4  thA = uThA, thB = uThB, thC = uThC;
    float chrome = clamp(thA.x, 0.0, 1.0);
    if (chrome > 0.0 && chrome < 1.0) {   // v81: the liquid chrome floods out from the console (and drains back): a soft,
        vec2  dc = p - vec2(640.0, 400.0);   // wavy front - only the pixels near it compute both looks (the GPU stays even)
        float dd = length(dc) + 36.0 * sin(atan(dc.y, dc.x) * 5.0 + t * 0.7);
        chrome = smoothstep(0.0, 1.0, (chrome * 960.0 - dd) / 160.0);
    }
    float ft = t + thA.z, lt = t + thB.y;                              // v81: the fog's and the lights' clocks (their pace)
    float nL = 3.0 + thB.x;                                            // v81: how many drifting lights

    // Fine dust: sparse specks on a drifting grid (something for the glass to bend). v81: the theme's sparkle
    vec2  g  = p / 46.0 + vec2(t * 0.35, t * 0.12);
    vec2  gi = floor(g), gf = fract(g) - 0.5;
    float h  = H2(gi);
    vec2  o  = (vec2(H2(gi + 7.0), H2(gi + 13.0)) - 0.5) * 0.7;
    float dust = step(0.82 - 0.06 * max(thB.w, 0.0), h) * exp(-dot(gf - o, gf - o) * 90.0) * (0.5 + 0.5 * sin(t * 1.3 + h * 40.0));
    dust *= max(1.0 + thB.w, 0.0);

    vec3 col = vec3(0.0);
    if (chrome < 0.999) {
        vec2  c = vec2(640.0, uHaze.x);
        vec2  d = (p - c) / vec2(640.0, 330.0);
        float breath = 0.85 + 0.15 * sin(ft * 0.42);
        float sc = 1.0 + 0.25 * thA.w, warp = 1.9 * (1.0 + thA.w);   // v81: Nuit profonde - bigger, smoother clouds

        // Two layers: far (big, slow) and near (finer, faster, thinner).
        float far  = Fog(p * (0.0026 * sc), ft, 1.0, warp);
        float near = Fog(p * (0.0052 * sc) + vec2(3.7, 9.1), ft, 2.2, warp);

        // Where the fog lives: a wide soft body, outline eaten by the noise. v81: the density (theme, hour, battery)
        float dn = thA.y;
        float body = exp(-(d.x * d.x * 1.1 + d.y * d.y * 2.6) / (1.0 + 0.6 * max(dn, 0.0)));
        float dens = smoothstep(0.30 - 0.28 * dn, 0.85 - 0.28 * dn, far * (0.55 + 0.75 * body)) * body;
        float densN = smoothstep(0.45 - 0.28 * dn, 0.95 - 0.28 * dn, near) * body * 0.6;
        float total = clamp(dens + densN, 0.0, 1.0);

        // Backlight: a soft light behind the middle, seen through the fog.
        float glow = exp(-dot(d, d) * 3.2) * breath * (1.0 - 0.35 * thC.x);
        // v29: the ambient colour (Réglages > Brume > Couleur d'ambiance; default (.22, .42, .95) = the old blue)
        vec3 fogCol   = A * vec3(0.227, 0.262, 0.316);
        vec3 lightCol = A;
        vec3 colF = mix(vec3(0.006, 0.008, 0.020), vec3(0.020, 0.010, 0.040), smoothstep(0.0, 720.0, p.y)) * (1.0 - 0.7 * thC.x);
        colF += fogCol * total * S;                                    // the fog itself, dim
        colF += lightCol * glow * (0.35 + 0.65 * total) * (1.0 - 0.45 * dens) * S * 0.40;  // light through it (v29: .55 -> .40, darker)
        colF += lightCol * glow * smoothstep(0.15, 0.0, abs(dens - 0.35)) * 0.09 * S;     // bright wisp edges

        // A few soft lights drifting through (never a beam). v81: how many, their pace, their strength (the theme)
        for (int k = 0; k < 6; k++) {
            float wk = clamp(nL - float(k), 0.0, 1.0);
            if (wk <= 0.0) break;
            float fk = float(k);
            vec2 m = vec2(640.0 + 430.0 * sin(lt * (0.045 + 0.017 * fk) + fk * 2.1),
                          uHaze.x + 170.0 * sin(lt * (0.063 + 0.013 * fk) + fk * 4.0));
            float dd = length(p - m) / (110.0 + 30.0 * fk);
            vec3 mc = k == 0 ? A * vec3(1.0, 0.81, 1.0) : (mod(fk, 2.0) > 0.5 ? mix(A, A.zxy, 0.35) : mix(A, A.yzx, 0.35));   // v29: from the ambient colour
            colF += mc * exp(-dd * dd) * (0.03 + 0.05 * total) * S * (1.0 + thB.z) * wk;
        }
        colF += vec3(0.55, 0.70, 1.0) * dust * (0.10 + 0.25 * body);
        col = colF;
    }
    if (chrome > 0.001) {
        vec3 colC = LiquidChrome(p, ft, S, A, dust * 0.5, vec4(nL, lt, 1.0 + thB.z, 0.0));
        col = mix(col, colC, chrome);
    }

    vec2 v = (p - vec2(640.0, 360.0)) / vec2(640.0, 360.0);
    col *= 1.0 - (0.40 + 0.28 * thC.x) * smoothstep(0.45, 1.25, length(v));   // v81: Nuit profonde - deeper edges
    if (uHaze.w > 0.001) {                                             // v52: the XMB wave, with the light
        vec3 wv = XmbWave(p, t);
        col += wv * uHaze.w * (0.25 + 0.75 * clamp(uLight.x, 0.0, 1.0));
    }
    col *= uAmbient.w > 0.01 ? uAmbient.w : 1.0;                       // v47: the theme's light (Réglages > Brume > Thème)
    outColor = vec4(col, 1.0);
}
