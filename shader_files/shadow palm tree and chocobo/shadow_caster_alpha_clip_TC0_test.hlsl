// shadow_caster_alpha_clip_TC0_test.hlsl — DIAGNOSTIC VARIANT
//
// Identical to shadow_caster_alpha_clip.hlsl EXCEPT:
//   UVs are read from TEXCOORD0 instead of TEXCOORD4.
//
// Purpose: test the hypothesis that the tail-feather shadow VS does not
// populate TEXCOORD4 (leaving it zero → UV 0,0 → always opaque pixel →
// clip never fires → blocky shadow).  If the tail feather VS outputs UVs
// to TEXCOORD0 instead, reading from there should give correct transparent
// pixels and clip them away.
//
// Expected outcomes:
//   - If tail shadows now have transparency BUT head/palm shadows break
//     → tail VS uses TEXCOORD0; head VS uses TEXCOORD4
//   - If tail shadows still solid
//     → TEXCOORD0 also zeroed; try TEXCOORD2.xy or check uvIndex value
//   - If everything breaks (depth wrong too)
//     → VS doesn't output positions to TEXCOORD1 either; different VS
//
// To deploy: compile below, copy .bin over e3ffdec520d824b2.bin in
//   hd_textures_shaders\, relaunch game.  Revert to original .bin when done.
//
// Compile:
//   fxc /nologo /T ps_3_0 /E main /Fo shadow_caster_alpha_clip_TC0_test.bin shadow_caster_alpha_clip_TC0_test.hlsl

sampler2D diffuseSampler : register(s0);

float4 worldViewProjectionMatrix[4] : register(c0);  // c0..c3
float4 mask                         : register(c4);
float  uvIndex                      : register(c5);
float  shadowBufferZBias            : register(c6);
float4 colorScale                   : register(c7);

#define ALPHA_CLIP_THRESHOLD 0.5f

void main(float4 wPos : TEXCOORD1,
          float4 uvs  : TEXCOORD0,    // <-- changed from TEXCOORD4 to TEXCOORD0
          out float4 oColor : COLOR0,
          out float  oDepth : DEPTH)
{
    // UV channel select — same logic, but sourced from TEXCOORD0
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

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
