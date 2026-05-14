// af31d3405062ee68.hlsl — Gameplay DoF replacement: colour grade only, no blur
//
// Preserves the full depth-based colour matrix composite from the original
// shader, but removes all blurring. Every pixel stays sharp; the CoC value
// still drives the lerp between the ungraded sharp pixel and the colour-
// matrix-graded pixel, so depth-dependent atmospheric effects (rain
// darkening, etc.) are retained exactly as the game intends.
//
// Sampler / register layout is identical to the original:
//   s0        = color buffer (sharp scene)
//   s1        = depth buffer
//   c0..c3    = s_colorMatrix[4]
//   c4        = s_blur (unused here)
//   c5.xy     = s_rangeValue  (CoC = saturate(depth * c5.y + c5.x))
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo af31d3405062ee68.bin af31d3405062ee68.hlsl

sampler2D s_color       : register(s0);
sampler2D s_depth       : register(s1);

float4 s_colorMatrix[4] : register(c0);   // c0..c3
float4 s_blur           : register(c4);   // unused
float2 s_rangeValue     : register(c5);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Depth-based Circle of Confusion — same formula as original.
    float depth = tex2D(s_depth, uv).x;
    float coc   = saturate(depth * s_rangeValue.y + s_rangeValue.x);

    // Sharp centre pixel only — no gather, no blur.
    float4 sharp = tex2D(s_color, uv);

    // Colour matrix applied to the sharp pixel (preserves rain darkening,
    // colour grading, and any other depth-driven atmospheric composite).
    float3 c;
    c  = sharp.x * s_colorMatrix[0].xyz;
    c += sharp.y * s_colorMatrix[1].xyz;
    c += sharp.z * s_colorMatrix[2].xyz;
    c  = saturate(c + s_colorMatrix[3].xyz);

    // Blend: CoC=0 → sharp ungraded, CoC=1 → sharp colour-graded.
    float3 result = lerp(sharp.xyz, c, coc);

    return float4(result, 1.0f);
}
