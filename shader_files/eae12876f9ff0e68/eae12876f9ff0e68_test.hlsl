// eae12876f9ff0e68_test.hlsl — test: is this the rain particle shader?
//
// VFX particle pixel shader using latitudeParam/fogColor framework.
// Original reconstructed logic:
//   tex = sample s0
//   tex *= c23 (texColor, {1,1,1,1} at runtime → identity)
//   rgb = lerp(tex*v0.rgb, c22.rgb, fogFactor)  (c22.w=0 → no fog → tex*v0.rgb)
//   a   = tex.a * c23.a * v0.a
//   rgb = rgb * c21.z + c21.y  (c21={1,0,1} → identity)
//
// This test: multiply output alpha by 0.2 to make rain nearly transparent.
// If rain visibly disappears/dims → confirmed shader.
//
// Compile:
//   fxc /T ps_3_0 /E main /Fo eae12876f9ff0e68.bin eae12876f9ff0e68_test.hlsl

sampler2D s0 : register(s0);

float4 c21 : register(c21);  // latitudeParam: {x=brightnessScale, y=brightnessAdd, z=finalScale, w=?}
float4 c22 : register(c22);  // fogColor: {rgb=fogColor, w=fogStrength}
float4 c23 : register(c23);  // texColor/tint: {rgba}

float4 main(
    float4 v0  : TEXCOORD0,   // vertex modulate color (rgb) + depth-fade alpha (w)
    float4 v1  : TEXCOORD4,   // UV (xy)
    float4 v2  : TEXCOORD6    // fog coord (w)
) : COLOR0
{
    float4 tex = tex2D(s0, v1.xy);
    float4 modulated = tex * c23;        // apply texture color tint

    float fogFactor = min(c22.w, v2.w);  // fog factor (0 when c22.w=0)

    float4 result;
    result.rgb = lerp(modulated.rgb * v0.rgb, c22.rgb, fogFactor);
    result.rgb = result.rgb * c21.z + c21.y;  // latitude: brightness adjust
    result.a   = modulated.a * v0.a;

    // TEST: halve alpha to confirm this is rain
    result.a *= 0.2f;

    return result;
}
