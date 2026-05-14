// dof_cutscene_spread.hlsl — Near-blur spread / dilation pass for FF13-2 cutscene DoF
//
// Replaces b32c3447f7a5418c (step 2 of 3).
//
// Input s0 = step-1 near-blur buffer:
//   RGB = near-blurred colour
//   A   = near CoC (from dof_cutscene_near / original gather pass)
//
// Registers match original exactly:
//   s0  = color sampler
//   c0  = s_textureSizeReciprocal (float2: 1/w, 1/h)
//
// RGB change: none — original weights sum to 1.0 and are kept verbatim
//   centre = 0.5,  inner 4 taps = 0.1 each,  outer 8 taps = 0.0125 each
//
// Alpha (CoC) fix:
//   Original: 2 × max(blended_CoC, centre_CoC) − centre_CoC
//   This dilation formula is correct in intent but was not clamped, so it
//   could produce values > 1.0 which fed the compositor out-of-range CoC
//   and caused characters/objects to appear over-blurred.
//   Fix: wrap the result in saturate() — identical dilation, range [0,1].
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo dof_cutscene_spread.bin dof_cutscene_spread.hlsl

sampler2D s_color                : register(s0);
float2    s_textureSizeReciprocal : register(c0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 ts = s_textureSizeReciprocal;

    // --- Spread kernel (tap positions identical to original) ----------------

    // Outer ring — 8 taps, each weighted 0.0125
    float4 o1 = tex2D(s_color, uv + float2(-2.5f, -1.5f) * ts);
    float4 o2 = tex2D(s_color, uv + float2(-1.5f,  2.5f) * ts);
    float4 o3 = tex2D(s_color, uv + float2( 2.5f,  1.5f) * ts);
    float4 o4 = tex2D(s_color, uv + float2( 1.5f, -2.5f) * ts);
    float4 o5 = tex2D(s_color, uv + float2(-0.5f, -3.5f) * ts);
    float4 o6 = tex2D(s_color, uv + float2(-3.5f,  0.5f) * ts);
    float4 o7 = tex2D(s_color, uv + float2( 0.5f,  3.5f) * ts);
    float4 o8 = tex2D(s_color, uv + float2( 3.5f, -0.5f) * ts);

    // Inner ring — 4 taps, each weighted 0.1
    float4 i1 = tex2D(s_color, uv + float2(-0.5f, -1.5f) * ts);
    float4 i2 = tex2D(s_color, uv + float2(-1.5f,  0.5f) * ts);
    float4 i3 = tex2D(s_color, uv + float2( 0.5f,  1.5f) * ts);
    float4 i4 = tex2D(s_color, uv + float2( 1.5f, -0.5f) * ts);

    // Centre — weight 0.5
    float4 centre = tex2D(s_color, uv);

    // --- RGB blend (unchanged from original; weights sum to 1.0) -----------
    float4 outerSum = o1 + o2 + o3 + o4 + o5 + o6 + o7 + o8;
    float4 innerSum = i1 + i2 + i3 + i4;

    float3 rgb = centre.xyz * 0.5f
               + innerSum.xyz * 0.1f
               + outerSum.xyz * 0.0125f;

    // --- CoC dilation (fixed: saturate prevents overflow) ------------------
    float blendedCoc = centre.w  * 0.5f
                     + innerSum.w * 0.1f
                     + outerSum.w * 0.0125f;

    float dilatedCoc = saturate(2.0f * max(blendedCoc, centre.w) - centre.w);

    return float4(rgb, dilatedCoc);
}
