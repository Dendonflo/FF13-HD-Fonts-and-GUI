// 5e4c28d7859bc12a — diffuse surface: 4 dynamic lights (bool-gated) + 4-cascade shadow
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~319 instruction slots; uses b8-b15 (8 bool regs, 2 per light)
//
// Per-light bit convention (e.g. light 0: b8=bit0, b9=bit1):
//   bit0=1 bit1=1 → spot light (position + cone check)
//   bit0=0 bit1=1 → directional (NdotL only, no position)
//   bit0=1 bit1=0 → point light (position + attenuation, no cone)
//   bit0=0 bit1=0 → disabled
//
// Register layout:
//   c21-c24 lightDirections[4]      c25-c28 lightPositions[4]
//   c29-c32 lightColors[4]          c33-c36 lightParams[4] (z=falloffExp, w=radius, xy=cone)
//   c37-c40 shadowOffset0[4]        c41-c44 shadowOffset1[4]
//   c45-c46 shadowProjMat0(2r)      c47-c48 shadowProjMat1(2r)
//   c49-c50 shadowProjMat2(2r)      c51-c52 shadowProjMat3(2r)
//   c53 ambientLightColor  c54 shadowFadeParam  c55 shadowSplitRange
//   c56 shadowColor        c57 latitudeParam    c58 ambientColor
//   c59 diffuseColor       c60 fogColor         c61 multiDiffuseColor
//   c62 modulateColor
//   s0 diffuseSampler      s5 shadowMap
//
// Vertex inputs:
//   v0=vertColor(pp)  v1.zw=clipZW  v2.xyz=worldPos
//   v3.xyz=normal     v4=UV(xy/zw)  v5.w=fogFactor(pp)

sampler2D s0 : register(s0);
sampler2D s5 : register(s5);

float4 lightDirections[4]   : register(c21);
float4 lightPositions[4]    : register(c25);
float4 lightColors[4]       : register(c29);
float4 lightParams[4]       : register(c33);  // x=innerAngle y=outerAngle z=falloffExp w=radius
float4 shadowOffset0[4]     : register(c37);
float4 shadowOffset1[4]     : register(c41);
float4 shadowProjMat0[2]    : register(c45);
float4 shadowProjMat1[2]    : register(c47);
float4 shadowProjMat2[2]    : register(c49);
float4 shadowProjMat3[2]    : register(c51);
float4 ambientLightColor    : register(c53);
float4 shadowFadeParam      : register(c54);
float4 shadowSplitRange     : register(c55);
float4 shadowColor          : register(c56);
float4 latitudeParam        : register(c57);
float4 ambientColor         : register(c58);
float4 diffuseColor         : register(c59);
float4 fogColor             : register(c60);
float4 multiDiffuseColor    : register(c61);
float4 modulateColor        : register(c62);

// Boolean light-type flags (2 bits per light)
bool light0bit0 : register(b8);
bool light0bit1 : register(b9);
bool light1bit0 : register(b10);
bool light1bit1 : register(b11);
bool light2bit0 : register(b12);
bool light2bit1 : register(b13);
bool light3bit0 : register(b14);
bool light3bit1 : register(b15);

// Evaluate one light's contribution given its two mode bits.
// bit0=1 bit1=1 → spot  |  bit0=0 bit1=1 → dir  |  bit0=1 bit1=0 → point  |  both 0 → off
float3 EvalLight(bool bit0, bool bit1, int idx, float3 N, float3 worldPos)
{
    float3 contrib = 0.0f;
    if (bit0) {
        if (bit1) {
            // spot light: position + cone falloff
            float3 toLight  = lightPositions[idx].xyz - worldPos;
            float  dist2    = dot(toLight, toLight);
            float  invDist  = rsqrt(dist2);
            float  dist     = 1.0f / invDist;
            float3 toLightN = toLight * invDist;
            float  distPow  = pow(dist, lightParams[idx].z);
            float  atten    = max(0.0f, lightParams[idx].w - dist) /
                              (distPow * lightParams[idx].w);
            // spot cone: dot(lightDir, -toLightN) compared against inner/outer angles
            float cosTheta  = dot(-lightDirections[idx].xyz, -toLightN);
            float spotInner = lightParams[idx].x;
            float spotRange = lightParams[idx].y - spotInner;
            float spotAtten = saturate((cosTheta - spotInner) / max(spotRange, 0.001f));
            float NdotL     = saturate(dot(N, toLightN));
            contrib         = atten * spotAtten * lightColors[idx].rgb * NdotL;
        } else {
            // point light: position + attenuation, no cone
            float3 toLight  = lightPositions[idx].xyz - worldPos;
            float  dist2    = dot(toLight, toLight);
            float  invDist  = rsqrt(dist2);
            float  dist     = 1.0f / invDist;
            float3 toLightN = toLight * invDist;
            float  distPow  = pow(dist, lightParams[idx].z);
            float  atten    = max(0.0f, lightParams[idx].w - dist) /
                              (distPow * lightParams[idx].w);
            float NdotL     = saturate(dot(N, toLightN));
            contrib         = atten * lightColors[idx].rgb * NdotL;
        }
    } else {
        if (bit1) {
            // directional only
            float NdotL = saturate(dot(N, -lightDirections[idx].xyz));
            contrib     = NdotL * lightColors[idx].rgb;
        }
        // else: disabled, contrib stays 0
    }
    return contrib;
}

