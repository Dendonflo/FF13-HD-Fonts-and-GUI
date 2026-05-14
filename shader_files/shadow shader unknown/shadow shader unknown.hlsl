// shadow_alpha_mask_clip_variant.hlsl
//
// Fix for shader 69d9822c85b46095
//
// Original behaviour:
// - samples diffuse texture
// - computes alpha via dot(texel, mask)
// - NEVER discards transparent pixels
// - still writes depth/output for fully transparent texels
//
// Result:
// alpha-card geometry (feathers, foliage, etc.) casts solid shadows.
//
// This replacement adds clip() so transparent texels do not survive.
//

sampler2D diffuseSampler : register(s0);

float4 mask       : register(c0);
float  uvIndex    : register(c1);
float4 colorScale : register(c2);

// ---------------------------------------------------------------------------

#define ALPHA_CLIP_THRESHOLD 0.5f

// ---------------------------------------------------------------------------

void main(float4 uvs : TEXCOORD4,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // Original UV selection logic
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // Original texture sample
    float4 texel = tex2D(diffuseSampler, uv);

    // Original alpha computation
    float alpha = saturate(dot(texel, mask));

    // ---------------------------------------------------------------------
    // FIX:
    // discard transparent texels so they do not write shadow/depth
    // ---------------------------------------------------------------------

    clip(alpha - ALPHA_CLIP_THRESHOLD);

    // Original outputs
    oColor = float4(0.0f, 0.0f, 0.0f, alpha) * colorScale;

    // Original shader wrote constant zero depth
    oDepth = 0.0f;
}