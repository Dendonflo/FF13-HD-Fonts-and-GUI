// a6f4d5a5756cdb25 — diffuse surface: 1 dir light + 1 point light + 4-cascade shadow
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~114 instruction slots
//
// Register layout:
//   c21-c24 shadowOffset0[4]    c25-c28 shadowOffset1[4]
//   c29-c30 shadowProjMat0(2r)  c31-c32 shadowProjMat1(2r)
//   c33-c34 shadowProjMat2(2r)  c35-c36 shadowProjMat3(2r)
//   c37 ambientLightColor       c38 FirstLightFlag
//   c39 DirLightDirections      c40 DirLightColors
//   c41 PointLightPositions     c42 PointLightColors
//   c43 PointLightParams        c44 shadowFadeParam
//   c45 shadowSplitRange        c46 shadowColor
//   c47 latitudeParam           c48 ambientColor
//   c49 diffuseColor            c50 fogColor
//   c51 multiDiffuseColor       c52 modulateColor
//   s0 diffuseSampler           s5 shadowMap
//
// Vertex inputs:
//   v0=vertColor(pp)  v1.zw=clipZW  v2.xyz=worldPos
//   v3.xyz=normal     v4=UV(xy/zw)  v5.w=fogFactor(pp)

sampler2D s0 : register(s0);
sampler2D s5 : register(s5);

float4 shadowOffset0[4]  : register(c21);
float4 shadowOffset1[4]  : register(c25);
float4 shadowProjMat0[2] : register(c29);
float4 shadowProjMat1[2] : register(c31);
float4 shadowProjMat2[2] : register(c33);
float4 shadowProjMat3[2] : register(c35);
float4 ambientLightColor : register(c37);
float4 FirstLightFlag    : register(c38);
float4 DirLightDirections: register(c39);
float4 DirLightColors    : register(c40);
float4 PointLightPositions: register(c41);
float4 PointLightColors  : register(c42);
float4 PointLightParams  : register(c43);  // z=falloffExp w=radius
float4 shadowFadeParam   : register(c44);
float4 shadowSplitRange  : register(c45);
float4 shadowColor       : register(c46);
float4 latitudeParam     : register(c47);
float4 ambientColor      : register(c48);
float4 diffuseColor      : register(c49);
float4 fogColor          : register(c50);
float4 multiDiffuseColor : register(c51);
float4 modulateColor     : register(c52);

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

    // shadow: project world pos into both shadow spaces
    float4 wp4 = float4(v2, 1.0f);
    float3 sp0 = float3(dot(wp4, shadowProjMat0[0]), dot(wp4, shadowProjMat0[1]), dot(wp4, shadowProjMat2[0]));
    float3 sp1 = float3(dot(wp4, shadowProjMat1[0]), dot(wp4, shadowProjMat1[1]), dot(wp4, shadowProjMat2[1]));

    float linearDepth = v1.z;
    bool useSlice1    = (linearDepth > shadowSplitRange.x + shadowSplitRange.y);

    float3 shadowCoords = useSlice1 ? sp1 : sp0;

    // 4-tap PCF
    float4 taps;
    taps.x = tex2D(s5, shadowCoords.xy + (useSlice1 ? shadowOffset1[0].xy : shadowOffset0[0].xy)).r;
    taps.y = tex2D(s5, shadowCoords.xy + (useSlice1 ? shadowOffset1[1].xy : shadowOffset0[1].xy)).r;
    taps.z = tex2D(s5, shadowCoords.xy + (useSlice1 ? shadowOffset1[2].xy : shadowOffset0[2].xy)).r;
    taps.w = tex2D(s5, shadowCoords.xy + (useSlice1 ? shadowOffset1[3].xy : shadowOffset0[3].xy)).r;

    float ref       = shadowCoords.z;
    float rawShadow = dot(taps > ref, 0.25f);

    float fadeVal   = saturate((linearDepth - shadowFadeParam.x) * shadowFadeParam.y);
    float shadow    = lerp(rawShadow, 1.0f, fadeVal);
    shadow          = lerp(1.0f, shadow, shadowFadeParam.z);

    float3 shadowTint = (1.0f - shadow) * shadowColor.w * shadowColor.rgb;

    // directional light
    float3 N    = normalize(v3);
    float NdotL = saturate(dot(N, -DirLightDirections.xyz));
    float3 dirContrib = NdotL * DirLightColors.rgb * shadow + shadowTint;

    // point light
    float3 toLight  = PointLightPositions.xyz - v2;
    float  dist2    = dot(toLight, toLight);
    float  invDist  = rsqrt(dist2);
    float  dist     = 1.0f / invDist;
    float3 toLightN = toLight * invDist;
    float  atten    = max(0.0f, PointLightParams.w - dist) /
                      (pow(dist, PointLightParams.z) * PointLightParams.w);
    float NdotP     = saturate(dot(N, toLightN));
    float3 pointContrib = atten * PointLightColors.rgb * NdotP;

    // FirstLightFlag
    float2 flags    = max(0.0f, 1.0f - FirstLightFlag.xy);
    dirContrib     *= flags.x;
    pointContrib   *= flags.y;
    float3 totalLit = dirContrib + pointContrib;

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
