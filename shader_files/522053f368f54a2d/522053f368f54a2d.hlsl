// 522053f368f54a2d.hlsl — Alpha-clipping shadow caster for alpha-blended geometry
//
// Replaces 522053f368f54a2d (119 tokens) — the shadow caster used for feather planes
// and other alpha-blended geometry that uses clip-space depth from the VS.
//
// Original register layout:
//   c0       = mask            (float4, alpha dot-product weights, usually {0,0,0,1})
//   c1       = uvIndex         (float, 0 = UV set 0 from TEXCOORD4.xy, else UV set 1 from .zw)
//   s0       = diffuseSampler
//   TEXCOORD2.zw = clip-space z and w  (depth = z/w)
//   TEXCOORD4    = packed UVs (xy = UV0, zw = UV1, channel selected by uvIndex)
//
// Bug in original: computes alpha via dot(texel, mask) and outputs it to oC0.w
// for D3D9 alpha testing — but alpha testing is not active on the R32F shadow
// render target, so transparent pixels still write depth and cast solid blocky
// shadows (especially visible on chocobo tail feathers).
//
// Fix: replace the alpha-test output path with an explicit clip(), matching the
// approach used in the e3ffdec520d824b2 replacement.
//
// Compile:
//   fxc /nologo /T ps_3_0 /E main /Fo 522053f368f54a2d.bin 522053f368f54a2d.hlsl

sampler2D diffuseSampler : register(s0);

float4 mask    : register(c0);
float  uvIndex : register(c1);

#define ALPHA_CLIP_THRESHOLD 0.5f

void main(float4 clipPos : TEXCOORD2,   // .z = clip-space z, .w = clip-space w
          float4 uvs     : TEXCOORD4,   // .xy = UV set 0, .zw = UV set 1
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // --- UV channel select (identical to original) -------------------------
    // uvIndex == 0 → UV set 0 (uvs.xy), non-zero → UV set 1 (uvs.zw)
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // --- Alpha from diffuse texture (identical to original) ----------------
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

    // --- Clip transparent pixels — this is the fix -------------------------
    // Original wrote alpha to oC0.w for D3D9 alpha test, which has no effect
    // on R32F render targets. Explicit clip() discards the fragment instead.
    clip(alpha - ALPHA_CLIP_THRESHOLD);

    // --- Depth from clip-space position (identical to original) ------------
    float depth = clipPos.z / clipPos.w;

    // --- Output ------------------------------------------------------------
    oColor = float4(depth, depth, depth, 1.0f);
    oDepth = depth;
}
