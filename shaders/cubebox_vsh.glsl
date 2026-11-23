// v90 (perf) - the water cubes, each in its own square (after glassbg_fsh drew the room over the whole screen): the square
// is the box around the cube's 8 corners seen on screen (the same projection as glass_fsh: focal 900, centre 640 x 360), plus
// what the lock screen's blasts can push a pixel (uCubeFx[5].w, set by Entry.cpp: the air bends in glass_fsh LightAt), plus
// 2 px. glass_fsh's hits are inside the cube's outline, so no cube pixel is ever left out; where two squares overlap, the
// same pixel is written twice with the same colour (glass_fsh finds the nearest cube itself). 6 instances x 6 vertices; an
// unused cube gives an empty square. (common.glsl comes first: the Scene block, the same as the fragment shader's.)
layout (location = 0) out vec2 vUV;

vec3 QRotV(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

void main() {
    int k = gl_InstanceID, v = gl_VertexID;
    vec4 c = uCube[k];
    if (c.w <= 0.0) { vUV = vec2(0.0); gl_Position = vec4(2.0, 2.0, 0.0, 1.0); return; }
    vec4 q = uRot[k];
    vec2 lo = vec2(1e9), hi = vec2(-1e9);
    for (int i = 0; i < 8; i++) {
        vec3 s = vec3((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0, (i & 4) != 0 ? 1.0 : -1.0);
        vec3 w = c.xyz + QRotV(q, s * c.w);
        float z = max(w.z, 0.05);                                    // (the cubes stay far in front: z >= 9)
        vec2 sp = vec2(640.0 + w.x / z * 900.0, 360.0 - w.y / z * 900.0);
        lo = min(lo, sp); hi = max(hi, sp);
    }
    float m = 2.0 + uCubeFx[5].w;                                    // + how far the lock screen's blasts push a pixel (Entry.cpp)
    lo = clamp(lo - m, vec2(0.0), vec2(1280.0, 720.0));
    hi = clamp(hi + m, vec2(0.0), vec2(1280.0, 720.0));
    vec2 corner = vec2((v == 1 || v == 2 || v == 4) ? 1.0 : 0.0, (v == 2 || v == 4 || v == 5) ? 1.0 : 0.0);
    vec2 px = mix(lo, hi, corner);
    vUV = px / vec2(1280.0, 720.0);
    gl_Position = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 0.0, 1.0);
}
