// dof_cutscene_near.hlsl — Improved near-blur pass for FF13-2 cutscene DoF
//
// Replaces 8b0d63a81922561b (first pass of the 3-stage cutscene DoF pipeline).
//
// Pipeline context:
//   1. This shader  (8b0d63a81922561b) — near-blur gather + near CoC
//   2. Spread pass  (b32c3447f7a5418c) — dilates the near-blur buffer
//   3. Compositor   (67f7f77335ff7a82) — blends sharp / near / far layers
//
// What this shader must output:
//   oC0.xyz = near-blurred color  (fed into the spread pass as s0)
//   oC0.w   = max near CoC from 4 depth samples (fed into compositor)
//
// RGB improvement: replaces the original 5-tap cross filter with a
// 19-tap circular kernel (rings of 6 + 12 taps + centre).
// Kernel radius is driven by BLUR_RADIUS_TEXELS scaled by the render
// target texel size (c2), so it is resolution-independent.
// Using c2.x / c2.y separately for X / Y naturally produces a circle
// in pixel space without needing an explicit aspect ratio correction.
//
// Alpha (near CoC): unchanged from original — samples depth at 4 half-texel
// corners, converts to CoC via s_nearScaleBias / s_nearLimit, outputs max.
//
// Registers (match original exactly):
//   s0  = color buffer
//   s1  = depth buffer
//   c0  = s_nearScaleBias  (float2: CoC = saturate(depth * c0.x + c0.y))
//   c1  = s_nearLimit      (float:  CoC *= c1)
//   c2  = s_textureSizeReciprocal (float2: 1/width, 1/height)
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo dof_cutscene_near.bin dof_cutscene_near.hlsl

sampler2D s_color               : register(s0);
sampler2D s_depth               : register(s1);

float2 s_nearScaleBias          : register(c0);
float  s_nearLimit              : register(c1);
float2 s_textureSizeReciprocal  : register(c2);

// ---------------------------------------------------------------------------
// Tweakable parameters
// ---------------------------------------------------------------------------

// Near-blur radius in texels. The original used 1 texel.
// 3.0 gives noticeably rounder, softer near-blur before the spread pass.
#define BLUR_RADIUS_TEXELS 3.0f

// ---------------------------------------------------------------------------

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // --- 19-tap circular near-blur gather --------------------------------
    // Using c2.xy as separate X/Y steps naturally gives a pixel-space circle.
    float2 s1uv = s_textureSizeReciprocal * (BLUR_RADIUS_TEXELS * 0.5f);  // inner ring
    float2 s2uv = s_textureSizeReciprocal *  BLUR_RADIUS_TEXELS;           // outer ring

    float4 sum = tex2D(s_color, uv);  // centre

    // Ring 1 — 6 taps, angles every 60 deg
    sum += tex2D(s_color, uv + float2( s1uv.x,         0.0f        ));
    sum += tex2D(s_color, uv + float2( s1uv.x * 0.5f,  s1uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2(-s1uv.x * 0.5f,  s1uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2(-s1uv.x,         0.0f        ));
    sum += tex2D(s_color, uv + float2(-s1uv.x * 0.5f, -s1uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2( s1uv.x * 0.5f, -s1uv.y * 0.866f));

    // Ring 2 — 12 taps, angles every 30 deg
    sum += tex2D(s_color, uv + float2( s2uv.x,           0.0f          ));
    sum += tex2D(s_color, uv + float2( s2uv.x * 0.866f,  s2uv.y * 0.5f ));
    sum += tex2D(s_color, uv + float2( s2uv.x * 0.5f,    s2uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2( 0.0f,              s2uv.y         ));
    sum += tex2D(s_color, uv + float2(-s2uv.x * 0.5f,    s2uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2(-s2uv.x * 0.866f,  s2uv.y * 0.5f ));
    sum += tex2D(s_color, uv + float2(-s2uv.x,           0.0f          ));
    sum += tex2D(s_color, uv + float2(-s2uv.x * 0.866f, -s2uv.y * 0.5f ));
    sum += tex2D(s_color, uv + float2(-s2uv.x * 0.5f,   -s2uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2( 0.0f,             -s2uv.y         ));
    sum += tex2D(s_color, uv + float2( s2uv.x * 0.5f,   -s2uv.y * 0.866f));
    sum += tex2D(s_color, uv + float2( s2uv.x * 0.866f, -s2uv.y * 0.5f ));

    float4 blurred = sum / 19.0f;

    // --- Near CoC for alpha (unchanged from original) --------------------
    // Sample depth at 4 half-texel corners, take max CoC.
    float2 h = s_textureSizeReciprocal * 0.5f;
    float d0 = tex2D(s_depth, uv + float2(-h.x, -h.y)).x;
    float d1 = tex2D(s_depth, uv + float2( h.x, -h.y)).x;
    float d2 = tex2D(s_depth, uv + float2(-h.x,  h.y)).x;
    float d3 = tex2D(s_depth, uv + float2( h.x,  h.y)).x;
    float4 cocs = saturate(float4(d0, d1, d2, d3) * s_nearScaleBias.x + s_nearScaleBias.y)
                  * s_nearLimit;
    float nearCoc = max(max(cocs.x, cocs.y), max(cocs.z, cocs.w));

    return float4(blurred.xyz, nearCoc);
}
