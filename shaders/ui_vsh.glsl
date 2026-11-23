#version 460
// Interface quads, positioned in 1280x720 menu space (whatever the real target size).
layout (location = 0) in vec2 aPos;     // menu px
layout (location = 1) in vec2 aUV;      // atlas uv (text) or local px (shapes)
layout (location = 2) in vec4 aColor;   // straight rgba
layout (location = 3) in vec4 aParam;   // x = mode, yzw = per-mode values

layout (location = 0) out vec2 vPos;
layout (location = 1) out vec2 vUV;
layout (location = 2) out vec4 vColor;
layout (location = 3) out vec4 vParam;

void main() {
    vPos = aPos; vUV = aUV; vColor = aColor; vParam = aParam;
    gl_Position = vec4(aPos.x / 640.0 - 1.0, 1.0 - aPos.y / 360.0, 0.0, 1.0);
}
