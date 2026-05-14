// bd5c5fd231ee8968.hlsl — test: halve alpha output to confirm this is the rain shader
//
// Original: sample texture, multiply by vertex color (2 instructions)
// This version: same but multiplies output alpha by 0.5 to test if rain opacity changes.
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo bd5c5fd231ee8968.bin bd5c5fd231ee8968.hlsl

sampler2D s_sampler : register(s0);

float4 main(float4 vColor : COLOR0, float2 uv : TEXCOORD0) : COLOR0
{
    float4 tex = tex2D(s_sampler, uv);
    float4 result = tex * vColor;
    result.a *= 0.5f;
    return result;
}
