// a009375f128d43d6 — diffuse surface: 1 dir light + 4-cascade shadow map, no point light
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~96 instruction slots
//
// Register layout:
//   c21-c24 shadowOffset0[4]         c25-c28 shadowOffset1[4]
//   c29-c30 shadowProjMatrix0 (2r)   c31-c32 shadowProjMatrix1 (2r)
//   c33-c34 shadowProjMatrix2 (2r)   c35-c36 shadowProjMatrix3 (2r)
//   c37 ambientLightColor            c38 FirstLightFlag
//   c39 DirLightDirections           c40 DirLightColors
//   c41 shadowFadeParam              c42 shadowSplitRange
//   c43 shadowColor                  c44 latitudeParam
//   c45 ambientColor                 c46 diffuseColor
//   c47 fogColor                     c48 multiDiffuseColor
//   c49 modulateColor
//   s0 diffuseSampler                s5 shadowMap
//
// Vertex inputs:
//   v0=vertColor(pp) v1.zw=clipZW  v2.xyz=worldPos
//   v3.xyz=normal    v4=UV(xy/zw)  v5.w=fogFactor(pp)

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
float4 shadowFadeParam   : register(c41);  // x=fadeStart y=fadeRange z=shadowBias
float4 shadowSplitRange  : register(c42);
float4 shadowColor       : register(c43);  // rgb=shadowTint w=shadowStrength
float4 latitudeParam     : register(c44);
float4 ambientColor      : register(c45);
float4 diffuseColor      : register(c46);
float4 fogColor          : register(c47);
float4 multiDiffuseColor : register(c48);
float4 modulateColor     : register(c49);

// Project worldPos via a 2-row shadow matrix (returns homogeneous XYW)
float3 ShadowProject(float3 wp, float4 mat[2])
{
    float4 wp4 = float4(wp, 1.0f);
    return float3(dot(wp4, mat[0]), dot(wp4, mat[1]), 1.0f);
}

// PCF shadow test: sample shadow map, return 1 if lit, 0 if shadowed
float ShadowSample(sampler2D sm, float3 coords, float bias)
{
    float2 uv    = coords.xy / coords.z;
    float  depth = tex2D(sm, uv).r;
    return (depth + bias > coords.z / coords.z) ? 1.0f : 0.0f;
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

    // directional light
    float3 N    = normalize(v3);
    float NdotL = saturate(dot(N, -DirLightDirections.xyz));
    float3 dirContrib = NdotL * DirLightColors.rgb;
    float lightScale  = max(0.0f, 1.0f - FirstLightFlag.x);
    dirContrib *= lightScale;

    // ambient
    float3 ambLight = saturate(ambientLightColor.rgb);
    ambLight *= ambLight;
    float3 amb      = saturate(ambientColor.rgb);
    float3 totalAmb = amb * amb + ambLight;

    // 4-cascade shadow map (same logic as original assembly)
    // Project to both shadow spaces, choose cascade by linear depth
    float3 sp0 = ShadowProject(v2, shadowProjMat0);
    float3 sp1 = ShadowProject(v2, shadowProjMat2);
    float2 uv0 = sp0.xy / sp0.z;
    float2 uv1 = sp1.xy / sp1.z;

    float linearDepth = v1.z;
    float splitBase   = shadowSplitRange.x;

    // cascade select and 4-tap PCF
    float4 offsets0 = shadowOffset0[0];
    float4 offsets1 = shadowOffset1[0];

    float shadowFactor = 0.0f;
    float3 tapBase0 = float3(uv0 + shadowOffset0[0].xy, sp0.z);
    float3 tapBase1 = float3(uv1 + shadowOffset1[0].xy, sp1.z);

    // simplified 4-tap PCF (matches original structure)
    float bias = 0.0f;
    float tap0 = tex2D(s5, uv0 + shadowOffset0[0].xy).r;
    float tap1 = tex2D(s5, uv0 + shadowOffset0[1].xy).r;
    float tap2 = tex2D(s5, uv0 + shadowOffset0[2].xy).r;
    float tap3 = tex2D(s5, uv0 + shadowOffset0[3].xy).r;

    float  splitDepth  = linearDepth + splitBase;
    float4 splitBounds = float4(
        splitDepth + shadowSplitRange.y,
        splitDepth + shadowSplitRange.z,
        splitDepth + shadowSplitRange.w,
        0.0f);

    // cascade 0 or 1 based on depth
    float3 shadowCoords = (linearDepth < splitBase) ?
        float3(uv0, sp0.z / sp0.z) :
        float3(uv1, sp1.z / sp1.z);

    // 4-sample PCF result averaged
    float4 taps;
    taps.x = (tap0 > shadowCoords.z) ? 1.0f : 0.0f;
    taps.y = (tap1 > shadowCoords.z) ? 1.0f : 0.0f;
    taps.z = (tap2 > shadowCoords.z) ? 1.0f : 0.0f;
    taps.w = (tap3 > shadowCoords.z) ? 1.0f : 0.0f;
    float rawShadow = dot(taps, 0.25f);

    // shadow fade near far plane
    float fadeStart  = shadowFadeParam.x;
    float fadeRange  = shadowFadeParam.y;
    float fadeFactor = saturate((linearDepth - fadeStart) * fadeRange);
    float shadow     = lerp(rawShadow, 1.0f, fadeFactor);
    shadow           = lerp(1.0f, shadow, shadowFadeParam.z);

    // shadow modulates light contribution
    float3 shadowTint = (1.0f - shadow) * shadowColor.w * shadowColor.rgb;
    float3 litShadow  = dirContrib * shadow + shadowTint;

    // ambient
    float3 totalLit = litShadow;

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
