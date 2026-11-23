#version 460
// One triangle that covers the whole target (no vertex buffer: built from
// gl_VertexID). vUV is 0..1 across the screen, with y = 0 at the TOP.
// deko3d device defaults: origin upper-left, Y axis up -> clip y = +1 is the top row.
layout (location = 0) out vec2 vUV;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
