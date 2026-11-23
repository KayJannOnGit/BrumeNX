// Full screen: the small haze upscaled (one bilinear fetch) and up to three
// cubes of water, like the PS2 / PSBBN boot: PERFECT cubes (sharp corners) with
// no line drawn anywhere. A cube is only seen through what it does to the light:
//  - each face catches the scene's light differently (top lighter, sides darker,
//    the one facing the light glows) - that is what shows the shape;
//  - the light is followed through the water: bent on the way in, across, bent
//    on the way out (or bounced inside by total internal reflection), so what is
//    behind shows magnified, shifted, flipped;
//  - Fresnel reflections of soft lights at grazing angles, a glint;
//  - it borrows the light around it, so in the dark it nearly disappears.
layout (location = 0) in vec2 vUV;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D uHazeTex;

const float kIor = 1.33;   // water

// textureLod: no derivatives needed, so lookups inside loops / branches stay cheap
// (implicit-LOD fetches in divergent code force compilers to unroll everything).
vec3 Haze(vec2 p) { return textureLod(uHazeTex, clamp(p / vec2(1280.0, 720.0), 0.0, 1.0), 0.0).rgb; }
float Ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
vec3 QRot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
float Luma(vec3 c) { return dot(c, vec3(0.30, 0.55, 0.15)); }

// Ray / cube in the cube's own frame, half size 1. Returns near t (< 0 = miss);
// e = entry point, n = entry face normal (both in the cube's frame).
float Box(vec3 o, vec3 d, out vec3 e, out vec3 n) {
    e = vec3(0.0); n = vec3(0.0, 0.0, -1.0);
    vec3 m = 1.0 / (d + vec3(1e-6));
    vec3 k = abs(m);
    vec3 s = m * o;
    vec3 t1 = -s - k, t2 = -s + k;
    float tN = max(max(t1.x, t1.y), t1.z), tF = min(min(t2.x, t2.y), t2.z);
    if (tN > tF || tN < 0.0) return -1.0;
    n = -sign(d) * step(t1.yzx, t1.xyz) * step(t1.zxy, t1.xyz);
    e = o + d * tN;
    return tN;
}
// From inside the cube, along d: distance to the wall it leaves through, and that wall's normal.
float Exit(vec3 p, vec3 d, out vec3 n) {
    vec3 m = 1.0 / (d + vec3(1e-6));
    vec3 t = (sign(d) - p) * m;
    float tX = min(t.x, min(t.y, t.z));
    n = sign(d) * step(t, t.yzx + 1e-6) * step(t, t.zxy + 1e-6);
    return max(tX, 0.0);
}

// Soft lights for the reflections (neutral, they read as « glossy water »).
vec3 Studio(vec3 r, float la) {
    float top  = smoothstep(0.55, 0.90, r.y) * 0.55;
    vec3  sd   = normalize(vec3(cos(la), 0.20, sin(la) - 0.4));
    float side = smoothstep(0.80, 0.97, dot(r, sd)) * 0.45;
    return vec3(0.75, 0.85, 1.0) * (top + side);
}

// The lock screen's light, exactly the mockup's lightPass: returns the light level
// at px and the « air bends » offset of the shock fronts passing by.
float LightAt(vec2 px, out vec2 off, out float fl) {
    float lv = uLight.x;
    fl = 0.0; off = vec2(0.0);
    // v90: no blast and full light (the wheel, the pages): exactly lv (the loop and the button's shade add nothing then)
    if (uB[0].w == 0.0 && uB[1].w == 0.0 && uB[2].w == 0.0 && uB[3].w == 0.0 && lv >= 1.0) return lv;
    for (int i = 0; i < 4; i++) {
        vec4 b = uB[i];
        if (b.w == 0.0) continue;
        vec2 dv = px - uBO[i].xy;
        float d = length(dv) + 1e-3;
        float ins = 1.0 - smoothstep(b.x - uLight.y, b.x, d);
        if (b.w > 1.5) {} else if (b.w > 0.0) lv = mix(lv, b.y, ins); else lv = mix(b.y, lv, ins);
        float x = (d - b.x) / (d < b.x ? uLight.z * 2.6 : uLight.z);
        fl += b.z * exp(-x * x);                                         // sharp front, longer tail behind
        float xs = (d - b.x) / uLight.z;
        off += (dv / d) * (-2.0 * xs * exp(-xs * xs)) * b.z * uLight.z * 0.22;   // the air bends as the front passes
    }
    float fromBtn = 1.0 - 0.5 * (1.0 - clamp(lv, 0.0, 1.0)) * smoothstep(0.12, 1.0, length(px - uOrigin.xy) / uLight.w);
    return lv * fromBtn + fl * 1.7;
}

