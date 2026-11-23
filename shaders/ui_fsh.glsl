// Interface: text from the pre-baked atlas + the lock button's shapes. Every look
// is the CSS of docs/M1/maquette-brume.html, value for value. Output premultiplied.
layout (location = 0) in vec2 vPos;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec4 vColor;
layout (location = 3) in vec4 vParam;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D uAtlas;
layout (binding = 1) uniform sampler2D uUiAtlas;   // the wheel's interface, baked from the mockup (tools/bake-assets.html)
layout (binding = 2) uniform sampler2D uHazeT;     // the blurred fog: what a .glass panel shows behind it
layout (binding = 3) uniform sampler2D uAvatars;   // v46: the profiles' avatars (a strip of 8 x 128 px)
layout (binding = 4) uniform sampler2D uSocial;    // v95: the friends' strip (avatars and game icons, 64 px each; a QR code)
layout (binding = 5) uniform sampler2D uPicture;   // v98: a picture of its own (the Spotify cover, 128 px)

// signed distance to a rounded box of size sz (top-left at 0), radius r: < 0 inside
float RRect(vec2 p, vec2 sz, float r) { vec2 q = abs(p - sz * 0.5) - (sz * 0.5 - r); return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
// v78: a « surface »'s box - its corners are superellipses (n = 4, a squircle: never a hard square, softer than an arc)
float SqBox(vec2 p, vec2 sz, float r) {
    vec2 q = abs(p - sz * 0.5) - (sz * 0.5 - r), m = max(q, 0.0), m2 = m * m;
    return sqrt(sqrt(m2.x * m2.x + m2.y * m2.y)) + min(max(q.x, q.y), 0.0) - r;
}

float Cov(vec2 uv) { return textureLod(uAtlas, uv, 0.0).r; }

// .clock / .refl background: linear-gradient(180deg,#f6f8ff 0%,#d4defe 42%,#8ea2ee 78%,#5a6cc8 100%)
vec3 ClockGrad(float f) {
    f = clamp(f, 0.0, 1.0);
    vec3 a = vec3(0.965, 0.973, 1.0), b = vec3(0.831, 0.871, 0.996), c = vec3(0.557, 0.635, 0.933), d = vec3(0.353, 0.424, 0.784);
    if (f < 0.42) return mix(a, b, f / 0.42);
    if (f < 0.78) return mix(b, c, (f - 0.42) / 0.36);
    return mix(c, d, (f - 0.78) / 0.22);
}
// The light that crosses the digits: linear-gradient(100deg, transparent 42%, white .95 50%, transparent 58%),
// image 280% wide (3584 px) whose centre is at x = cx. y0 = top of the 150 px line box.
float Sweep(vec2 p, float cx, float y0) {
    vec2 dir = vec2(0.9848, 0.1736);                       // 100deg
    float u = dot(p - vec2(cx, y0 + 75.0), dir) / 3556.0 + 0.5;
    return 0.95 * clamp(1.0 - abs(u - 0.5) / 0.08, 0.0, 1.0);
}
// .colon background: linear-gradient(180deg,#f6f8ff 0%,#c9d6ff 50%,#7f95e8 100%)
vec3 ColonGrad(float f) {
    f = clamp(f, 0.0, 1.0);
    vec3 a = vec3(0.965, 0.973, 1.0), b = vec3(0.788, 0.839, 1.0), c = vec3(0.498, 0.584, 0.910);
    return f < 0.5 ? mix(a, b, f / 0.5) : mix(b, c, (f - 0.5) / 0.5);
}
vec4 Premul(vec3 c, float a) { return vec4(c * a, a); }

void main() {
    int mode = int(vParam.x + 0.5);
    vec4 o = vec4(0.0);
    if (mode == 0) {                                       // plain text
        o = Premul(vColor.rgb, Cov(vUV) * vColor.a);
    } else if (mode == 1) {                                // the clock digits: gradient + sweep
        float f = (vPos.y - vParam.y) / 150.0;
        vec3 c = mix(ClockGrad(f), vec3(1.0), Sweep(vPos, vParam.z, vParam.y));
        o = Premul(c, Cov(vUV) * vParam.w);
    } else if (mode == 2) {                                // the colon: its own gradient, beats with the heart
        o = Premul(ColonGrad((vPos.y - vParam.y) / 150.0), Cov(vUV) * vParam.w);
    } else if (mode == 3) {                                // glow (blurred atlas): drop-shadow 0 0 22px
        o = Premul(vColor.rgb, Cov(vUV) * vColor.a);
    } else if (mode == 4) {                                // reflection: flipped, blurred 1.5px, masked, .16
        vec2 ts = 1.0 / vec2(textureSize(uAtlas, 0));
        float k = vColor.r;                                // blur radius in atlas px
        float cov = Cov(vUV) * 0.4 + (Cov(vUV + vec2(k, 0.0) * ts) + Cov(vUV - vec2(k, 0.0) * ts) +
                                      Cov(vUV + vec2(0.0, k) * ts) + Cov(vUV - vec2(0.0, k) * ts)) * 0.15;
        float fv = (vPos.y - vParam.y) / 150.0;            // 0 at the top of the reflection (= its local bottom)
        float mask = 0.9 * clamp(1.0 - fv / 0.55, 0.0, 1.0);
        float ly = vParam.y + (1.0 - fv) * 150.0;          // local (unflipped) y for the background
        vec3 c = mix(ClockGrad(1.0 - fv), vec3(1.0), Sweep(vec2(vPos.x, ly), vParam.z, vParam.y));
        o = Premul(c, cov * mask * vParam.w);
    } else if (mode == 10) {                               // the three arcs (r 45, stroke 2, 8deg gaps)
        vec2 q = vUV - vec2(50.0);
        float r = length(q);
        float ang = degrees(atan(q.y, q.x)) + 90.0;        // 0 = top, clockwise (y down)
        if (ang < 0.0) ang += 360.0;
        int seg = int(ang / 120.0);
        float inSeg = ang - float(seg) * 120.0;
        float gapA = smoothstep(7.0, 9.0, inSeg) * (1.0 - smoothstep(111.0, 113.0, inSeg));
        float on = seg == 0 ? vParam.y : (seg == 1 ? vParam.z : vParam.w);
        float stroke = 1.0 - smoothstep(0.6, 1.6, abs(r - 45.0));
        float glow = exp(-pow(abs(r - 45.0) / 3.5, 2.0)) * 0.85 * on;     // drop-shadow 0 0 5px
        vec3 off = vec3(0.588, 0.667, 0.941), onC = vec3(0.776, 0.820, 0.973), glowC = vec3(0.471, 0.588, 1.0);
        float aS = stroke * mix(0.15, 1.0, on);
        vec3 c = mix(off, onC, on);
        o = (Premul(c, aS) + Premul(glowC, glow * (1.0 - aS))) * gapA * vColor.a;
    } else if (mode == 11) {                               // the glow behind A: radial-gradient(rgba(140,165,255,.4) -> 0 at 68%)
        float r = length(vUV) / vParam.y;
        o = Premul(vec3(0.549, 0.647, 1.0), 0.4 * clamp(1.0 - r / 0.68, 0.0, 1.0) * vColor.a);
    } else if (mode == 12) {                               // the glass A button (.a.glass, 56 px circle)
        float d = length(vUV), R = 28.0;
        float inside = 1.0 - smoothstep(R - 0.75, R + 0.25, d);
        float t = clamp(dot(vUV / (2.0 * R), normalize(vec2(1.0, 1.0))) + 0.5, 0.0, 1.0);   // 135deg
        vec4 fill = t < 0.5 ? mix(vec4(0.588, 0.667, 1.0, 0.07), vec4(1.0, 1.0, 1.0, 0.015), t / 0.5)
                            : mix(vec4(1.0, 1.0, 1.0, 0.015), vec4(0.549, 0.471, 1.0, 0.045), (t - 0.5) / 0.5);
        vec4 a = Premul(fill.rgb, fill.a * inside);
        float hl = exp(-dot(vUV - vec2(0.0, -0.4 * R), vUV - vec2(0.0, -0.4 * R)) / (144.0 * 144.0)) * 0.09 * inside;
        a += Premul(vec3(0.627, 0.725, 1.0), hl);           // ::after radial highlight
        float ring = (1.0 - smoothstep(0.0, 1.0, abs(d - (R - 0.5)))) * 0.10;                 // 1px border
        a = a * (1.0 - ring) + Premul(vec3(0.667, 0.745, 1.0), ring);
        float top = (1.0 - smoothstep(0.0, 1.2, abs(d - (R - 1.5)))) * smoothstep(0.0, -0.6, vUV.y / R) * 0.14 * inside;
        a = a * (1.0 - top) + Premul(vec3(0.784, 0.843, 1.0), top);                          // inset 0 1px highlight
        o = a * vColor.a;
    } else if (mode == 13) {                               // its shadow: 0 10px 30px rgba(0,0,0,.45)
        float d = length(vUV - vec2(0.0, 10.0));
        o = vec4(0.0, 0.0, 0.0, 0.45 * (1.0 - smoothstep(28.0 - 15.0, 28.0 + 30.0, d)) * vColor.a);
    } else if (mode == 14) {                               // .glass panel (top bar, name banner)
        vec2 sz = vParam.yz; float r = vParam.w, d = RRect(vUV, sz, r);
        float inside = 1.0 - smoothstep(-0.5, 0.5, d);
        // backdrop-filter: blur(16px) saturate(1.2) -> the (already blurred) fog behind, saturated
        vec3 back = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb;
        back = mix(vec3(dot(back, vec3(0.2126, 0.7152, 0.0722))), back, 1.2);
        // background: linear-gradient(135deg, rgba(150,170,255,.07), rgba(255,255,255,.015) 50%, rgba(140,120,255,.045))
        float t = clamp(dot(vUV - sz * 0.5, vec2(0.7071)) / ((sz.x + sz.y) * 0.7071) + 0.5, 0.0, 1.0);
        vec4 g = t < 0.5 ? mix(vec4(0.588, 0.667, 1.0, 0.07), vec4(1.0, 1.0, 1.0, 0.015), t / 0.5)
                         : mix(vec4(1.0, 1.0, 1.0, 0.015), vec4(0.549, 0.471, 1.0, 0.045), (t - 0.5) / 0.5);
        vec3 c = mix(back, g.rgb, g.a);
        // ::after radial-gradient(240px circle at 50% 30%, rgba(160,185,255,.09), transparent 60%)
        float hl = 0.09 * clamp(1.0 - length(vUV - vec2(sz.x * 0.5, sz.y * 0.3)) / 240.0 / 0.6, 0.0, 1.0);
        c = mix(c, vec3(0.627, 0.725, 1.0), hl);
        // border: 1px solid rgba(170,190,255,.10) - v70 (the PS2 look, Yass: « on voit trop les arêtes »): .035
        float border = 1.0 - smoothstep(0.0, 1.0, abs(d + 0.5));
        c = mix(c, vec3(0.667, 0.745, 1.0), 0.035 * border);
        // box-shadow: inset 0 1px 0 rgba(200,215,255,.14) - v70: .05
        float inset = inside * smoothstep(-0.5, 0.5, RRect(vUV - vec2(0.0, 1.0), sz, r));
        c = mix(c, vec3(0.784, 0.843, 1.0), 0.05 * inset);
        o = vec4(c * inside, inside) * vColor.a;
    } else if (mode == 15) {                               // its shadow: 0 10px 30px rgba(0,0,0,.45)
        float d = RRect(vUV - vec2(0.0, 10.0), vParam.yz, vParam.w);
        o = vec4(0.0, 0.0, 0.0, 0.45 * (1.0 - smoothstep(-19.0, 19.0, d)) * vColor.a);
    } else if (mode == 16) {                               // .btn-g: 25px circle, border 1.5px rgba(200,212,245,.7)
        float d = length(vUV), rr = vParam.y > 0.5 ? vParam.y : 11.75;   // p1 = ring radius (v28: 12.75 for the 27px hints)
        float ring = 1.0 - smoothstep(0.0, 0.9, abs(d - rr) - 0.75);
        o = Premul(vec3(0.784, 0.831, 0.961), ring * vColor.a);
    } else if (mode == 17) {                               // .tab.on: background rgba(140,160,255,.10), inset 0 1px 0 rgba(200,215,255,.12)
        vec2 sz = vParam.yz; float r = vParam.w, d = RRect(vUV, sz, r);
        float inside = 1.0 - smoothstep(-0.5, 0.5, d);
        float inset = inside * smoothstep(-0.5, 0.5, RRect(vUV - vec2(0.0, 1.0), sz, r));
        vec3 tc = vColor.rgb;                              // v44: the accent colour (Réglages > Brume); blue glacier = (.549, .627, 1)
        vec4 a = Premul(tc, 0.10 * inside);
        a = a * (1.0 - 0.12 * inset) + Premul(mix(tc, vec3(1.0), 0.52), 0.12 * inset);
        o = a * vColor.a;
    } else if (mode == 20) {                               // a baked sprite (straight rgba), p1 = blur (atlas px)
        vec4 c = textureLod(uUiAtlas, vUV, 0.0);
        if (vParam.y > 0.05) {
            vec2 ts = vParam.y / vec2(textureSize(uUiAtlas, 0));
            c = c * 0.2 + (textureLod(uUiAtlas, vUV + vec2(ts.x, 0.0), 0.0) + textureLod(uUiAtlas, vUV - vec2(ts.x, 0.0), 0.0) +
                           textureLod(uUiAtlas, vUV + vec2(0.0, ts.y), 0.0) + textureLod(uUiAtlas, vUV - vec2(0.0, ts.y), 0.0)) * 0.2;
        }
        o = vec4(c.rgb * c.a, c.a) * vColor.a;
    } else if (mode == 21) {                               // v28: a star (the mockup's dust points): soft dot, added (alpha 0)
        if (vParam.y > 0.5) {                              // v70: a piece of a comet's trail - soft across, even along, its ends
            float along = 1.0 - smoothstep(1.0 - vParam.z, 1.0, abs(vUV.x));   // fading over p2 (they overlap: an even streak)
            o = vec4(vColor.rgb * exp(-vUV.y * vUV.y * 4.0) * along * vColor.a, 0.0);
        } else o = vec4(vColor.rgb * exp(-dot(vUV, vUV) * 4.0) * vColor.a, 0.0);
    } else if (mode == 22) {                               // v28: a settings icon of neutral glass (mockup v26 .gi)
        // sprite: red = the shape, green = the light inside its edges (baked); p1, p2 = the sprite's atlas v range
        // p3: 0 glass, 1 selected (brighter), 2 the soft shadow under it
        vec4 tx = textureLod(uUiAtlas, vUV, 0.0);
        float sh = tx.r * tx.a, ed = tx.g * tx.a, t = clamp((vUV.y - vParam.y) / max(vParam.z - vParam.y, 1e-5), 0.0, 1.0);
        // p3: 3 / 4 = the PS2 look (v70): the same glass, its edges discreet (Yass: « on voit trop les arêtes »)
        if (vParam.w > 1.5 && vParam.w < 2.5) { o = vec4(0.0, 0.0, 0.0, 0.45 * sh * vColor.a); }
        else {
            // backdrop-filter: blur(3px) brightness(1.3) saturate(.3) -> the (blurred) fog behind, paler and grey
            vec3 back = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb;
            back = mix(vec3(dot(back, vec3(0.2126, 0.7152, 0.0722))), back, 0.3) * 1.3;
            // linear-gradient(168deg, white .30, .12 at 40 %, .05 at 64 %, .13) (selected: .40 .16 .07 .18)
            float s = (vParam.w > 0.5 && vParam.w < 1.5) || vParam.w > 3.5 ? 1.33 : 1.0;
            float ga = (t < 0.4 ? mix(0.30, 0.12, t / 0.4) : t < 0.64 ? mix(0.12, 0.05, (t - 0.4) / 0.24) : mix(0.05, 0.13, (t - 0.64) / 0.36)) * s;
            vec3 c = mix(back, vec3(1.0), ga);
            // the light on the edges' thickness: linear-gradient(150deg, .72, .18 at 40 %, .04 at 62 %, .32), opacity .75
            float ea = (t < 0.4 ? mix(0.72, 0.18, t / 0.4) : t < 0.62 ? mix(0.18, 0.04, (t - 0.4) / 0.22) : mix(0.04, 0.32, (t - 0.62) / 0.38)) * (vParam.w > 2.5 ? 0.28 : 0.75);
            c = mix(c, vec3(1.0), clamp(ed * ea, 0.0, 1.0));
            c += vec3(0.32) * clamp(1.0 - t * 2.2, 0.0, 1.0) * 0.5;                  // the reflection at the top
            o = vec4(c * sh, sh) * vColor.a;
        }
    // (v73's mode 29, the settings icons' liquid glass, lives in its own program since v74: shaders/verre_fsh.glsl)
    } else if (mode == 23) {                               // v28: the frosted dark band behind the chosen setting (#xBand)
        vec2 sz = vParam.yz;                               // v70.5: p3 > 0 = the fade's width in px (the same on every side)
        float wx = vParam.w > 0.5 ? vParam.w / sz.x : 0.14, wy = vParam.w > 0.5 ? vParam.w / sz.y : 0.22;
        float fx = smoothstep(0.0, wx, vUV.x / sz.x) * smoothstep(0.0, wx, 1.0 - vUV.x / sz.x);
        float fy = smoothstep(0.0, wy, vUV.y / sz.y) * smoothstep(0.0, wy, 1.0 - vUV.y / sz.y);
        float a = min(0.92, 0.55 * fx * fy * vColor.a);   // (vColor.a may go above 1: a darker veil)
        o = vec4(vec3(0.016, 0.024, 0.094) * a, a);
    } else if (mode == 25) {                               // v33: a popup's frosted backdrop: the blurred fog instead of
        // the scene (the consoles melt into the mist), dimmed (vColor.rgb) with a soft vignette
        vec3 back = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb;
        float vig = 1.0 - 0.45 * clamp(length((vPos - vec2(640.0, 360.0)) / vec2(700.0, 420.0)), 0.0, 1.0);
        o = vec4(back * vColor.rgb * vig * vColor.a, vColor.a);
    } else if (mode == 26) {                               // v46: an avatar, a soft-edged circle; p1 = its centre u, p2 = half its width in u
        vec2 q = vec2((vUV.x - vParam.y) / max(vParam.z, 1e-5), (vUV.y - 0.5) * 2.0);
        float a = (1.0 - smoothstep(0.93, 1.0, length(q))) * vColor.a;
        o = vec4(textureLod(uAvatars, vUV, 0.0).rgb * vColor.rgb * a, a);
    } else if (mode == 27) {                               // v70, the PS2 look: a soft light ADDED, no edge at all; p1, p2 = size
        vec2 q = clamp(vUV / max(vParam.yz, vec2(1.0)), 0.0, 1.0);   // p3: 0 a band (fades on every side), 1 a round glow
        float k;
        if (vParam.w < 0.5) { float vy = 1.0 - abs(q.y * 2.0 - 1.0); k = sin(3.14159265 * q.x) * vy * vy; }
        else { vec2 c = q * 2.0 - 1.0; float d = clamp(1.0 - dot(c, c), 0.0, 1.0); k = d * d; }
        o = vec4(vColor.rgb * (k * vColor.a), 0.0);
    } else if (mode == 28) {                               // v70, the PS2 look: a small solid triangle (a value's arrows, the hints)
        vec2 c = vUV;                                      // -1..1; p1 = where it points: 0 left, 1 right, 2 up, 3 down; p2 = 1 px in uv
        int dir = int(vParam.y + 0.5);
        if (dir == 1) c.x = -c.x; else if (dir == 2) c = vec2(c.y, c.x); else if (dir == 3) c = vec2(-c.y, c.x);
        float d = max(max(c.x - 0.7, abs(c.y) - (c.x + 1.0) * 0.5), -1.0 - c.x);   // pointing left: tip at x = -1
        o = Premul(vColor.rgb, (1.0 - smoothstep(-vParam.z, vParam.z, d)) * vColor.a);
    } else if (mode == 30) {                               // v78 (Yass: « les menus avec fond, ou même les notifs, on voit pas
        // leur fond... elle est carrée... pas de Liquid Glass »): a « surface » - a plate of dense night mist, NOT glass. Rounded
        // (squircle) corners with a soft but clear edge; deep blue at the top to near-black at the bottom, the blurred fog behind
        // breathing through a little; a cloud of its tint high in it; the light of that tint along its top edge (gone at the
        // sides); a shadow under it and a faint halo of light around it.
        // uv = local px, the plate at [0, w] x [0, h] (the quad is larger: shadow, halo); p1, p2 = w, h; p3 = corner radius;
        // vColor.rgb = its tint (the ambient colour; red for a deletion, pink for an error), vColor.a = how much it shows
        // (written for few registers: ui_fsh's register count is shared by EVERY interface pixel - see v74)
        float d = SqBox(vUV, vParam.yz, vParam.w);
        float inside = 1.0 - smoothstep(-1.0, 1.0, d);
        if (inside < 0.004) {                              // around it: the shadow (14 px lower, wide) and the halo (added)
            float sh = 0.55 * (1.0 - smoothstep(-16.0, 40.0, SqBox(vUV - vec2(0.0, 14.0), vParam.yz, vParam.w)));
            o = vec4(vColor.rgb * (0.13 * exp(-d / 20.0)), sh) * vColor.a;
        } else {
            vec3 c = textureLod(uHazeT, vPos / vec2(1280.0, 720.0), 0.0).rgb * 0.16;   // the fog behind, softly (never a clear view)
            vec2 f = clamp(vUV / vParam.yz, 0.0, 1.0);
            float e = max(-d, 0.0), side = sin(3.14159265 * f.x);
            // the lights in it, as amounts of its tint: the cloud high in it, the edge barely lighter (it reads on black), and
            // the light along its top edge (soft, gone at the sides; whiter)
            float top = 0.32 * exp(-e * e / 3.5) * (1.0 - smoothstep(0.0, 0.32, f.y)) * side * side;
            float lit = 0.11 * exp(-(f.x - 0.5) * (f.x - 0.5) * 2.25 - (f.y + 0.05) * (f.y + 0.05) * 6.76) + 0.045 * exp(-e / 6.0) + 0.55 * top;
            c += mix(vec3(0.050, 0.066, 0.150), vec3(0.010, 0.013, 0.040), f.y) + vColor.rgb * lit + vec3(0.45 * top);   // deep blue -> near-black
            c *= 1.0 - 0.30 * exp(-e / 10.0) * smoothstep(0.55, 1.0, f.y);                // the bottom edge, in shadow
            c += (fract(sin(dot(vPos, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.008;   // a grain: no banding
            o = mix(vec4(vColor.rgb * 0.13, 0.42), vec4(c * 0.94, 0.94), inside) * vColor.a;   // (its edge: 2 px, onto the halo)
        }
    } else if (mode == 31) {                               // v95: the friends' strip. p1, p2 = the centre (u, v) of the picture;
        // p3 = its half width in u: 0 < p3 < 1 an avatar (a soft-edged circle), 1 < p3 < 2 a game icon (a soft rounded square,
        // half width p3 - 1), p3 < 0 a QR code (half width -p3): black and white made crisp at any size, light = vColor
        vec2 ts = vec2(textureSize(uSocial, 0));
        float hu = vParam.w < 0.0 ? -vParam.w : (vParam.w > 1.0 ? vParam.w - 1.0 : vParam.w);
        vec2 q = vec2((vUV.x - vParam.y) / max(hu, 1e-5), (vUV.y - vParam.z) / max(hu * ts.x / ts.y, 1e-5));
        vec3 t = textureLod(uSocial, vUV, 0.0).rgb;
        if (vParam.w < 0.0) {
            float l = t.g, w = max(fwidth(l) * 0.75, 0.02);
            float lit = smoothstep(0.5 - w, 0.5 + w, l);
            vec3 c = mix(vec3(0.015, 0.02, 0.05), vColor.rgb, lit);
            float rc = length(max(abs(q) - vec2(0.9), 0.0)) / 0.1;            // its corners rounded (inside the quiet zone)
            float a = (1.0 - smoothstep(0.85, 1.0, rc)) * vColor.a;
            o = vec4(c * a, a);
        } else {
            float d = vParam.w > 1.0 ? length(max(abs(q) - vec2(0.62), 0.0)) / 0.38 : length(q);
            float a = (1.0 - smoothstep(0.93, 1.0, d)) * vColor.a;
            o = vec4(t * vColor.rgb * a, a);
        }
    } else if (mode == 32) {                               // v98: a picture (uPicture), a rounded square with a soft edge; uv 0..1, p1 = corner radius (share of the side)
        float a = (1.0 - smoothstep(-0.012, 0.004, RRect(vUV, vec2(1.0), vParam.y))) * vColor.a;
        o = vec4(textureLod(uPicture, vUV, 0.0).rgb * vColor.rgb * a, a);
    } else if (mode == 24) {                               // v28: a plain rounded rect (straight colour); p1, p2 = size, p3 = radius
        float a = (1.0 - smoothstep(-0.5, 0.5, RRect(vUV, vParam.yz, vParam.w))) * vColor.a;
        o = vec4(vColor.rgb * a, a);
    }
    outColor = o;
}
