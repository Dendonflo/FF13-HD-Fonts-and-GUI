// e3ffdec520d824b2_bayer.hlsl — Soft shadow caster via ordered Bayer dithering
//
// Replaces e3ffdec520d824b2 (alpha-aware shadow caster, 13 instructions).
// This is a SEPARATE variant — the original (hard clip at 0.5) lives in
//   shader_files\shadow palm tree and chocobo\
//
// How it works:
//   Instead of clipping all pixels below a fixed 0.5 threshold, we use a 4×4
//   Bayer ordered-dither matrix keyed on the screen pixel position (VPOS).
//   The Bayer threshold at each pixel is in [0, 1], so:
//     alpha ≥ threshold → pixel is drawn (writes depth)
//     alpha <  threshold → pixel is clipped (no shadow contribution)
//   This produces a stipple pattern whose density scales continuously with
//   alpha — the result approximates soft, feathered shadow edges rather than
//   a hard binary cutout.
//
//   Because the shadow buffer is R32F (single depth value), true sub-pixel
//   alpha blending of depth is not possible.  Ordered dithering is the best
//   approximation available without multi-sample shadow maps.
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
//   VPOS      = screen pixel position   (ps_3_0 — required for dithering)
//
// Compile (from this folder):
//   fxc /T ps_3_0 /E main /Fo e3ffdec520d824b2_bayer.bin e3ffdec520d824b2_bayer.hlsl
// Deploy:
//   Copy e3ffdec520d824b2_bayer.bin to hd_textures_shaders\e3ffdec520d824b2.bin
//   (rename on copy — hash filename is what the runtime looks for)

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);  // c0..c3
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

// ---------------------------------------------------------------------------
// 4×4 Bayer ordered-dither matrix, values normalised to (0, 1).
// Entry [row][col] gives the threshold for that pixel position mod 4.
// ---------------------------------------------------------------------------
static const float kBayer[4][4] = {
    {  0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0 },
    { 12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0 },
    {  3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0 },
    { 15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0 }
};

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD4,
          float2 vpos : VPOS,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // --- UV channel select ---------------------------------------------------
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // --- Alpha from diffuse texture ------------------------------------------
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

    // --- Ordered Bayer dither ------------------------------------------------
    // Screen pixel index mod 4 selects the Bayer threshold.
    // alpha == 1 → always above threshold → always drawn
    // alpha == 0 → always below threshold → always clipped
    // 0 < alpha < 1 → fraction of pixels drawn proportional to alpha
    int ix = (int)fmod(vpos.x, 4.0);
    int iy = (int)fmod(vpos.y, 4.0);
    float threshold = kBayer[iy][ix];
    clip(alpha - threshold);

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
