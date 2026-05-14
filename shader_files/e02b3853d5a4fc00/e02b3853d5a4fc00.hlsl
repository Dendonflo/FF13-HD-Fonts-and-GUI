// e02b3853d5a4fc00 — diffuse surface: 1 dir light + 2 point lights (loop) + 4-cascade shadow
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~130 instruction slots, uses rep i0 (loop count=2) for point lights
//
// Register layout:
//   c21-c24 shadowOffset0[4]      c25-c28 shadowOffset1[4]
//   c29-c30 PointLightPositions[2] c31-c32 PointLightColors[2]
//   c33-c34 PointLightParams[2]    c35-c36 shadowProjMat0(2r)
//   c37-c38 shadowProjMat1(2r)     c39-c40 shadowProjMat2(2r)
//   c41-c42 shadowProjMat3(2r)
//   c43 ambientLightColor  c44 FirstLightFlag   c45 DirLightDirections
//   c46 DirLightColors     c47 shadowFadeParam  c48 shadowSplitRange
//   c49 shadowColor        c50 latitudeParam    c51 ambientColor
//   c52 diffuseColor       c53 fogColor         c54 multiDiffuseColor
//   c55 modulateColor      i0=2 (loop count)
//   s0 diffuseSampler      s5 shadowMap
//
// Vertex inputs:
//   v0=vertColor(pp)  v1.zw=clipZW  v2.xyz=worldPos
//   v3.xyz=normal     v4=UV(xy/zw)  v5.w=fogFactor(pp)

sampler2D s0 : register(s0);
sampler2D s5 : register(s5);

float4 shadowOffset0[4]     : register(c21);
float4 shadowOffset1[4]     : register(c25);
float4 PointLightPositions[2]: register(c29);
float4 PointLightColors[2]  : register(c31);
float4 PointLightParams[2]  : register(c33);
float4 shadowProjMat0[2]    : register(c35);
float4 shadowProjMat1[2]    : register(c37);
float4 shadowProjMat2[2]    : register(c39);
float4 shadowProjMat3[2]    : register(c41);
float4 ambientLightColor    : register(c43);
float4 FirstLightFlag       : register(c44);
float4 DirLightDirections   : register(c45);
float4 DirLightColors       : register(c46);
float4 shadowFadeParam      : register(c47);
float4 shadowSplitRange     : register(c48);
float4 shadowColor          : register(c49);
float4 latitudeParam        : register(c50);
float4 ambientColor         : register(c51);
float4 diffuseColor         : register(c52);
float4 fogColor             : register(c53);
float4 multiDiffuseColor    : register(c54);
float4 modulateColor        : register(c55);

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

    float4 taps;
    float4 ofs0 = useSlice1 ? shadowOffset1[0] : shadowOffset0[0];
    float4 ofs1 = useSlice1 ? shadowOffset1[1] : shadowOffset0[1];
    float4 ofs2 = useSlice1 ? shadowOffset1[2] : shadowOffset0[2];
    float4 ofs3 = useSlice1 ? shadowOffset1[3] : shadowOffset0[3];
    taps.x = tex2D(s5, shadowUV + ofs0.xy).r;
    taps.y = tex2D(s5, shadowUV + ofs1.xy).r;
    taps.z = tex2D(s5, shadowUV + ofs2.xy).r;
    taps.w = tex2D(s5, shadowUV + ofs3.xy).r;

    float rawShadow = dot(taps > shadowZ, 0.25f);
    float fadeVal   = saturate((linearDepth - shadowFadeParam.x) * shadowFadeParam.y);
    float shadow    = lerp(rawShadow, 1.0f, fadeVal);
    shadow          = lerp(1.0f, shadow, shadowFadeParam.z);
    float3 shadowTint = (1.0f - shadow) * shadowColor.w * shadowColor.rgb;

    // directional light (scaled by shadow)
    float3 N    = normalize(v3);
    float NdotL = saturate(dot(N, -DirLightDirections.xyz));
    float3 dirContrib = NdotL * DirLightColors.rgb * shadow + shadowTint;
    float lightScale  = max(0.0f, 1.0f - FirstLightFlag.x);
    dirContrib *= lightScale;

    // 2 point lights (unrolled from the rep i0 loop)
    float3 pointTotal = 0.0f;
    [unroll]
    for (int j = 0; j < 2; j++)
    {
        float3 toLight  = PointLightPositions[j].xyz - v2;
        float  dist2    = dot(toLight, toLight);
        float  invDist  = rsqrt(dist2);
        float  dist     = 1.0f / invDist;
        float3 toLightN = toLight * invDist;
        float  atten    = max(0.0f, PointLightParams[j].w - dist) /
                          (pow(dist, PointLightParams[j].z) * PointLightParams[j].w);
        float NdotP     = saturate(dot(N, toLightN));
        float3 pc       = atten * PointLightColors[j].rgb * NdotP;
        // FirstLightFlag scaling for point lights (j=0 uses flag.y, j=1 uses flag.z per original)
        float flagScale = max(0.0f, 1.0f - FirstLightFlag[j + 1]);
        pointTotal     += pc * flagScale;
    }

    float3 totalLit = dirContrib + pointTotal;

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