void main(
    float4 v0 : TEXCOORD0,
    float4 v1 : TEXCOORD2,
    float3 v2 : TEXCOORD1,
    float3 v3 : TEXCOORD3,
    float4 v4 : TEXCOORD4,
    float4 v5 : TEXCOORD6,
    out float4 oC0    : COLOR,
    out float  oDepth : DEPTH)
{
    oDepth = v1.z / v1.w;

    // shadow
    float4 wp4 = float4(v2, 1.0f);
    float2 uv0 = float2(dot(wp4, shadowProjMat0[0]), dot(wp4, shadowProjMat0[1]));
    float2 uv1 = float2(dot(wp4, shadowProjMat1[0]), dot(wp4, shadowProjMat1[1]));
    float  z0  = dot(wp4, shadowProjMat2[0]);
    float  z1  = dot(wp4, shadowProjMat2[1]);

    float  linearDepth = v1.z;
    bool   useSlice1   = (linearDepth > shadowSplitRange.x + shadowSplitRange.y);
    float2 shadowUV    = useSlice1 ? uv1 : uv0;
    float  shadowZ     = useSlice1 ? z1  : z0;

    float4 o0 = useSlice1 ? shadowOffset1[0] : shadowOffset0[0];
    float4 o1 = useSlice1 ? shadowOffset1[1] : shadowOffset0[1];
    float4 o2 = useSlice1 ? shadowOffset1[2] : shadowOffset0[2];
    float4 o3 = useSlice1 ? shadowOffset1[3] : shadowOffset0[3];

    float4 taps;
    taps.x = tex2D(s5, shadowUV + o0.xy).r;
    taps.y = tex2D(s5, shadowUV + o1.xy).r;
    taps.z = tex2D(s5, shadowUV + o2.xy).r;
    taps.w = tex2D(s5, shadowUV + o3.xy).r;

    float rawShadow = dot(taps > shadowZ, 0.25f);
    float fadeVal   = saturate((linearDepth - shadowFadeParam.x) * shadowFadeParam.y);
    float shadow    = lerp(rawShadow, 1.0f, fadeVal);
    shadow          = lerp(1.0f, shadow, shadowFadeParam.z);
    float3 shadowTint = (1.0f - shadow) * shadowColor.w * shadowColor.rgb;

    // sum all 4 lights
    float3 N = normalize(v3);
    float3 totalLit = 0.0f;
    totalLit += EvalLight(light0bit0, light0bit1, 0, N, v2);
    totalLit += EvalLight(light1bit0, light1bit1, 1, N, v2);
    totalLit += EvalLight(light2bit0, light2bit1, 2, N, v2);
    totalLit += EvalLight(light3bit0, light3bit1, 3, N, v2);

    // apply shadow to the total (shadow attenuates all, adds shadow tint)
    totalLit = totalLit * shadow + shadowTint;

    // ambient
    float3 ambLight = saturate(ambientLightColor.rgb);
    ambLight *= ambLight;
    float3 amb      = saturate(ambientColor.rgb);
    float3 totalAmb = amb * amb + ambLight;

    // sample textures
    float4 detailSample = tex2D(s0, v4.zw);
    float4 diffSample   = tex2D(s0, v4.xy);

    // save alpha (FIX)
    float alpha = diffSample.a * v0.a;

    // detail/diffuse blend
    float3 detailRGB = detailSample.rgb * multiDiffuseColor.rgb;
    float3 diffRGB   = diffSample.rgb * diffuseColor.rgb;
    float3 diffScale = diffRGB * 1.5f;
    float3 blended   = saturate(detailSample.a * (detailRGB * 1.5f - diffScale) + diffScale);
    blended *= blended;

    float3 litColor = totalLit * blended + totalAmb * blended;
    litColor = max(litColor, 9.99999997e-7f);
    float3 sqrtLit  = sqrt(litColor);

    float3 tonemapped = sqrtLit * v0.rgb;
    float3 fogDelta   = fogColor.rgb - tonemapped;
    float fogAmt      = min(saturate(fogColor.a), v5.w);
    float3 fogged     = fogAmt * fogDelta + tonemapped;

    // modulate RGB only (FIX)
    float3 modRGB   = fogged * modulateColor.rgb;
    float3 finalRGB = (modRGB * latitudeParam.x + latitudeParam.y) * latitudeParam.z;

    oC0 = float4(finalRGB, alpha);
}
