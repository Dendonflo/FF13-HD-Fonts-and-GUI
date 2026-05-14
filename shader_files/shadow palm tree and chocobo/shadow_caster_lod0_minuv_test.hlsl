// shadow_caster_lod0_minuv_test.hlsl — DIAGNOSTIC: force mip 0, try both UV channels
//
// Two changes from working shader:
//   1. tex2Dlod(..., float4(uv, 0, 0)) forces the finest mip (LOD=0).
//      If the shadow VS doesn't output proper UV derivatives, tex2D might
//      select a very coarse mip where all punch-through transparent pixels
//      have averaged into opaque blocks.  LOD=0 bypasses that.
//
//   2. Samples BOTH TC4.xy and TC4.zw at LOD=0, takes the MINIMUM alpha.
//      If uvIndex is choosing the wrong channel (non-zero when tail UVs are
//      in .xy, or vice-versa), the min catches transparency from either set.
//
// If tail feather shadows fix: one or both of these was the problem.
// If still solid: the texture on s0 for tail draws is genuinely fully opaque
//   (wrong texture bound — investigate material/draw-call setup).
//
// Compile:
//   fxc /nologo /T ps_3_0 /E main /Fo shadow_caster_lod0_minuv_test.bin shadow_caster_lod0_minuv_test.hlsl

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

#define ALPHA_CLIP_THRESHOLD 0.5f

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD4,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // Sample BOTH UV sets at LOD=0 (bypass derivative-based mip selection)
    float4 texel_xy = tex2Dlod(diffuseSampler, float4(uvs.xy, 0, 0));
    float4 texel_zw = tex2Dlod(diffuseSampler, float4(uvs.zw, 0, 0));

    float alpha_xy = dot(texel_xy, mask);
    float alpha_zw = dot(texel_zw, mask);

    // Take the minimum: if EITHER UV set sees a transparent pixel, clip.
    float alpha = min(alpha_xy, alpha_zw);

    clip(alpha - ALPHA_CLIP_THRESHOLD);

    float depth = wPos.x * worldViewProjectionMatrix[0].z
                + wPos.y * worldViewProjectionMatrix[1].z
                + wPos.z * worldViewProjectionMatrix[2].z
                + wPos.w * worldViewProjectionMatrix[3].z;
    depth += shadowBufferZBias;
    depth  = saturate(depth);

    float4 result = saturate(float4(depth, depth, depth, alpha));
    oColor = result * colorScale;
    oDepth = depth;
}
