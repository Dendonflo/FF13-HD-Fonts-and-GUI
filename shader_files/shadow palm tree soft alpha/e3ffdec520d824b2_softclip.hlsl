// e3ffdec520d824b2_softclip.hlsl — Soft shadow caster via low clip threshold
//
// Replaces e3ffdec520d824b2 (alpha-aware shadow caster, 13 instructions).
// This is a SEPARATE variant — the original (hard clip at 0.5) lives in
//   shader_files\shadow palm tree and chocobo\
//
// How it works:
//   Lowers the clip threshold from 0.5 to ALPHA_CLIP_THRESHOLD (default 0.1).
//   Any pixel with alpha > 0.1 will write its depth to the shadow buffer,
//   including semi-transparent fringe pixels.  The shadow buffer is R32F, so
//   all written pixels produce a full-strength shadow regardless of alpha —
//   this does NOT produce graduated opacity, only a wider/softer cutout edge.
//
//   Compare with _bayer variant: bayer gives true stippled graduation of alpha
//   across all pixels; this variant gives a uniformly-drawn fringe region.
//   Use whichever looks better in game.
//
// Register layout (identical to original e3ffdec520d824b2):
//   c0-c3  = worldViewProjectionMatrix  (light MVP, 4 × float4 rows)
//   c4     = mask                       (float4, alpha dot-product weights)
//   c5     = uvIndex                    (float, selects xy vs zw UV channel)
//   c6     = shadowBufferZBias          (float)
//   c7     = colorScale                 (float4)
//   s0     = diffuseSampler
//   TEXCOORD1 = world-space position    (for depth computation)
//   TEXCOORD4 = UV coords (xy + zw)     (channel selected by uvIndex)
//
// Compile (from this folder):
//   fxc /T ps_3_0 /E main /Fo e3ffdec520d824b2_softclip.bin e3ffdec520d824b2_softclip.hlsl
// Deploy:
//   Copy e3ffdec520d824b2_softclip.bin to hd_textures_shaders\e3ffdec520d824b2.bin
//   (rename on copy — hash filename is what the runtime looks for)

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);  // c0..c3
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

// ---------------------------------------------------------------------------
// Tweakable — lower values include more fringe pixels in the shadow
// ---------------------------------------------------------------------------
#define ALPHA_CLIP_THRESHOLD 0.1f

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD4,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // --- UV channel select ---------------------------------------------------
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // --- Alpha from diffuse texture ------------------------------------------
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

    // --- Low-threshold clip — keeps fringe pixels that 0.5 would discard ----
    clip(alpha - ALPHA_CLIP_THRESHOLD);

    // --- Depth in light space ------------------------------------------------
    float depth = wPos.x * worldViewProjectionMatrix[0].z
                + wPos.y * worldViewProjectionMatrix[1].z
                + wPos.z * worldViewProjectionMatrix[2].z
                + wPos.w * worldViewProjectionMatrix[3].z;
    depth += shadowBufferZBias;
    depth  = saturate(depth);

    // --- Output --------------------------------------------------------------
    float4 result = saturate(float4(depth, depth, depth, alpha));
    oColor = result * colorScale;
    oDepth = depth;
}
