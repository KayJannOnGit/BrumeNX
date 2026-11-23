#version 460
// The consoles (3D models baked from the mockup by tools/bake-assets.html).
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNrm;
layout (location = 2) in vec2 aUV;

// Same block as mesh_fsh.glsl (binding 1).
layout (std140, binding = 1) uniform Mesh {
    mat4 uMVP;       // projection * view * model
    mat4 uModel;     // model -> world
    vec4 uBase;      // base colour (linear, already x0.78 like the mockup), opacity
    vec4 uMat;       // metalness, roughness, envMapIntensity, flags (1 texture, 2 glass transmission)
    vec4 uTint;      // glass attenuation colour (linear), thickness / attenuationDistance
    vec4 uEmi;       // emissive (linear)
    vec4 uCam;       // camera position (world), exposure
    vec4 uKeyDir;    // the mockup's lights (scene of maquette-brume.html), linear colour x intensity
    vec4 uKeyCol;
    vec4 uRimDir;
    vec4 uRimCol;
    vec4 uDriftPos;
    vec4 uDriftCol;
    vec4 uSky;       // hemisphere light
    vec4 uGround;
    vec4 uSH[9];     // the room light at roughness 1 (env level 5) as 9 spherical harmonics (rgb)
};

layout (location = 0) out vec3 vW;
layout (location = 1) out vec3 vN;
layout (location = 2) out vec2 vUV;

void main() {
    vW = (uModel * vec4(aPos, 1.0)).xyz;
    vN = mat3(uModel) * aNrm;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