void main() {
#ifdef FRONT
    {   // v29 second pass (built as glassfront_fsh): leave at once where no cube can be
        vec2 q = vUV * vec2(1280.0, 720.0);
        vec3 r = normalize(vec3((q.x - 640.0) / 900.0, (360.0 - q.y) / 900.0, 1.0));
        bool any = false;
        for (int k = 0; k < 6; k++) {
            vec4 c = uCube[k];
            if (c.w <= 0.0) continue;
            float b = dot(r, c.xyz);
            if (b > 0.0 && dot(c.xyz, c.xyz) - b * b <= c.w * c.w * 3.2) { any = true; break; }
        }
        if (!any) discard;
    }
#endif
    vec2 off; float fl;
    float light = LightAt(vUV * vec2(1280.0, 720.0), off, fl);
    vec2 p = vUV * vec2(1280.0, 720.0) - off;
    vec3 rd = normalize(vec3((p.x - 640.0) / 900.0, (360.0 - p.y) / 900.0, 1.0));
    vec3 room = Haze(p);
    vec3 col = room;

    // Nearest cube (a bounding-sphere test first: most pixels stop there).
    float best = 1e9; int hitK = -1;
    vec3 bo = vec3(0.0), bd = vec3(0.0, 0.0, 1.0), be = vec3(0.0), bn = vec3(0.0);
#ifndef BG                                                   // v90: glassbg_fsh = the room only (the cubes come after, in their squares)
    for (int k = 0; k < 6; k++) {
        vec4 c = uCube[k];
        if (c.w <= 0.0) continue;
        float b = dot(rd, c.xyz);
        if (dot(c.xyz, c.xyz) - b * b > c.w * c.w * 3.0) continue;
        vec4 qi = vec4(-uRot[k].xyz, uRot[k].w);
        vec3 o = QRot(qi, -c.xyz) / c.w, d = QRot(qi, rd), e, n;
        float t = Box(o, d, e, n);
        if (t >= 0.0 && t * c.w < best) { best = t * c.w; hitK = k; bo = o; bd = d; be = e; bn = n; }
    }
#endif
#ifdef CUBES
    if (hitK < 0) discard;                                   // v90: glasscube_fsh - only the cube pixels (the room is there already)
#endif

    float hitDepth = 0.0;
    if (hitK >= 0) {
        vec4 q = uRot[hitK];
        vec3 nW = QRot(q, bn);
        float cosI = clamp(dot(-bd, bn), 0.0, 1.0);   // (v86 revue: never > 1 - pow(1 - cosI, n) of a negative = NaN)
        float fres = 0.02 + 0.98 * pow(1.0 - cosI, 5.0);

        // In, across, out (one extra bounce if the light is trapped inside).
        vec3 dIn = refract(bd, bn, 1.0 / kIor);
        vec3 nX;
        float thick = Exit(be, dIn, nX);
        vec3 pX = be + dIn * thick;
        vec3 dOut = refract(dIn, -nX, kIor);
        float trapped = 0.0;
        if (dot(dOut, dOut) < 1e-4) {
            trapped = 1.0;
            vec3 d2 = reflect(dIn, -nX), n2;
            float t2 = Exit(pX + d2 * 0.001, d2, n2);
            thick += t2;
            dOut = refract(d2, -n2, kIor);
            if (dot(dOut, dOut) < 1e-4) dOut = d2;
        }
        vec3 outW = QRot(q, dOut);

        // What is seen through: the room, displaced by the two bends (bounded so
        // it stays in the fog), red / blue a hair apart (dispersion).
        vec2 dv = (outW.xy / max(outW.z, 0.25) - rd.xy / rd.z) * vec2(1.0, -1.0) * 760.0;   // v29.1: 480 -> 760 (Yass: not enough distortion)
        dv *= min(1.0, 430.0 / max(length(dv), 1e-3));
        vec3 through = vec3(Haze(p + dv * 0.97).r, Haze(p + dv).g, Haze(p + dv * 1.04).b);

        // Each face catches the light differently: this is what draws the cube.
        // It scales with the light around it (in the dark the cube nearly vanishes).
        float around = clamp(Luma(room) * 6.0, 0.08, 1.0);
        vec3  key = normalize(vec3(-0.45, 0.70, -0.55));               // soft light, upper left, front
        float face = 0.10 + 0.30 * max(nW.y, 0.0) + 0.28 * max(dot(nW, key), 0.0) + 0.06 * max(-nW.z, 0.0);
        vec3  water = vec3(0.30, 0.52, 1.0) * face * around;            // light caught by the faces
        water += vec3(0.20, 0.38, 0.85) * (thick * 0.06 + trapped * 0.10) * around;  // light caught inside
        water += vec3(0.35, 0.55, 1.0) * uCubeFx[hitK].x * 0.45;          // lit up when the light front touches it
        water = mix(water, vec3(1.0, 0.72, 0.30) * (face * 1.4 + 0.08), uCubeFx[hitK].z * 0.8);   // v44: the golden cube

        vec3 rl  = reflect(rd, nW);
        vec3 env = Studio(rl, uHaze.z) * (0.35 + 0.65 * around) + Haze(p - rl.xy * vec2(-260.0, 260.0)) * 0.7;
        vec3 L   = normalize(vec3(cos(uHaze.z) * 0.6, 0.55, -0.6 + sin(uHaze.z) * 0.3));
        float glint = pow(max(dot(rl, L), 0.0), 140.0) * 1.1 * (0.3 + 0.7 * around);

        // v81 - the themes: the reflections' strength (uThC.y: Nuit profonde dimmer), and Chrome = POLISHED CHROME cubes:
        // no water any more, each face a flat mirror of the studio (common.glsl ChromeEnv) and of the liquid chrome behind
        // (the room, read once more), a crisp highlight, the edges whiter at grazing angles
        float chromeK = clamp(uThA.x, 0.0, 1.0), rk = max(1.0 + uThC.y * (1.0 - chromeK), 0.0);
        col = mix(through * 1.04 + water, env, clamp(fres * rk, 0.0, 1.0)) + vec3(0.9, 0.95, 1.0) * glint * rk;
        if (chromeK > 0.001) {
            float frc = 0.60 + 0.40 * pow(1.0 - cosI, 3.0);
            vec3 metal = (ChromeEnv(rl, uAmbient.rgb) * 1.5 + Haze(p - rl.xy * vec2(-260.0, 260.0)) * 0.85) * frc;
            metal *= 0.75 + 0.5 * face;                                         // the faces still read (top lighter)
            metal += vec3(0.95, 0.97, 1.0) * (pow(max(dot(rl, L), 0.0), 220.0) * 2.2 + glint * 0.8);
            col = mix(col, metal * mix(0.35, 1.0, around), chromeK);
        }
        // v29 - help the eye read the depth (Yass): the far cubes sink into the fog (their light fades into the room),
        // the near ones catch a little more light (moodboard: the PS2 boot, near cubes big and soft, far ones dim)
        col = mix(col, room, smoothstep(9.0, 23.0, best) * 0.75);
        col += (col - room) * 0.25 * (1.0 - smoothstep(3.0, 7.0, best));
        hitDepth = rd.z * best;                                  // view depth of the hit (the cubes' z = the camera's)
    }

    col = col * light + vec3(0.498, 0.596, 1.0) * fl * 0.05;   // tint #7f98ff on the front
    // v28 - the big bang (Yass: « a real visual effect »): while the final flash lasts, rays of light shoot out of the
    // button across the screen and a bright core opens and fades (uOrigin.z = the flash, .85 -> 0 in ~1.4 s)
    float fb = uOrigin.z;
    if (fb > 0.001) {
        vec2 dv = p - uOrigin.xy;
        float r = length(dv) + 1.0, an = atan(dv.y, dv.x);
        float rays = pow(abs(sin(an * 9.0 + 1.3)), 24.0) + 0.7 * pow(abs(sin(an * 14.0 - 0.7)), 40.0);
        float reach = (1.0 - fb / 0.85) * 1400.0 + 120.0;          // the rays run outwards as the flash fades
        float g = rays * smoothstep(reach, reach * 0.25, r) * fb * fb;
        col += vec3(0.62, 0.72, 1.0) * (g * 0.55 + fb * fb * 0.9 * exp(-r * r / (90000.0 * (1.2 - fb))));
    }
    col += (Ign(p + fract(uScreen.z) * 61.0) - 0.5) / 255.0;   // dither: no banding in the dark
#ifdef FRONT
    {
        // v29 - second pass, after the consoles (Yass: a cube in front passed behind the consoles at the back): only
        // the cube pixels, at their real depth (the wheel's projection: near .3, far 300); the depth test keeps those
        // nearer than the console there, the depth bounds test skips the background. The cube is laid over the
        // console like glass in front of it (the console still shows through).
        if (hitK < 0) discard;
        gl_FragDepth = clamp(1.001001 - 0.3003003 / max(hitDepth, 0.31), 0.0, 1.0);
        float op = mix(0.62, 1.0, clamp(uThA.x, 0.0, 1.0));     // v81: Chrome cubes are metal (opaque)
        outColor = vec4(col * op, op);                          // premultiplied: 62 % glass over the console
        return;
    }
#else
    outColor = vec4(col, 1.0);
#endif
}
