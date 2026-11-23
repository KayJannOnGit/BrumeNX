#version 460
// v29.1 - the second glass pass (the water cubes in front of the consoles) drawn as one small square per cube instead
// of the whole screen (the full-screen version cost ~7 ms: Yass lost FPS). 6 instances x 6 vertices; a cube that does
// not need it (uCubeFx[k].y = 0: it touches no console on screen) gives an empty square (no pixel at all).
// vUV like fullscreen_vsh: 0..1 across the screen, y = 0 at the top. The block below = shaders/common.glsl.
layout (std140, binding = 0) uniform Scene {
    vec4 uScreen;
    vec4 uHaze;
    vec4 uCube[6];
    vec4 uRot[6];
    vec4 uCubeFx[6];
    vec4 uLight;
    vec4 uOrigin;
    vec4 uB[4];
    vec4 uBO[4];
    vec4 uAmbient;
};
layout (location = 0) out vec2 vUV;

void main() {
    int k = gl_InstanceID, v = gl_VertexID;
    vec4 c = uCube[k];
    if (c.w <= 0.0 || uCubeFx[k].y < 0.5 || c.z <= 0.3) { vUV = vec2(0.0); gl_Position = vec4(2.0, 2.0, 0.0, 1.0); return; }
    vec2 corner = vec2((v == 1 || v == 2 || v == 4) ? 1.0 : -1.0, (v == 2 || v == 4 || v == 5) ? 1.0 : -1.0);
    vec2 centre = vec2(640.0 + c.x / c.z * 900.0, 360.0 - c.y / c.z * 900.0);
    float r = c.w * 1.8 / c.z * 900.0 + 6.0;                     // the cube's bounding circle on screen (+ a margin)
    vec2 px = centre + corner * r;
    vUV = px / vec2(1280.0, 720.0);
    gl_Position = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 0.0, 1.0);
}
