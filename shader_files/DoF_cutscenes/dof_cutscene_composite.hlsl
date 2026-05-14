// dof_cutscene_composite.hlsl — Final DoF compositor for FF13-2 cutscene DoF
//
// Replaces 67f7f77335ff7a82 (step 3 of 3).
//
// Inputs:
//   s0  = sharp colour buffer
//   s1  = depth buffer
//   s2  = near-blur + spread buffer (step 2 output: RGB = blurred, A = near CoC)
//
// Registers match original exactly:
//   c0  = s_farScaleBias          (float2: CoC = saturate(depth*c0.x + c0.y))
//   c1  = s_farLimit              (float)
//   c2  = s_textureSizeReciprocal (float2: 1/w, 1/h)
//
// Blend fix:
//   Original used an additive formula (sharp + nearBlur * weight) which
//   over-brightened the image and ignored far DoF entirely when near CoC was 0.
//   Replacement uses two clean lerps:
//     1. lerp(sharp, farBlur, farCoc)       — far field
//     2. lerp(above,  nearBlur, nearCoc)    — near field on top
//   Both CoC values are saturated before use as a safety clamp against any
//   residual overflow from the spread pass.
//
// Far blur improvement:
//   Original: 5-tap box at ±0.5/1.5 texels (very narrow, resolution-dependent).
//   Replacement: 9-tap at ±1.5 texels (wider, still cheap) for a softer far field.
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo dof_cutscene_composite.bin dof_cutscene_composite.hlsl

sampler2D s_color              : register(s0);
sampler2D s_depth              : register(s1);
sampler2D s_nearBlur           : register(s2);

float2 s_farScaleBias          : register(c0);
float  s_farLimit              : register(c1);
float2 s_textureSizeReciprocal : register(c2);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 ts = s_textureSizeReciprocal;

    // --- Sharp centre sample ---------------------------------------------
    float3 sharp = tex2D(s_color, uv).xyz;

    // --- Far blur: 9-tap gather from sharp buffer ------------------------
    // Samples at 8 surrounding positions + centre, each equally weighted.
    float3 farBlur = sharp;
    farBlur += tex2D(s_color, uv + float2(-1.5f, -1.5f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2( 0.0f, -1.5f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2( 1.5f, -1.5f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2(-1.5f,  0.0f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2( 1.5f,  0.0f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2(-1.5f,  1.5f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2( 0.0f,  1.5f) * ts).xyz;
    farBlur += tex2D(s_color, uv + float2( 1.5f,  1.5f) * ts).xyz;
    farBlur /= 9.0f;

    // --- CoC values ------------------------------------------------------
    float depth   = tex2D(s_depth, uv).x;
    float farCoc  = saturate(depth * s_farScaleBias.x + s_farScaleBias.y)
                    * s_farLimit;

    float4 nearBlurSample = tex2D(s_nearBlur, uv);
    float  nearCoc        = saturate(nearBlurSample.w);  // clamp: safety against overflow

    // --- Composite -------------------------------------------------------
    // Layer 1: far field — blend sharp toward far blur
    float3 result = lerp(sharp, farBlur, farCoc);

    // Layer 2: near field — blend result toward near blur
    // Near blur takes priority over far blur (it's closer to camera).
    result = lerp(result, nearBlurSample.xyz, nearCoc);

    return float4(result, 1.0f);
}
