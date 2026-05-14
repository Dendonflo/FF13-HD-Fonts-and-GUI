// 4b82d418ba0f5339 — diffuse surface: 1 dir light + 1 point light, no shadow
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~58 instruction slots
//
// Register layout:
//   c21 ambientLightColor     c22 FirstLightFlag      c23 DirLightDirections
//   c24 DirLightColors        c25 PointLightPositions  c26 PointLightColors
//   c27 PointLightParams      c28 latitudeParam        c29 ambientColor
//   c30 diffuseColor          c31 fogColor             c32 multiDiffuseColor
//   c33 modulateColor         s0  diffuseSampler
//   v0=vertColor v1.zw=clipZW v2.xyz=worldPos v3.xyz=normal v4=UV v5.w=fogFactor

sampler2D s0 : register(s0);

float4 ambientLightColor    : register(c21);
float4 FirstLightFlag       : register(c22);
float4 DirLightDirections   : register(c23);
float4 DirLightColors       : register(c24);
float4 PointLightPositions  : register(c25);
float4 PointLightColors     : register(c26);
float4 PointLightParams     : register(c27);  // x=innerAngle y=outerAngle z=falloffExp w=radius
float4 latitudeParam        : register(c28);
float4 ambientColor         : register(c29);
float4 diffuseColor         : register(c30);
float4 fogColor             : register(c31);
float4 multiDiffuseColor    : register(c32);
float4 modulateColor        : register(c33);

void main(
    float4 v0 : TEXCOORD0,   // vertex color rgba (pp)
    float4 v1 : TEXCOORD2,   // clip pos zw
    float3 v2 : TEXCOORD1,   // world position
    float3 v3 : TEXCOORD3,   // surface normal
    float4 v4 : TEXCOORD4,   // UV: xy=main zw=detail
    float4 v5 : TEXCOORD6,   // w=fog factor (pp)
    out float4 oC0    : COLOR,
    out float  oDepth : DEPTH)
{
    oDepth = v1.z / v1.w;

    // point light
    float3 toLight   = PointLightPositions.xyz - v2;
    float  dist2     = dot(toLight, toLight);
    float  invDist   = rsqrt(dist2);
    float  dist      = 1.0f / invDist;
    float3 toLightN  = toLight * invDist;
    float  distPow   = pow(dist, PointLightParams.z);
    float  atten     = max(0.0f, PointLightParams.w - dist) / (distPow * PointLightParams.w);
    float3 pointContrib = atten * PointLightColors.rgb;

    // directional + point NdotL
    float3 N       = normalize(v3);
    float NdotP    = saturate(dot(N, toLightN));
    float NdotL    = saturate(dot(N, -DirLightDirections.xyz));
    float3 dirContrib = NdotL * DirLightColors.rgb;
    pointContrib *= NdotP;

    // FirstLightFlag scales (x for dir, y for point)
    float2 flags    = max(0.0f, 1.0f - FirstLightFlag.xy);
    pointContrib   *= flags.y;
    float3 totalLit = dirContrib * flags.x + pointContrib;

    // ambient
    float3 ambLight = saturate(ambientLightColor.rgb);
    ambLight *= ambLight;
    float3 amb = saturate(ambientColor.rgb);
    float3 totalAmb = amb * amb + ambLight;

    // sample textures
    float4 detailSample = tex2D(s0, v4.zw);
    float4 diffSample   = tex2D(s0, v4.xy);

    // save alpha before any modulate (FIX)
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

    // modulate RGB only — do NOT multiply alpha (FIX)
    float3 modRGB = fogged * modulateColor.rgb;

    float3 finalRGB = (modRGB * latitudeParam.x + latitudeParam.y) * latitudeParam.z;

    oC0 = float4(finalRGB, alpha);
}
