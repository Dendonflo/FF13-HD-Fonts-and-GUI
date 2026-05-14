// eae12876f9ff0e68_red.hlsl — identity test: does this shader cover rain?
// Outputs solid red (1,0,0,1). If rain turns red -> shader confirmed.
// If only the time travel menu previews turn red -> wrong path for rain.

sampler2D s0 : register(s0);

float4 main(
    float4 v0 : TEXCOORD0,
    float4 v1 : TEXCOORD4,
    float4 v2 : TEXCOORD6
) : COLOR0
{
    return float4(1, 0, 0, 1);
}
