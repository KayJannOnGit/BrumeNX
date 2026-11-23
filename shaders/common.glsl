// Shared by every Brume fragment shader (prepended at build time).
// Coordinates: the shaders think in a 1280x720 "menu space" whatever the real
// target size, so a scene looks the same at 720p, 1080p or in a small buffer.
layout (std140, binding = 0) uniform Scene {
    vec4 uScreen;    // target width, target height, time (s), w: 1 = glass_fsh second pass (the cubes in front of the consoles)
    vec4 uHaze;      // cloud y (menu px), strength, light angle, w: v52 the XMB wave (0 off .. 1)
    vec4 uCube[6];   // centre xyz (view space), half size (0 = no cube) - v29: up to 6, how many is random
    vec4 uRot[6];    // orientation quaternions
    vec4 uCubeFx[6]; // x = « lit up » bump when the light front touches the cube (0..1)
    // The lock screen's light (docs/M1/maquette-brume.html, lightPass): blasts of
    // light leave the A button; behind each front the scene keeps more light.
    vec4 uLight;     // base light, EDGE, FRONT_W, FULLR      (1, ... = fully lit: no effect)
    vec4 uOrigin;    // button centre x, y (menu px), flash, wake (fog grows with each step)
    vec4 uB[4];      // per blast: front radius, light behind, flash amp, type (0 none, 1 on, -1 off, 2 flash)
    vec4 uBO[4];     // per blast: origin x, y
    vec4 uAmbient;   // v29: the ambient colour (Réglages > Brume), the fog's light; w: v47 the theme's light
    vec4 uGlass;     // v73: the settings icons' liquid glass - xy: where the light comes from (screen, y down, unit),
                     // z: the lens strength, w: how much the glass is there (the page arriving: 0 -> 1). The block is FULL now (512 bytes)
};

// v81 - the themes (Réglages > Brume > Thème: Brume, Nuit profonde, Chrome; src/Theme.cpp). The block is full: the theme's
// numbers ride in its free halves (uBO[k].zw: the lock's blasts use xy only; uCubeFx[k].w: the cubes use xyz). They glide
// on the CPU (a theme change crossfades). ALL ZERO = the Brume look, exactly as before v81.
#define uThA vec4(uBO[0].zw, uBO[1].zw)   // x: Chrome (0..1), y: the fog's density (+ thicker), z: the fog's clock (s, added to t), w: its curl (- smoother)
#define uThB vec4(uBO[2].zw, uBO[3].zw)   // x: drifting lights - 3, y: their clock (s, added to t), z: their strength (+), w: sparkle (+)
#define uThC vec4(uCubeFx[0].w, uCubeFx[1].w, uCubeFx[2].w, uCubeFx[3].w)   // x: the room's darkness (0..1), y: the cubes' reflections (+), z: the wheel's wind, w: A's ripple (s since; 0 none)
#define uThD vec2(uCubeFx[4].w, uCubeFx[5].w)   // x: the charging breath (0..1), y: v90 - how far the blasts push a pixel (px: cubebox_vsh)

// v81 - Chrome: a soft studio seen in polished metal (the liquid chrome, its drops, the cubes, the pebbles share it).
// r: the reflected ray (x right, y up, z into the screen: what the metal shows of the room around the viewer is z < 0).
// A deep blue-black studio, a bright horizon strip (the chrome's signature line), a broad soft light above, a cyan softbox
// on the left, a white one on the right. tint: the ambient colour (its hue and its brightness: the sky, the battery, the silver).
vec3 ChromeEnv(vec3 r, vec3 tint) {
    float y = r.y;
    vec3 c = mix(vec3(0.020, 0.022, 0.030), vec3(0.085, 0.10, 0.135), smoothstep(-0.85, -0.02, y));   // the floor, up to the horizon
    c = mix(c, vec3(0.055, 0.068, 0.098), smoothstep(0.02, 0.30, y));                                    // a deep blue room above
    c = mix(c, vec3(0.012, 0.018, 0.035), smoothstep(0.45, 0.95, y));                                    // the ceiling: dark
    c += vec3(0.80, 0.88, 1.00) * exp(-(y - 0.16) * (y - 0.16) * 200.0) * 0.82;                           // the horizon strip
    c += vec3(0.62, 0.70, 0.82) * smoothstep(0.50, 0.70, y) * smoothstep(0.98, 0.80, y) * 0.22;          // the broad light above
    c += vec3(0.50, 0.86, 1.00) * smoothstep(0.86, 0.98, dot(r, vec3(-0.62, 0.30, -0.72))) * 0.34;       // cyan softbox, left
    c += vec3(0.96, 0.97, 1.00) * smoothstep(0.88, 0.985, dot(r, vec3(0.58, 0.42, -0.70))) * 0.42;       // white softbox, right
    vec3 tn = tint / max(max(tint.r, tint.g), max(tint.b, 1e-3));
    float lk = clamp(sqrt(dot(tint, vec3(0.30, 0.55, 0.15)) / 0.66), 0.20, 1.35);   // a dark ambient (night, low battery): dark metal
    return c * mix(vec3(1.0), tn, 0.55) * lk;
}

// v52 - the XMB wave (Réglages > Brume > Vague XMB; mockup makeWave, redone in 2D): a thin transparent sheet that twists
// as it waves; it only glows where it folds (seen edge-on), the rest is barely there. The sheet's own numbers are the
// mockup's (speeds, sines, widths, colours); x / y are mapped to the screen (menu px). v73: here (it was in haze_fsh),
// shared (v74: only haze_fsh uses it - recomputed per pixel in the icons' glass it cost too much on the console).
vec3 Wave(vec2 p, float t, float seed, float width, float alpha, vec3 col, float baseY) {
    float x = (p.x - 640.0) / 32.0;                                         // sheet units across the screen (about +-20)
    float tt = t * 0.16 + seed;
    float yw = sin(x * 0.09 + tt) + sin(x * 0.21 - tt * 0.8 + seed) * 0.42 + sin(x * 0.035 + tt * 0.45) * 1.25;
    float phi = sin(x * 0.13 + tt * 0.9 + seed * 2.0) * 1.25 + sin(x * 0.05 - tt * 0.5) * 0.6;   // the twist
    float w = width * (0.55 + 0.45 * sin(x * 0.07 + tt * 0.6 + seed));
    float cphi = abs(cos(phi));
    float halfW = 0.5 * w * cphi + 0.07;                                    // what is seen of its width (edge-on: a line)
    float dy = abs(p.y - (baseY - yw * 48.0)) / 48.0;
    if (dy > halfW + 0.2) return vec3(0.0);
    float inside = 1.0 - smoothstep(halfW, halfW + 0.14, dy);
    float fold = pow(1.0 - cphi, 5.0);                                      // bright lines at the folds
    float rim = smoothstep(halfW * 0.55, halfW, dy) * (1.0 - fold);         // its two borders, faint
    float fade = smoothstep(0.0, 0.18, p.x / 1280.0) * smoothstep(1.0, 0.82, p.x / 1280.0);
    return col * (fold * 0.9 + 0.035 + rim * 0.12) * inside * fade * alpha;
}
// both sheets, as the fog pass adds them (haze_fsh), before the lock screen's light and the theme's
vec3 XmbWave(vec2 p, float t) {
    return Wave(p, t, 0.0, 2.4, 0.55, vec3(0.561, 0.643, 0.910), 488.0) + Wave(p, t, 2.7, 1.7, 0.35, vec3(0.604, 0.549, 0.878), 470.0);
}
