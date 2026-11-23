// The settings carousel's 3D icons (v28): glass pebbles on a ring. The mockup v27's waterGlass (icon path), in the
// cubes' frame: x right, y up, z forward; menu px with y down.
// Settings.cpp puts the camera's basis in uKeyDir (right), uKeyCol (up), uRimDir (forward) and its position in uCam.
// v74 (Yass, on the console: « on dirait qu'elles sont coloriées en bleu », and the FPS dropped) - CLEAR NEUTRAL glass,
// the water of the cubes (glass_fsh): no milky body, no blue tint, no frost. What is behind (the fog) shows through,
// bent by the rounded edges; the light of the settings' glass (uGlass, like the 2D icons) lights the rims on its side,
// a sharp « wet » reflection at the top, a vertical light (the PS3 icons' gradient), a glow on the far side, the chosen
// pebble lit from inside (uEmi.x), a cube's bump flashing in it (uEmi.y). ONE read of the fog per pixel (v73: 8), and
// the back faces are culled (Settings::DrawMeshes): each pixel is shaded once.
layout (std140, binding = 1) uniform Mesh {
    mat4 uMVP;
    mat4 uModel;
    vec4 uBase;
    vec4 uMat;
    vec4 uTint;
    vec4 uEmi;       // v73: x = how chosen (0..1), y = a cube's bump / A's flash (0..1), z = the category open (0..1)
    vec4 uCam;
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
layout (binding = 2) uniform sampler2D uHazeT;   // the blurred fog (what the glass lets through)

const float kIor = 1.33;   // water

void main() {
    vec3 R = uKeyDir.xyz, U = uKeyCol.xyz, F = uRimDir.xyz;
    vec3 dW = normalize(vW - uCam.xyz), nWw = normalize(vN);
    vec3 rd = vec3(dot(dW, R), dot(dW, U), dot(dW, F));
    vec3 nW = vec3(dot(nWw, R), dot(nWw, U), dot(nWw, F));
    if (dot(nW, rd) > 0.0) nW = -nW;                      // (a face seen from behind: its normal towards us)
    vec2 p = gl_FragCoord.xy * (1280.0 / uScreen.x);
    float cosI = max(dot(-rd, nW), 0.0), e = 1.0 - cosI, e2 = e * e, fres = 0.02 + 0.98 * e2 * e2 * e;
    // thick glass with nearly parallel faces: the middle lets the fog through, the rounded edges act as lenses
    vec3 dIn = refract(rd, nW, 1.0 / kIor), outW = normalize(rd + (dIn - rd) * 2.0);
    vec2 dv = (outW.xy / max(outW.z, 0.25) - rd.xy / rd.z) * vec2(1.0, -1.0) * 480.0;
    dv *= min(1.0, 300.0 / max(length(dv), 1e-3));
    float chromeK = clamp(uThA.x, 0.0, 1.0);                                            // v81: Chrome - polished silver pebbles
    vec3 rl0 = reflect(rd, nW);
    dv = mix(dv, -rl0.xy * vec2(-220.0, 220.0), chromeK);                               // (the one read: what the metal mirrors)
    vec3 through = textureLod(uHazeT, clamp((p + dv) / vec2(1280.0, 720.0), 0.0, 1.0), 0.0).rgb;   // the one read
    float around = clamp(dot(through, vec3(0.30, 0.55, 0.15)) * 6.0, 0.08, 1.0);   // it borrows the light around it
    // the light of the settings' glass (uGlass.xy: on screen, y down) - from the top left, it drifts and follows the tilt
    vec2  Ls = dot(uGlass.xy, uGlass.xy) > 0.01 ? normalize(uGlass.xy) : vec2(-0.6, -0.8);
    vec3  key = normalize(vec3(Ls.x * 0.75, -Ls.y * 0.75, -0.62));
    float face = 0.06 + 0.16 * max(nW.y, 0.0) + 0.20 * max(dot(nW, key), 0.0);       // each face catches the light a little
    vec3 rl = reflect(rd, nW);
    // soft studio lights in the reflection (no read): a band above with a sharp « wet » edge, a soft one on the side
    float sky = smoothstep(0.55, 0.62, rl.y) * 0.55 + smoothstep(0.80, 0.97, dot(rl, key)) * 0.45;
    vec3 env = vec3(0.92, 0.95, 1.0) * sky * (0.45 + 0.65 * around) + through * 0.55;
    float rk = max(dot(rl, key), 0.0), r2 = rk * rk, r4 = r2 * r2, r8 = r4 * r4, r16 = r8 * r8, r32 = r16 * r16;
    float glint = r32 * r32 * r16 * 1.4 * (0.4 + 0.6 * around);                         // ~pow 80
    // the rims: bright on the light's side, a little on the other (the light crossing a thick glass), never even
    vec2  ns = nW.xy; float nl = length(ns); ns = nl > 1e-4 ? ns / nl : vec2(0.0);
    float lg = dot(ns, vec2(Ls.x, -Ls.y)), lit = max(lg, 0.0), far = max(-lg, 0.0);
    float edge = e2 * e;
    vec3 col = mix(through * 1.03 + vec3(0.90, 0.94, 1.0) * face * (0.30 + 0.45 * around), env, clamp(fres, 0.0, 1.0));
    col += vec3(0.95, 0.97, 1.0) * glint;
    col = mix(col, vec3(0.94, 0.96, 1.0), clamp(edge * (lit * lit * lit + 0.15 * far * far) * 0.60, 0.0, 0.60));
    col += (0.5 + 0.5 * cos(6.2831853 * (edge * 1.6 + vec3(0.0, 0.33, 0.67)))) * edge * (0.05 + 0.10 * lit) * (0.4 + 0.6 * around);
    // the PS3 icons' vertical light (their painted gradient): lighter at the top of the pebble, deeper at its bottom
    float hn = dot(vW - uModel[3].xyz, U) / max(length(uModel[1].xyz) * 0.6, 1e-3);
    col = mix(col, vec3(0.92, 0.95, 1.0), smoothstep(-0.2, 1.0, hn) * 0.16 * (0.4 + 0.6 * cosI)) * mix(0.84, 1.0, smoothstep(-1.0, 0.25, hn));
    vec3 wl = mix(uAmbient.rgb, vec3(1.0), 0.75);                                       // nearly white (a hint of the ambience)
    col += wl * far * far * smoothstep(0.05, 0.5, e) * 0.18;                            // the light gathered on the far side
    float f = clamp(uEmi.x, 0.0, 1.0), kick = clamp(uEmi.y, 0.0, 1.0);
    col += wl * (f * (0.04 + 0.08 * edge) * (0.86 + 0.14 * sin(uScreen.z * 2.2440)) + kick * 0.22);   // lit from inside (2.8 s)
    if (chromeK > 0.001) {                                                              // v81: Chrome - the studio in polished metal
        vec3 metal = (ChromeEnv(rl, uAmbient.rgb) * 1.45 + through * 0.8) * (0.62 + 0.38 * e2 * e) * (0.8 + 0.4 * face / 0.42);
        metal += vec3(0.95, 0.97, 1.0) * (glint * 1.6 + r32 * r32 * r32 * r32 * 1.5);  // a sharp light, ~pow 128
        metal += wl * (f * 0.10 + kick * 0.22);
        col = mix(col, metal * mix(0.4, 1.0, around), chromeK);
    }
    outColor = vec4(col, 1.0);
}
