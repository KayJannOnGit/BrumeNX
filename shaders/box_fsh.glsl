// v31-v32 - the game boxes and what is inside them (Games.cpp, Shelf.cpp).
// Boxes = one unit box, each face dressed here:
//   front  = the official cover (BC1 sRGB, real proportions: the box is sized from the scan), under a clear film
//   spine  = made from the cover's own left edge (its colours), with the console's band where the real one has it
//   back   = the cover, dim and blurred;  sides = the case's plastic (DVD black, jewel clear, Switch red, cardboard)
//   v32: a box can open - part 1 = the tray (its front shows the inside), part 2 = the lid (its back is the inside)
// Media (v32) = what goes into the console: 5 a disc (the disc model: label side = the cover, data side = rainbow),
//   6 a cartridge (the box model: label = the cover), 7 a UMD (the box model: label front, the disc through a window).
// The same lights as the consoles (the wheel's block), but no ACES: a cover keeps its real colours.
// No arrays indexed in loops and no dFdx / dFdy (they fault the GPU in the HOME menu, see mesh_fsh.glsl).
layout (std140, binding = 1) uniform Mesh {
    mat4 uMVP;
    mat4 uModel;
    vec4 uBase;      // rgb: the plastic (linear); a: 1 + part (1 whole box, 2 tray, 3 lid)
    vec4 uMat;       // x: has a cover (v93: > 1.5 = a Switch game's real box, its width / height = x - 2), y: kind (0 DVD case, 1 jewel, 2 UMD case, 3 Switch, 4 cardboard; 5 disc,
                     // 6 cartridge, 7 UMD), z: boxes: chosen 0..1 / discs: 0 silver, 1 black (PS1), 2 blue (PS2),
                     // w: light (1 normal, .45 archived); < 0 = the reflection on the floor (strength = -w)
    vec4 uTint;      // x: boxes: Switch band (fraction of the height) / media: the cover's width / height;
                     // yzw: size w, h, d (world)
    vec4 uEmi;       // boxes: band colour (linear), w: the floor's height; media: xyz = its centre (world)
    vec4 uCam;       // camera position, w: ambient boost
    vec4 uKeyDir;
    vec4 uKeyCol;
    vec4 uRimDir;
    vec4 uRimCol;
    vec4 uDriftPos;
    vec4 uDriftCol;
    vec4 uSky;
    vec4 uGround;
    vec4 uSH[9];
};
layout (location = 0) in vec3 vW;
layout (location = 1) in vec3 vN;
layout (location = 2) in vec2 vUV;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D uTex;    // the cover (or the room light when there is none)
layout (binding = 1) uniform sampler2D uEnv;    // the room environment (see mesh_fsh.glsl)
layout (binding = 3) uniform sampler2D uTex2;   // v37: the album's sharp photo (faded in over the thumbnail)

#define PI 3.141592653589793

float EnvAt(vec3 d, float rough) {
    float lv = clamp(rough, 0.0, 1.0) * 5.0, l0 = floor(lv), l1 = min(l0 + 1.0, 5.0);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = clamp((asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5) * 32.0, 0.5, 31.5);
    float a = textureLod(uEnv, vec2(u, (l0 * 32.0 + v) / 192.0), 0.0).r;
    float b = textureLod(uEnv, vec2(u, (l1 * 32.0 + v) / 192.0), 0.0).r;
    return mix(a, b, lv - l0);
}
vec3 LinearToSRGB(vec3 c) { return mix(pow(c, vec3(0.41666)) * 1.055 - 0.055, c * 12.92, lessThanEqual(c, vec3(0.0031308))); }
vec3 Spectrum(float x) { return 0.5 + 0.5 * cos(6.2831853 * (x + vec3(0.0, 0.33, 0.67))); }

