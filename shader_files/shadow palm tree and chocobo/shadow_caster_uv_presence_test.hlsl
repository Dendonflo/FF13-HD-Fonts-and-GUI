// shadow_caster_uv_presence_test.hlsl — DIAGNOSTIC: is TC4 zeroed for tail draws?
//
// Instead of clipping on texture alpha, clips based on whether TC4.xy is
// near (0,0). If TC4 is zeroed for tail feather shadow draws, those shadow
// polygons will disappear. If TC4 carries valid UVs, they won't.
//
// Expected outcomes:
//   A) Tail blocky shadows DISAPPEAR, head/palm unaffected
//      → TC4 IS zeroed for tail draws. UV-zero hypothesis confirmed.
//        Next step: find which TEXCOORD slot the tail shadow VS outputs UVs to.
//
//   B) Tail blocky shadows PERSIST
//      → TC4 is NOT zeroed. The tail shadow VS IS outputting real UVs to TC4,
//        but the texture at those UVs happens to be fully opaque.
//        Next step: investigate mask values or s0 texture binding.
//
//   C) Head/palm shadows ALSO disappear or glitch
//      → Head/palm TC4.xy also has many (0,0) texels (wrapping UV at origin),
//        which is a false positive. Widen the threshold (change 0.001 to 0.0001).
//
// Compile:
//   fxc /nologo /T ps_3_0 /E main /Fo shadow_caster_uv_presence_test.bin shadow_caster_uv_presence_test.hlsl

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
    // --- Normal UV select (same as working shader) --------------------------
    float2 uv = abs(uvIndex) > 0.0f ? uvs.zw : uvs.xy;

    // --- Clip if UV is (0,0): TC4 was not populated by the shadow VS --------
    // If TC4 is zeroed, uv.x + uv.y == 0.0 exactly.
    // Using a tiny epsilon to account for floating-point, not for wrapping UVs
    // (legitimate UV=0,0 corners are rare but possible — if false positives
    // appear on valid geometry, lower to 0.0 for exact-zero only).
    clip(uv.x + uv.y - 0.001f);

    // --- Everything below is unchanged from working shader ------------------
    float4 texel = tex2D(diffuseSampler, uv);
    float  alpha = dot(texel, mask);

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
