// dof_bokeh.hlsl — High-quality circular bokeh DoF replacement for FF13-2
//
// Replaces the original 4-tap DoF gather with a 37-tap circular kernel
// (3 concentric rings: 6 + 12 + 18 taps + 1 centre).
//
// Sampler / register layout matches the original shader exactly so the
// game's constant uploads continue to work without any CPU-side changes:
//
//   s0        = color buffer  (sharp pre-DoF scene)
//   s1        = depth buffer
//   c0..c3    = color matrix  (s_colorMatrix[4])
//   c4        = s_blur        (original offset params, unused in this version)
//   c5.xy     = s_rangeValue  (depth-to-CoC: CoC = saturate(depth * c5.y + c5.x))
//
// Output blending matches original:
//   result = lerp(blurred, colorMatrix(blurred), CoC)
//   → CoC=0 : sharp pixel, no colour grade
//   → CoC=1 : blurred pixel with full colour grade
//
// Compile with:
//   fxc /T ps_3_0 /E main /Fo dof_bokeh.bin dof_bokeh.hlsl

sampler2D s_color       : register(s0);
sampler2D s_depth       : register(s1);

float4 s_colorMatrix[4] : register(c0);   // c0..c3
float4 s_blur           : register(c4);   // original blur params (unused)
float2 s_rangeValue     : register(c5);

// ---------------------------------------------------------------------------
// Tweakable parameters
// ---------------------------------------------------------------------------

// Aspect ratio correction: multiplied on X offsets so bokeh stays circular.
// 16:9 → 9/16 = 0.5625. Adjust if running at a different aspect ratio.
#define ASPECT_X 0.5625f

// ---------------------------------------------------------------------------

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // --- Circle of Confusion from depth ----------------------------------
    float depth      = tex2D(s_depth, uv).x;
    float coc        = saturate(depth * s_rangeValue.y + s_rangeValue.x);
    // Use the game's own blur radius (length of the original tap offset vector).
    // This preserves per-scene DoF intensity set by the game's CPU side.
    float gameRadius = length(s_blur.xy);
    float r          = coc * gameRadius;

    // --- 37-tap circular gather ------------------------------------------
    // Three concentric rings at r*0.333, r*0.667 and r*1.0.
    // When coc=0, r=0 and all taps collapse to the centre texel (sharp).

    float4 sum = tex2D(s_color, uv);  // centre tap

    // Ring 1 — 6 taps, radius r*0.333, angles every 60 deg
    float r1 = r * 0.333f;
    sum += tex2D(s_color, uv + float2( r1 * ASPECT_X,         0.000f      ));
    sum += tex2D(s_color, uv + float2( r1 * ASPECT_X * 0.5f,  r1 * 0.866f ));
    sum += tex2D(s_color, uv + float2(-r1 * ASPECT_X * 0.5f,  r1 * 0.866f ));
    sum += tex2D(s_color, uv + float2(-r1 * ASPECT_X,         0.000f      ));
    sum += tex2D(s_color, uv + float2(-r1 * ASPECT_X * 0.5f, -r1 * 0.866f ));
    sum += tex2D(s_color, uv + float2( r1 * ASPECT_X * 0.5f, -r1 * 0.866f ));

    // Ring 2 — 12 taps, radius r*0.667, angles every 30 deg
    float r2 = r * 0.667f;
    sum += tex2D(s_color, uv + float2( r2 * ASPECT_X,          0.000f      ));
    sum += tex2D(s_color, uv + float2( r2 * ASPECT_X * 0.866f, r2 * 0.5f  ));
    sum += tex2D(s_color, uv + float2( r2 * ASPECT_X * 0.5f,   r2 * 0.866f ));
    sum += tex2D(s_color, uv + float2( 0.000f,                  r2          ));
    sum += tex2D(s_color, uv + float2(-r2 * ASPECT_X * 0.5f,   r2 * 0.866f ));
    sum += tex2D(s_color, uv + float2(-r2 * ASPECT_X * 0.866f, r2 * 0.5f  ));
    sum += tex2D(s_color, uv + float2(-r2 * ASPECT_X,          0.000f      ));
    sum += tex2D(s_color, uv + float2(-r2 * ASPECT_X * 0.866f,-r2 * 0.5f  ));
    sum += tex2D(s_color, uv + float2(-r2 * ASPECT_X * 0.5f,  -r2 * 0.866f ));
    sum += tex2D(s_color, uv + float2( 0.000f,                 -r2          ));
    sum += tex2D(s_color, uv + float2( r2 * ASPECT_X * 0.5f,  -r2 * 0.866f ));
    sum += tex2D(s_color, uv + float2( r2 * ASPECT_X * 0.866f,-r2 * 0.5f  ));

    // Ring 3 — 18 taps, radius r*1.0, angles every 20 deg
    // cos/sin values pre-computed for 0,20,40,...,340 degrees
    sum += tex2D(s_color, uv + float2( r * ASPECT_X,           0.000f      ));  //   0
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.940f,  r * 0.342f  ));  //  20
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.766f,  r * 0.643f  ));  //  40
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.500f,  r * 0.866f  ));  //  60
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.174f,  r * 0.985f  ));  //  80
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.174f,  r * 0.985f  ));  // 100
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.500f,  r * 0.866f  ));  // 120
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.766f,  r * 0.643f  ));  // 140
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.940f,  r * 0.342f  ));  // 160
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X,           0.000f      ));  // 180
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.940f, -r * 0.342f  ));  // 200
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.766f, -r * 0.643f  ));  // 220
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.500f, -r * 0.866f  ));  // 240
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.174f, -r * 0.985f  ));  // 260
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.174f, -r * 0.985f  ));  // 280
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.500f, -r * 0.866f  ));  // 300
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.766f, -r * 0.643f  ));  // 320
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.940f, -r * 0.342f  ));  // 340

    float4 blurred = sum / 37.0f;

    // --- Color matrix (preserves original colour grading) ----------------
    float3 c;
    c  = blurred.x * s_colorMatrix[0].xyz;
    c += blurred.y * s_colorMatrix[1].xyz;
    c += blurred.z * s_colorMatrix[2].xyz;
    c  = saturate(c + s_colorMatrix[3].xyz);

    // Blend: CoC=0 → sharp pixel (no grade), CoC=1 → graded blurred pixel.
    // Matches the original shader's blending formula exactly.
    float3 result = lerp(blurred.xyz, c, coc);

    return float4(result, 1.0f);
}
