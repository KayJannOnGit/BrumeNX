// The consoles: the material three.js r160 uses in the mockup (MeshStandardMaterial, physical lights),
// ported function for function: GGX + Smith-correlated + Schlick, multiscattering IBL, hemisphere light,
// 2 directional lights + 1 point light (decay 2), the « room » environment (RoomEnvironment PMREM, baked
// by three.js itself), ACES Filmic tone mapping (exposure 0.78), sRGB output.
layout (std140, binding = 1) uniform Mesh {
    mat4 uMVP;
    mat4 uModel;
    vec4 uBase;
    vec4 uMat;
    vec4 uTint;
    vec4 uEmi;
    vec4 uCam;
    vec4 uKeyDir;
    vec4 uKeyCol;
    vec4 uRimDir;
    vec4 uRimCol;
    vec4 uDriftPos;
    vec4 uDriftCol;
    vec4 uSky;
    vec4 uGround;
    vec4 uSH[9];     // the room light at roughness 1 (env level 5) as 9 spherical harmonics (rgb)
    vec4 uJoyL;      // v40: the Switch's left Joy-Con colour (linear rgb), w = its palette cell (u), < 0 = as modelled
    vec4 uJoyR;
};
layout (location = 0) in vec3 vW;
layout (location = 1) in vec3 vN;
layout (location = 2) in vec2 vUV;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D uTex;    // base colour (BC1 sRGB: the sampler gives linear)
layout (binding = 1) uniform sampler2D uEnv;    // 64 x 192: 6 roughness levels (0, .2 ... 1) of 32 rows, longitude/latitude
layout (binding = 2) uniform sampler2D uHazeT;  // the blurred fog (what glass lets through)
layout (binding = 3) uniform sampler2D uEmiT;   // emissive map (the screens)

#define PI 3.141592653589793
#define RECIPROCAL_PI 0.3183098861837907
float pow2(float x) { return x * x; }

// ---- three.js r160: bsdfs / lights_physical_pars_fragment ----
vec3 F_Schlick(vec3 f0, float f90, float dotVH) {
    float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
    return f0 * (1.0 - fresnel) + (f90 * fresnel);
}
float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {
    float a2 = pow2(alpha);
    float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV));
    float gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));
    return 0.5 / max(gv + gl, 1e-6);
}
float D_GGX(float alpha, float dotNH) {
    float a2 = pow2(alpha);
    float denom = pow2(dotNH) * (a2 - 1.0) + 1.0;
    return RECIPROCAL_PI * a2 / pow2(denom);
}
vec3 BRDF_GGX(vec3 L, vec3 V, vec3 N, vec3 f0, float f90, float roughness) {
    float alpha = pow2(roughness);
    vec3 H = normalize(L + V);
    float dotNL = clamp(dot(N, L), 0.0, 1.0), dotNV = clamp(dot(N, V), 0.0, 1.0);
    float dotNH = clamp(dot(N, H), 0.0, 1.0), dotVH = clamp(dot(V, H), 0.0, 1.0);
    return F_Schlick(f0, f90, dotVH) * (V_GGX_SmithCorrelated(alpha, dotNL, dotNV) * D_GGX(alpha, dotNH));
}
// RE_Direct_Physical for one light
void DirectLight(vec3 L, vec3 C, vec3 N, vec3 V, vec3 diffuse, vec3 specular, float rough, inout vec3 dD, inout vec3 dS) {
    float nl = clamp(dot(N, L), 0.0, 1.0);
    if (nl <= 0.0) return;                               // a light behind the surface adds nothing (saves the GGX)
    vec3 irr = nl * C;
    dS += irr * BRDF_GGX(L, V, N, specular, 1.0, rough);
    dD += irr * RECIPROCAL_PI * diffuse;
}
vec2 DFGApprox(vec3 N, vec3 V, float roughness) {
    float dotNV = clamp(dot(N, V), 0.0, 1.0);
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}
void computeMultiscattering(vec3 N, vec3 V, vec3 specularColor, float specularF90, float roughness,
                            inout vec3 singleScatter, inout vec3 multiScatter) {
    vec2 fab = DFGApprox(N, V, roughness);
    vec3 FssEss = specularColor * fab.x + specularF90 * fab.y;
    float Ess = fab.x + fab.y, Ems = 1.0 - Ess;
    vec3 Favg = specularColor + (1.0 - specularColor) * 0.047619;
    vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
    singleScatter += FssEss;
    multiScatter += Fms * Ems;
}
// the baked environment (textureCubeUV of the mockup's PMREM at roughness 0, .2, .4, .6, .8, 1)
// RoomEnvironment is white (checked texel by texel), so one channel is enough: on the console, the 3-colour
// version of the room light came out red, then yellow, from grey inputs (a shader compiler fault) -> grey scalars.
float EnvAt(vec3 d, float rough) {
    float lv = clamp(rough, 0.0, 1.0) * 5.0, l0 = floor(lv), l1 = min(l0 + 1.0, 5.0);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = clamp((asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5) * 32.0, 0.5, 31.5);
    float a = textureLod(uEnv, vec2(u, (l0 * 32.0 + v) / 192.0), 0.0).r;
    float b = textureLod(uEnv, vec2(u, (l1 * 32.0 + v) / 192.0), 0.0).r;
    return mix(a, b, lv - l0);
}
// ACESFilmicToneMapping (three.js r160)
vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
vec3 ACESFilmic(vec3 color, float exposure) {
    const mat3 ACESInputMat = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
    const mat3 ACESOutputMat = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
    color *= exposure / 0.6;
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}
vec3 SRGBToLinear(vec3 c) { return mix(pow(c * 0.9478672986 + 0.0521327014, vec3(2.4)), c * 0.0773993808, lessThanEqual(c, vec3(0.04045))); }
vec3 LinearToSRGB(vec3 c) { return mix(pow(c, vec3(0.41666)) * 1.055 - 0.055, c * 12.92, lessThanEqual(c, vec3(0.0031308))); }