// a cover-less box: dark glass with a soft diagonal sheen (Games draws the title under it)
vec3 Blank(vec2 uv) {
    float g = 0.5 + 0.5 * sin((uv.x - uv.y) * 3.0);
    return mix(vec3(0.010, 0.014, 0.035), vec3(0.035, 0.050, 0.120), g * (1.0 - uv.y * 0.5));
}
// the diffuse light: hemisphere + the room (9 harmonics, grey) + the two lamps + the drifting light
vec3 Diffuse(vec3 N, out vec3 Lp, out float dist) {
    float shN = uSH[0].x * 0.282095
              + (uSH[1].x * N.y + uSH[2].x * N.z + uSH[3].x * N.x) * 0.488603
              + (uSH[4].x * (N.x * N.y) + uSH[5].x * (N.y * N.z) + uSH[7].x * (N.x * N.z)) * 1.092548
              + uSH[6].x * (0.315392 * (3.0 * N.z * N.z - 1.0)) + uSH[8].x * (0.546274 * (N.x * N.x - N.y * N.y));
    vec3 toP = uDriftPos.xyz - vW; dist = length(toP); Lp = toP / max(dist, 1e-4);
    return mix(uGround.rgb, uSky.rgb, 0.5 * N.y + 0.5) * 1.6
         + vec3(max(shN, 0.0)) * 0.55
         + uKeyCol.rgb * max(dot(N, uKeyDir.xyz), 0.0) * 0.55
         + uRimCol.rgb * max(dot(N, uRimDir.xyz), 0.0) * 0.12
         + uDriftCol.rgb * max(dot(N, Lp), 0.0) / max(dist * dist, 0.6) * 0.30;
}
// a rainbow of a disc's data side: the light diffracted along the tracks (strongest across the tracks, facing the light)
vec3 Rainbow(vec3 N, vec3 V, vec3 tg, float r, vec3 Lp) {
    float g1 = dot(normalize(uKeyDir.xyz + V), tg), g2 = dot(normalize(Lp + V), tg);
    return Spectrum(g1 * 2.4 + r * 0.7) * smoothstep(0.02, 0.32, abs(g1)) * (1.0 - smoothstep(0.55, 0.95, abs(g1)))
         + Spectrum(g2 * 2.4 + r * 0.7 + 0.3) * smoothstep(0.02, 0.32, abs(g2)) * (1.0 - smoothstep(0.55, 0.95, abs(g2))) * 0.6;
}
vec3 Finish(vec3 col) {   // highlights only are compressed (a cover's colours stay as printed)
    vec3 x = max(col - 0.8, 0.0);
    return min(col, vec3(0.8)) + x / (1.0 + x * 4.0);
}

