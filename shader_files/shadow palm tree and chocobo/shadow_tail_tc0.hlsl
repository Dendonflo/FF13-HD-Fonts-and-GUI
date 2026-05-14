// shadow_tail_tc0.hlsl — tail shadow variant: UV from TEXCOORD0
// Identical to the main shadow shader except the UV source.
// Deployed selectively for tail draws only (s0 == f1b8e915b3f2fc8a).
// Test order: tc3 → tc0 → tc2

sampler2D diffuseSampler            : register(s0);
float4 worldViewProjectionMatrix[4] : register(c0);
float4 mask                         : register(c4);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

#define ALPHA_CLIP_THRESHOLD 0.5f

void main(float4 tc0  : TEXCOORD0,
          float4 wPos : TEXCOORD1,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    float2 uv = tc0.xy;
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);
    clip(alpha - ALPHA_CLIP_THRESHOLD);

    float depth = wPos.x * worldViewProjectionMatrix[0].z
                + wPos.y * worldViewProjectionMatrix[1].z
                + wPos.z * worldViewProjectionMatrix[2].z
                + wPos.w * worldViewProjectionMatrix[3].z;
    depth += shadowBufferZBias;
    depth  = saturate(depth);

    float4 result = saturate(float4(depth, depth, depth, alpha));
    oColor = result * colorScale;
    oDepth = depth;
}