void main() {
    int flags = int(uMat.w + 0.5);
    vec4 base = uBase;
    if ((flags & 1) != 0) {
        vec3 tc = texture(uTex, vUV).rgb;
        if (uJoyL.w > 0.0 && vUV.y < 0.25) {          // v40: the Joy-Con bodies in the real controllers' colours
            if (abs(vUV.x - uJoyL.w) < 0.004) tc = uJoyL.rgb;        // v41: the vertices sit on the cell's edge (u = cell / 128)
            else if (abs(vUV.x - uJoyR.w) < 0.004) tc = uJoyR.rgb;
        }
        base.rgb *= tc;
    }
    vec3 N = normalize(vN);
    vec3 V = normalize(uCam.xyz - vW);
    if (uEmi.w > 0.5 && dot(N, V) < 0.0) N = -N;     // double-sided part seen from behind
    float metal = uMat.x;
    // three.js adds the normal's screen derivatives (geometryRoughness) here; dFdx/dFdy make uam emit a
    // constbuf1, and binding it faults the GPU in the HOME menu (GCC page fault at a garbage address): left out.
    float rough = min(max(uMat.y, 0.0525), 1.0);
    vec3 diffuse = base.rgb * (1.0 - metal);
    vec3 specular = mix(vec3(0.04), base.rgb, metal);

    vec3 dD = vec3(0.0), dS = vec3(0.0);
    // RE_Direct_Physical: the two directional lights and the drifting point light
    // (no arrays: indexed in a loop they go to local memory, which the HOME menu's GPU setup doesn't provide)
    vec3 toP = uDriftPos.xyz - vW; float dist = length(toP);
    DirectLight(uKeyDir.xyz, uKeyCol.rgb, N, V, diffuse, specular, rough, dD, dS);
    DirectLight(uRimDir.xyz, uRimCol.rgb, N, V, diffuse, specular, rough, dD, dS);
    DirectLight(toP / max(dist, 1e-4), uDriftCol.rgb / max(dist * dist, 0.01), N, V, diffuse, specular, rough, dD, dS);
    // hemisphere light -> indirect diffuse
    vec3 iD = mix(uGround.rgb, uSky.rgb, 0.5 * N.y + 0.5) * RECIPROCAL_PI * diffuse;
    // the room environment (envMapIntensity .35 on the consoles)
    // three: iblIrradiance = PI * textureCubeUV(envMap, N, 1.0). The texture read here came out red on the console
    // (test view 7), so the roughness-1 level is evaluated from 9 spherical harmonics instead (no texture, cheaper).
    float shN = uSH[0].x * 0.282095
              + (uSH[1].x * N.y + uSH[2].x * N.z + uSH[3].x * N.x) * 0.488603
              + (uSH[4].x * (N.x * N.y) + uSH[5].x * (N.y * N.z) + uSH[7].x * (N.x * N.z)) * 1.092548
              + uSH[6].x * (0.315392 * (3.0 * N.z * N.z - 1.0)) + uSH[8].x * (0.546274 * (N.x * N.x - N.y * N.y));
    float cw = max(shN, 0.0) * uMat.z;                   // = iblIrradiance / PI (grey)
    vec3 R = normalize(mix(reflect(-V, N), N, rough * rough));
    float radiance = EnvAt(R, rough) * uMat.z;
    // computeMultiscattering (three r160), inlined
    vec2 fab = DFGApprox(N, V, rough);
    vec3 ss = specular * fab.x + vec3(fab.y);
    float Ess = fab.x + fab.y, Ems = 1.0 - Ess;
    vec3 Favg = specular + (1.0 - specular) * 0.047619;
    vec3 ms = ss * Favg / (1.0 - Ems * Favg) * Ems;
    vec3 total = ss + ms;
    vec3 iS = ss * radiance + ms * cw;
    vec3 envD = diffuse * (max(0.0, 1.0 - dot(total, vec3(0.3333333))) * cw);   // three: diffuseColor * (1 - totalScattering)
    iD += envD;

    vec3 emissive = uEmi.rgb;
    if ((flags & 4) != 0) emissive *= texture(uEmiT, vUV).rgb;   // three: totalEmissiveRadiance *= emissiveMap
    vec3 col = dD + iD + dS + iS + emissive;
    if ((flags & 2) != 0) {                              // glass (« Ports natifs » cards): the fog seen through it
        vec3 bg = SRGBToLinear(textureLod(uHazeT, gl_FragCoord.xy / uScreen.xy, 0.0).rgb);
        col = bg * pow(max(uTint.rgb, vec3(1e-4)), vec3(uTint.w)) + dS + iS;
    }
    outColor = vec4(LinearToSRGB(ACESFilmic(col, uCam.w)), base.a);
    // test views (ZR on the wheel): 1 = texture alone, 2 = the room light alone (should be grey), 3 = the lamps alone
    int dbg = int(uSky.w + 0.5);
    if (dbg == 1) outColor = vec4(LinearToSRGB(clamp(base.rgb, 0.0, 1.0)), 1.0);
    if (dbg == 2) outColor = vec4(LinearToSRGB(vec3(clamp(EnvAt(N, 1.0) * 0.4, 0.0, 1.0))), 1.0);
    if (dbg == 6) outColor = vec4(LinearToSRGB(ACESFilmic(iS * 4.0, uCam.w)), 1.0);        // shiny reflection of the room alone
    if (dbg == 7) outColor = vec4(LinearToSRGB(ACESFilmic(envD, uCam.w)), 1.0);            // the room's diffuse light alone
    if (dbg == 8) outColor = vec4(LinearToSRGB(clamp(emissive, 0.0, 1.0)), 1.0);          // glow alone (black except screens)
    if (dbg == 9) outColor = vec4(LinearToSRGB(clamp(col * 0.8, 0.0, 1.0)), 1.0);         // everything, without the tone mapping
    if (dbg == 3) outColor = vec4(LinearToSRGB(ACESFilmic(dD + dS + iD - envD, uCam.w)), 1.0);
}