void main() {
    float face = floor(vUV.x * 0.5);
    vec2 uv = vec2(vUV.x - 2.0 * face, vUV.y);
    int kind = int(uMat.y + 0.5);
    bool cover = uMat.x > 0.5;
    vec3 N = normalize(vN);
    vec3 V = normalize(uCam.xyz - vW);
    float facing = max(dot(N, V), 0.0);
    float W = uTint.y, H = uTint.z, D = uTint.w;

    // ======================= the media (v32) =======================
    if (kind >= 5) {
        bool mrefl = uMat.w < 0.0;                       // v37: a reflection on the floor (the album's prints)
        if (mrefl) N.y = -N.y;
        float lw = abs(uMat.w);
        vec3 Lp; float dist;
        vec3 diff = Diffuse(N, Lp, dist);
        float env = EnvAt(reflect(-V, N), 0.06);
        float spec = pow(max(dot(N, normalize(uKeyDir.xyz + V)), 0.0), 140.0) + pow(max(dot(N, normalize(Lp + V)), 0.0), 70.0) * 0.4;
        float fr = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
        vec3 col;
        if (kind == 5) {                                 // a disc (faces 4 label, 5 data, 6 edges)
            vec2 p = uv - 0.5;
            float r = length(p) * 2.0;                   // 0 centre .. 1 rim
            int variant = int(uMat.z + 0.5);
            if (face > 4.5 && face < 5.5) {              // the data side: mirror + rainbow
                vec3 rad = vW - uEmi.xyz; rad -= N * dot(rad, N);
                rad = rad / max(length(rad), 1e-5);
                vec3 tint = variant == 1 ? vec3(0.025) : (variant == 2 ? vec3(0.06, 0.08, 0.26) : vec3(0.55, 0.56, 0.60));
                col = tint * (0.25 + env * 1.1) + Rainbow(N, V, cross(N, rad), r, Lp) * (variant == 1 ? 0.30 : 0.85) + spec * 1.2;
                if (r < 0.34) col = vec3(0.18 + 0.45 * env) + spec;                     // the clear hub
                if (r > 0.975) col = vec3(0.5 + 0.35 * env);
            } else if (face < 4.5) {                     // the label: the cover's middle, round
                vec3 lab = cover ? texture(uTex, vec2(0.5 + p.x, 0.5 + p.y * uTint.x)).rgb : vec3(0.72, 0.74, 0.80);
                col = lab * (diff * uCam.w + 0.12) + (fr * env * 0.5 + spec * 0.25);
                if (r < 0.34) col = vec3(0.16 + 0.40 * env) + spec * 0.5;
                if (r > 0.975) col = vec3(0.5 + 0.35 * env);
            } else col = vec3(0.45 + 0.4 * env);         // the edges
        } else if (kind == 9) {                          // v36: an app - a glass cube, its icon in front, its colours in the glass
            vec2 dim = face < 1.5 ? vec2(W, H) : (face < 3.5 ? vec2(D, H) : vec2(W, D));
            vec2 e = min(uv, 1.0 - uv) * dim;
            float edge = min(e.x, e.y);
            vec3 tintG = cover ? textureLod(uTex, vec2(0.5), 2.0).rgb : vec3(0.06, 0.08, 0.2);   // the icon's average colour
            vec3 alb;
            if (face < 0.5) {
                // the icon, inset in the glass (a thin clear margin all round), slightly rounded
                vec2 q = abs(uv - 0.5) * 2.0;
                const float r = 0.86;
                float dist = length(max(q - (r - 0.12), 0.0)) + r - 0.12;   // a rounded square (corner radius .12)
                float inside = 1.0 - smoothstep(r - 0.01, r + 0.01, dist);
                vec2 iuv = (uv - 0.5) / r + 0.5;
                vec3 icon = cover ? texture(uTex, iuv).rgb : vec3(0.05, 0.07, 0.16);
                alb = mix(tintG * 0.35 + vec3(0.01, 0.012, 0.03), icon * mix(0.88, 1.0, facing), inside);
            } else alb = tintG * (0.22 + 0.25 * max(dot(N, uKeyDir.xyz), 0.0)) + vec3(0.008, 0.01, 0.025);   // coloured glass
            float ao = mix(0.84, 1.0, smoothstep(0.0, 0.03, edge));
            col = alb * ao * max(uMat.w, 0.0) + (fr * env * 0.7 + spec * 0.35) * max(uMat.w, 0.2);   // more glass than a photo
            col += (1.0 - smoothstep(0.0, 0.02, edge)) * (0.14 + 0.22 * uEmi.w);   // the light on its edges (chosen: more)
            col += uEmi.w * pow(1.0 - facing, 2.5) * (uRimCol.rgb * 0.25 + tintG * 0.25);
        } else if (kind == 10) {                         // v37: a power cube - dark glass, its sign engraved in light
            vec2 dim = face < 1.5 ? vec2(W, H) : (face < 3.5 ? vec2(D, H) : vec2(W, D));
            vec2 e = min(uv, 1.0 - uv) * dim;
            float edge = min(e.x, e.y);
            float sel = uMat.z;
            vec3 alb = vec3(0.016, 0.022, 0.055);
            vec3 glow = vec3(0.0);
            if (face < 0.5) {
                // the sign (the interface sheet's glass icon: red = its shape, green = the light on its edges)
                vec2 iuv = (uv - 0.17) / 0.66;
                if (iuv.x >= 0.0 && iuv.x <= 1.0 && iuv.y >= 0.0 && iuv.y <= 1.0) {
                    vec4 tx = textureLod(uTex, mix(uEmi.xy, uEmi.zw, iuv), 0.0);
                    float shape = tx.r * tx.a, ring = tx.g * tx.a;
                    glow = vec3(0.78, 0.86, 1.0) * (shape * (0.42 + 0.7 * min(sel, 1.0)) + ring * (0.18 + 0.3 * min(sel, 1.0)));
                }
                float c = 1.0 - length(uv - 0.5) * 1.6;   // a soft light inside the glass, around the sign
                glow += vec3(0.25, 0.34, 0.75) * max(c, 0.0) * 0.18 * min(sel, 1.0);
            }
            float ao = mix(0.8, 1.0, smoothstep(0.0, 0.03, edge));
            col = alb * diff * ao * lw + glow * (0.6 + 0.4 * lw) + (fr * env * 0.8 + spec * 0.4) * max(lw, 0.3);
            col += (1.0 - smoothstep(0.0, 0.02, edge)) * (0.12 + 0.25 * min(sel, 1.0)) * vec3(0.8, 0.88, 1.0);
            col += vec3(0.9, 0.94, 1.0) * max(sel - 1.0, 0.0) * 0.6;   // confirming: it turns into light
        } else if (kind == 8) {                          // v34: an album photo - a thin glass card, the picture in front
            vec2 dim = face < 1.5 ? vec2(W, H) : (face < 3.5 ? vec2(D, H) : vec2(W, D));
            vec2 e = min(uv, 1.0 - uv) * dim;
            float edge = min(e.x, e.y);
            vec3 alb = vec3(0.020, 0.025, 0.050);
            if (face < 0.5) {
                alb = cover ? texture(uTex, uv).rgb : vec3(0.025, 0.032, 0.075) * (0.8 + 0.2 * sin(uv.x * 5.0 - uv.y * 3.0));
                if (uTint.x > 0.001) alb = mix(alb, texture(uTex2, uv).rgb, uTint.x);   // v37: the sharp photo fades in
                if (uMat.z > 0.5) {                      // a video: a soft dark disc and a white triangle in the middle
                    vec2 p = (uv - 0.5) * dim / H;
                    float rr = length(p);
                    alb *= mix(0.45, 1.0, smoothstep(0.15, 0.165, rr));
                    vec2 q = p / 0.15;
                    float tri = step(-0.35, q.x) * step(q.x, 0.55) * step(abs(q.y), (0.55 - q.x) * 0.62);
                    alb = mix(alb, vec3(0.92), tri);
                }
                alb *= mix(0.86, 1.0, facing);           // (a photo stays as it was taken: little shading)
            } else alb *= 0.6 + 0.6 * max(dot(N, uKeyDir.xyz), 0.0);
            float ao = mix(0.82, 1.0, smoothstep(0.0, 0.025, edge));
            col = alb * ao * lw + (fr * env * 0.35 + spec * 0.18) * max(lw, 0.2);
            col += (1.0 - smoothstep(0.0, 0.012, edge)) * (0.10 + 0.18 * uEmi.w);   // the light on its edge (chosen: more)
            col += uEmi.w * pow(1.0 - facing, 3.0) * uRimCol.rgb * 0.2;
        } else {                                         // a cartridge / a UMD (the box model)
            vec2 dim = face < 1.5 ? vec2(W, H) : (face < 3.5 ? vec2(D, H) : vec2(W, D));
            vec2 e = min(uv, 1.0 - uv) * dim;
            float edge = min(e.x, e.y);
            vec3 alb = uBase.rgb;
            float film = 0.35;
            if (face < 0.5) {                            // the front: the label
                vec2 lo = kind == 7 ? vec2(0.08, 0.06) : vec2(0.10, 0.08), hi = kind == 7 ? vec2(0.92, 0.62) : vec2(0.90, 0.70);
                if (uv.x > lo.x && uv.x < hi.x && uv.y > lo.y && uv.y < hi.y) {
                    vec2 lu = (uv - lo) / (hi - lo);
                    // the cover fills the label (cropped, never stretched)
                    float la = (hi.x - lo.x) * W / ((hi.y - lo.y) * H), ca = uTint.x;
                    vec2 cu = la > ca ? vec2(lu.x, 0.5 + (lu.y - 0.5) * ca / la) : vec2(0.5 + (lu.x - 0.5) * la / ca, lu.y);
                    alb = cover ? texture(uTex, cu).rgb : vec3(0.7);
                    film = 0.8;
                }
            } else if (face < 1.5 && kind == 7) {        // the UMD's back: the disc through its window
                vec2 p = (uv - vec2(0.5, 0.47)) * vec2(W, H) / W;
                float r = length(p) / 0.40;
                if (r < 1.0) {
                    vec3 rad = normalize(vW - uEmi.xyz - N * dot(vW - uEmi.xyz, N) + 1e-5);
                    alb = vec3(0.45) * (0.3 + env) + Rainbow(N, V, cross(N, rad), r, Lp) * 0.8;
                    if (r < 0.3) alb = vec3(0.25 + 0.4 * env);
                    film = 1.0;
                }
            }
            // ridges on the cartridges' grip (the bottom of the front and back)
            if (kind == 6 && face < 1.5 && uv.y > 0.76) alb *= 0.85 + 0.15 * step(0.5, fract(uv.y * 40.0));
            float ao = mix(0.75, 1.0, smoothstep(0.0, 0.02, edge));
            col = alb * (diff * uCam.w + 0.12) * ao + film * (fr * env * 0.5 + spec * 0.4);
            col += (1.0 - smoothstep(0.0, 0.012, edge)) * 0.12;
        }
        float ma = 1.0;
        if (mrefl) {                                     // v37: fades out below the floor (its height: uEmi.x), darker
            ma = lw * (1.0 - smoothstep(0.0, 0.6, uEmi.x - vW.y)) * 0.9;
            col *= 0.5;
        }
        outColor = vec4(LinearToSRGB(clamp(Finish(col), 0.0, 1.0)), ma);
        return;
    }

    // ======================= the boxes =======================
    bool refl = uMat.w < 0.0;
    int part = int(uBase.a + 0.5) - 1;                   // 0 whole, 1 tray, 2 lid
    float band = uTint.x;
    bool boxArt = kind == 3 && uMat.x > 1.5;             // v93: a Switch game's real box (GameTDB), not its icon
    vec3 caseCol = uBase.rgb;
    vec2 dim;
    if (face < 1.5) dim = vec2(W, H);
    else if (face < 3.5) dim = vec2(D, H);
    else dim = vec2(W, D);

    vec3 alb;
    float film = 1.0;                                    // how much clear film / gloss covers this face
    bool inside = (part == 1 && face < 0.5) || (part == 2 && face > 0.5 && face < 1.5);
    if (inside) {                                        // v32: the inside of an open case
        alb = kind == 3 ? vec3(0.10, 0.012, 0.016) : vec3(0.012, 0.014, 0.020);
        if (part == 1 && kind != 3) {                    // the hub that holds the disc
            float r = length((uv - 0.5) * dim);
            alb += vec3(0.05) * (smoothstep(0.08, 0.09, r) - smoothstep(0.15, 0.16, r)) + vec3(0.02) * (1.0 - smoothstep(0.07, 0.08, r));
        }
        if (part == 1 && kind == 3) {                    // Switch: the game card holder
            vec2 q = abs(uv - vec2(0.5, 0.40)) * dim;
            alb += vec3(0.05, 0.01, 0.01) * (1.0 - step(0.13, max(q.x * 1.2, q.y)));
        }
        film = 0.5;
    } else if (face < 0.5 && boxArt) {                   // v93 (Yass: « vraies pochettes pour la Switch »): the real box
        // fills the front (it has its own red top), fitted whole: never stretched; if its shape is not quite the case's,
        // the margins take its own blurred edges
        float ca = uMat.x - 2.0, fa = W / H;
        vec2 cu = ca < fa ? vec2(0.5 + (uv.x - 0.5) * fa / ca, uv.y) : vec2(uv.x, 0.5 + (uv.y - 0.5) * ca / fa);
        bool inC = cu.x >= 0.0 && cu.x <= 1.0 && cu.y >= 0.0 && cu.y <= 1.0;
        alb = inC ? texture(uTex, cu).rgb : textureLod(uTex, clamp(cu, 0.0, 1.0), 2.0).rgb * 0.35;
    } else if (face < 0.5) {                             // front: the cover
        vec2 cuv = uv;
        if (band > 0.0 && uv.y < band) alb = uEmi.rgb;   // Switch: the red band at the top
        else {
            if (band > 0.0) cuv.y = (uv.y - band) / (1.0 - band);
            if (kind == 3 && cover) {
                // Switch (v32, Yass: « les jeux switch sont bugués »): the square icon, sharp, in the middle of a poster made
                // of the same icon blown up and blurred behind it (no more streaks)
                float q = W / (H * (1.0 - band));        // the icon's height in the area (0..1)
                float top = (1.0 - q) * 0.42;
                vec3 bg = textureLod(uTex, vec2(0.5 + (cuv.x - 0.5) * q, cuv.y), 2.0).rgb;
                bg *= 0.42 + 0.18 * (1.0 - abs(cuv.y - 0.5) * 2.0);
                float iy = (cuv.y - top) / q;
                if (iy >= 0.0 && iy <= 1.0) {
                    alb = texture(uTex, vec2(cuv.x, iy)).rgb;
                } else {
                    float sh = iy < 0.0 ? -iy * q : (iy - 1.0) * q;   // a soft shadow under / over the icon
                    alb = bg * mix(0.55, 1.0, smoothstep(0.0, 0.08, sh));
                }
            } else alb = cover ? texture(uTex, cuv).rgb : Blank(cuv);
        }
    } else if (face < 1.5) {                             // back: the cover, dim and blurred
        alb = cover ? textureLod(uTex, vec2(uv.x, uv.y), 2.0).rgb * 0.28 : Blank(uv) * 0.6;
    } else if (face < 2.5) {                             // spine: the cover's left edge, stretched
        vec3 s = cover ? textureLod(uTex, vec2(0.015 + uv.x * 0.03, uv.y), 1.0).rgb : Blank(uv);
        if (kind == 3 && cover && !boxArt) s = textureLod(uTex, vec2(0.5, uv.y), 2.0).rgb * 0.5;
        if (kind == 0 || kind == 2) s *= 0.55;           // under the case's dark plastic
        alb = s;
        if (band > 0.0 && uv.y < band) alb = uEmi.rgb * 0.85;
        if (kind == 1) alb = mix(s * 0.4, caseCol, 0.5); // jewel case: the black hinge
    } else {                                             // the other sides: the case
        alb = caseCol;
        if (kind == 4) alb = cover ? textureLod(uTex, uv, 2.0).rgb * 0.5 : caseCol;   // cardboard: printed all over
        film = kind == 4 ? 0.25 : 0.8;
    }
    if (kind == 4) film = min(film, 0.35);               // cardboard: satin, not glossy

    // the rounded edges: a soft darkening and a thin light line near each edge (no extra geometry)
    vec2 e = min(uv, 1.0 - uv) * dim;
    float edge = min(e.x, e.y);
    float rr = kind == 4 ? 0.008 : 0.022;
    float ao = mix(0.78, 1.0, smoothstep(0.0, rr * 2.5, edge));
    float lip = (1.0 - smoothstep(0.0, rr, edge)) * smoothstep(0.0, rr * 0.25, edge);

    if (refl) N.y = -N.y;                               // (the mirrored copy: light it as the real box)
    vec3 Lp; float dist;
    vec3 diff = Diffuse(N, Lp, dist);
    vec3 col = alb * (diff * uCam.w + 0.10) * (0.78 + 0.22 * facing);
    col *= ao * max(uMat.w, 0.0) + (refl ? ao : 0.0);

    // the clear film: the room's reflection (Fresnel), the lamps' highlights, and the light line on the edges
    float fr = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
    float env = EnvAt(reflect(-V, N), 0.12);
    vec3 H1 = normalize(uKeyDir.xyz + V), H2 = normalize(Lp + V);
    float spec = pow(max(dot(N, H1), 0.0), 120.0) * 0.9 + pow(max(dot(N, H2), 0.0), 60.0) * 0.5 / max(dist * dist * 0.15, 0.6);
    col += film * (fr * env * 0.55 + spec * (uKeyCol.rgb + uDriftCol.rgb * 0.15) * 0.35);
    col += lip * (0.10 + 0.25 * env) * film;
    col += uMat.z * pow(1.0 - facing, 3.0) * uRimCol.rgb * 0.25;   // chosen: a faint glow on its silhouette

    col = Finish(col);
    float a = 1.0;
    if (refl) {                                          // fades out below the floor, darker
        float below = uEmi.w - vW.y;
        a = -uMat.w * (1.0 - smoothstep(0.0, 0.55, below)) * 0.9;
        col *= 0.55;
    }
    outColor = vec4(LinearToSRGB(clamp(col, 0.0, 1.0)), a);
}
