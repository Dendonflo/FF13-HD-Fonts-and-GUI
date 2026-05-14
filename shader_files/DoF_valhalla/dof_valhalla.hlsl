// dof_valhalla.hlsl — High-quality circular bokeh DoF for FF13-2 Valhalla fight
//
// Replaces f206fbc4baadd122.
// Same 37-tap circular kernel as dof_bokeh.hlsl but register layout differs
// from the gameplay shader — no color matrix, different constant slots:
//
//   s0     = color buffer (sharp pre-DoF scene)
//   s1     = depth buffer
//   c0     = s_blur        (blur offset vectors; length(c0.xy) = game blur radius)
//   c1     = s_rangeValue  (depth-to-CoC: CoC = saturate(depth * c1.y + c1.x))
//
// Output: averaged blurred color, no color matrix (matches original).
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo dof_valhalla.bin dof_valhalla.hlsl

sampler2D s_color       : register(s0);
sampler2D s_depth       : register(s1);

float4 s_blur           : register(c0);
float2 s_rangeValue     : register(c1);

// ---------------------------------------------------------------------------
// Tweakable parameters
// ---------------------------------------------------------------------------

// Aspect ratio correction: multiplied on X offsets so bokeh stays circular.
// 16:9 → 9/16 = 0.5625.
#define ASPECT_X 0.5625f

// ---------------------------------------------------------------------------

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // --- Circle of Confusion from depth ----------------------------------
    float depth      = tex2D(s_depth, uv).x;
    float coc        = saturate(depth * s_rangeValue.y + s_rangeValue.x);
    float gameRadius = length(s_blur.xy);
    float r          = coc * gameRadius;

    // --- 37-tap circular gather ------------------------------------------
    float4 sum = tex2D(s_color, uv);  // centre

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
    sum += tex2D(s_color, uv + float2( r * ASPECT_X,           0.000f      ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.940f,  r * 0.342f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.766f,  r * 0.643f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.500f,  r * 0.866f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.174f,  r * 0.985f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.174f,  r * 0.985f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.500f,  r * 0.866f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.766f,  r * 0.643f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.940f,  r * 0.342f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X,           0.000f      ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.940f, -r * 0.342f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.766f, -r * 0.643f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.500f, -r * 0.866f  ));
    sum += tex2D(s_color, uv + float2(-r * ASPECT_X * 0.174f, -r * 0.985f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.174f, -r * 0.985f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.500f, -r * 0.866f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.766f, -r * 0.643f  ));
    sum += tex2D(s_color, uv + float2( r * ASPECT_X * 0.940f, -r * 0.342f  ));

    float4 blurred = sum / 37.0f;

    // No color matrix — output averaged color directly (matches original).
    return float4(blurred.xyz, 1.0f);
}
