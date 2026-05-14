// 4ee03898b1bf663b — VFX merge shader: lerp between two samples of the same texture
// Alpha is already correct in the original (tex.a * vert.a), included here for completeness.
// Original: ~8 instruction slots
//
// Register layout:
//   c171 latitudeParam   c172 vfxVanmMergePercent   s0 sampler_00
//
// Vertex inputs:
//   v0=vertColor  v1.xy=UV0  v2.zw=UV1  v3.xy=clipZW(yx order)

sampler2D s0 : register(s0);

float4 latitudeParam        : register(c171);
float4 vfxVanmMergePercent  : register(c172);

void main(
    float4 v0 : TEXCOORD0,    // vertex color
    float4 v1 : TEXCOORD4,    // xy = UV0
    float4 v2 : TEXCOORD5,    // zw = UV1
    float2 v3 : TEXCOORD7,    // xy = clipZW (x=clipZ y=clipW, i.e. depth w then z)
    out float4 oC0    : COLOR,
    out float  oDepth : DEPTH)
{
    float4 sample0 = tex2D(s0, v1.xy);
    float4 sample1 = tex2D(s0, v2.zw);
    float4 blended = lerp(sample0, sample1, vfxVanmMergePercent.x);
    float4 scaled  = blended * v0;

    oC0.xyz = scaled.xyz * latitudeParam.x + latitudeParam.y;
    oC0.w   = scaled.w;

    // depth: original uses rcp(v3.y)*v3.x → clipZ/clipW
    oDepth  = v3.x / v3.y;
}
