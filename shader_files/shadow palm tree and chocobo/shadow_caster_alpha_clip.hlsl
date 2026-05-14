// shadow_caster_alpha_clip.hlsl — Alpha-clipping shadow caster for FF13-2
//
// Replaces e3ffdec520d824b2 (alpha-aware shadow caster, 13 instructions).
//
// Original register layout preserved exactly:
//   c0-c3  = worldViewProjectionMatrix  (light MVP, 4 × float4 rows)
//   c4     = mask                       (float4, alpha dot-product weights)
//   c5     = uvIndex                    (float, selects xy vs zw UV channel)
//   c6     = shadowBufferZBias          (float)
//   c7     = colorScale                 (float4)
//   s0     = diffuseSampler
//   TEXCOORD1 = world-space position    (for depth computation)
//   TEXCOORD4 = UV coords (xy + zw)     (channel selected by uvIndex)
//
// The fix: the original computes alpha via dot(texel, mask) and writes it
// to oC0.w, but never discards transparent pixels — they still write depth
// into the shadow buffer and cast solid shadows where there should be none.
// This replacement adds clip() to discard them, matching the behaviour of
// the D3D11 translation path's explicit 'discard'.
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo shadow_caster_alpha_clip.bin shadow_caster_alpha_clip.hlsl

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);  // c0..c3
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

// ---------------------------------------------------------------------------
// Tweakable
// ---------------------------------------------------------------------------

#define ALPHA_CLIP_THRESHOLD 0.5f

// ---------------------------------------------------------------------------

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD4,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // --- UV channel select (identical to original) ------------------------
    // abs(uvIndex) == 0 → first UV set (xy), non-zero → second set (zw)
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // --- Alpha from diffuse texture (identical to original) ---------------
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

    // --- Clip transparent pixels — this is the fix -----------------------
    // Original never discarded; we match the D3D11 path's discard behaviour.
    clip(alpha - ALPHA_CLIP_THRESHOLD);

    // --- Depth in light space (identical to original) --------------------
    float depth = wPos.x * worldViewProjectionMatrix[0].z
                + wPos.y * worldViewProjectionMatrix[1].z
                + wPos.z * worldViewProjectionMatrix[2].z
                + wPos.w * worldViewProjectionMatrix[3].z;
    depth += shadowBufferZBias;
    depth  = saturate(depth);

    // --- Output (identical to original) ----------------------------------
    // oC0 = saturate(float4(depth, depth, depth, alpha)) * colorScale
    float4 result = saturate(float4(depth, depth, depth, alpha));
    oColor = result * colorScale;
    oDepth = depth;
}
