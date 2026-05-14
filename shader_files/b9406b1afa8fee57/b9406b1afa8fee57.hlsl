// b9406b1afa8fee57 — diffuse surface: 1 directional light, no shadow, no point light
// ALPHA FIX: oC0.w = tex.a * vert.a, bypassing modulateColor.w
// Original: ~41 instruction slots
//
// Register layout (must match game's cbuffer bindings exactly):
//   c21 ambientLightColor   c22 FirstLightFlag   c23 DirLightDirections
//   c24 DirLightColors      c25 latitudeParam     c26 ambientColor
//   c27 diffuseColor        c28 fogColor          c29 multiDiffuseColor
//   c30 modulateColor       s0  diffuseSampler

sampler2D s0 : register(s0);

float4 ambientLightColor  : register(c21);
float4 FirstLightFlag     : register(c22);
float4 DirLightDirections : register(c23);
float4 DirLightColors     : register(c24);
float4 latitudeParam      : register(c25);
float4 ambientColor       : register(c26);
float4 diffuseColor       : register(c27);
float4 fogColor           : register(c28);
float4 multiDiffuseColor  : register(c29);
float4 modulateColor      : register(c30);

void main(
    float4 v0 : TEXCOORD0,   // vertex color rgba
    float4 v1 : TEXCOORD2,   // clip pos (z=clipZ, w=clipW)
    float3 v2 : TEXCOORD3,   // surface normal
    float4 v3 : TEXCOORD4,   // UV: xy=main zw=detail
    float4 v4 : TEXCOORD6,   // w=fog factor
    out float4 oC0    : COLOR,
    out float  oDepth : DEPTH)
{
    oDepth = v1.z / v1.w;

    // directional light
    float3 N = normalize(v2);
    float NdotL = saturate(dot(N, -DirLightDirections.xyz));
    float3 dirContrib = NdotL * DirLightColors.rgb;
    float lightScale = max(0.0f, 1.0f - FirstLightFlag.x);
    dirContrib *= lightScale;

    // ambient
    float3 ambLight = saturate(ambientLightColor.rgb);
    ambLight *= ambLight;
    float3 amb = saturate(ambientColor.rgb);
    float3 totalAmb = amb * amb + ambLight;

    // sample textures
    float4 detailSample = tex2D(s0, v3.zw);
    float4 diffSample   = tex2D(s0, v3.xy);

    // save alpha before any modulate (FIX)
    float alpha = diffSample.a * v0.a;

    // detail/diffuse blend (RGB only)
    float3 detailRGB = detailSample.rgb * multiDiffuseColor.rgb;
    float3 diffRGB   = diffSample.rgb * diffuseColor.rgb;
    float3 diffScale = diffRGB * 1.5f;
    float3 blended   = saturate(detailSample.a * (detailRGB * 1.5f - diffScale) + diffScale);
    blended *= blended;  // gamma square

    // combine lighting
    float3 litColor = dirContrib * blended + totalAmb * blended;
    litColor = max(litColor, 9.99999997e-7f);
    float3 sqrtLit = sqrt(litColor);

    // vertex color + fog
    float3 tonemapped = sqrtLit * v0.rgb;
    float3 fogDelta   = fogColor.rgb - tonemapped;
    float fogAmt      = min(saturate(fogColor.a), v4.w);
    float3 fogged     = fogAmt * fogDelta + tonemapped;

    // modulate RGB only — do NOT multiply alpha (FIX)
    float3 modRGB = fogged * modulateColor.rgb;

    // latitude transform
    float3 finalRGB = (modRGB * latitudeParam.x + latitudeParam.y) * latitudeParam.z;

    oC0 = float4(finalRGB, alpha);
}
