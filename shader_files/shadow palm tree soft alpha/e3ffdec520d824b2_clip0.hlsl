// e3ffdec520d824b2_clip0.hlsl — Shadow caster with clip threshold at 0
//
// Matches the D3D9 alpha test the game sets for the shadow pass:
//   D3DRS_ALPHAFUNC  = GREATER
//   D3DRS_ALPHAREF   = 0
// i.e. discard only pixels where alpha == 0 exactly (< 1/255 in 8-bit).
// Every non-zero alpha pixel writes depth and casts shadow.
//
// Register layout identical to original e3ffdec520d824b2:
//   c0-c3  = worldViewProjectionMatrix
//   c4     = mask
//   c5     = uvIndex
//   c6     = shadowBufferZBias
//   c7     = colorScale
//   s0     = diffuseSampler
//   TEXCOORD1 = world-space position
//   TEXCOORD4 = UV coords (xy + zw)
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo e3ffdec520d824b2_clip0.bin e3ffdec520d824b2_clip0.hlsl
// Deploy:
//   Copy as hd_textures_shaders\e3ffdec520d824b2.bin

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD4,
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    float2 uv  = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

    // Discard only fully transparent pixels (alpha < 1/255).
    // Matches D3DRS_ALPHAFUNC=GREATER, D3DRS_ALPHAREF=0.
    clip(alpha - (1.0f / 255.0f));

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
