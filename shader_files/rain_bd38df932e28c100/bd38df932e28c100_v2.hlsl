// bd38df932e28c100_v2.hlsl
// Rain VFX particle shader — soft-particle depth-masked rain streaks
//
// v1 fix : flat ALPHA_SCALE reduces per-particle opacity
// v2 adds : distance-based fade (via fog factor) so distant rain thins out
//           tex2Dgrad for the distorted UV lookup so mip selection is correct
//           regardless of how much the UV was warped by the rain texture

// ── Tuning ────────────────────────────────────────────────────────────────
// Base per-particle alpha scale (1.0 = original, lower = more transparent).
static const float ALPHA_SCALE = 0.35;

// Controls how aggressively alpha fades with distance.
// 1.0 = fully fades to zero at the fog limit. Lower values preserve more
// alpha at distance. Try 0.6–1.0.
static const float DIST_FADE_STRENGTH = 0.85;
// ─────────────────────────────────────────────────────────────────────────

sampler2D _sampler_00             : register(s0);  // main rain streak texture
sampler2D Texture_lookup_2d_1     : register(s1);  // distortion texture
sampler2D maskingMap              : register(s2);  // scene depth for soft-particle

float4 _sampler_00TexMatrix0      : register(c21);
float4 _sampler_00TexMatrix1      : register(c22);
// c23 = matrix row 2 — unused in this shader
float4 _sampler_00TexMatrix3      : register(c24);  // UV offset
float4 maskingMapMatrix0          : register(c25);
float4 maskingMapMatrix1          : register(c26);
float4 maskingMapMatrix2          : register(c27);
float4 maskingMapInvVP0           : register(c28);
float4 maskingMapInvVP1           : register(c29);
float4 maskingMapInvVP2           : register(c30);
float3 latitudeParam              : register(c31);  // contrast scale, offset, brightness
float4 fogColor                   : register(c32);  // rgb: fog colour, w: fog max
float4 _sampler_00TexColor        : register(c33);  // xy: distortion UV scale
float  distortionAmount           : register(c34);
float3 vfxDistortionTex_UVScroll  : register(c35);  // xy: UV scroll
float2 vfxDistortionTex_UVScale   : register(c36);

float4 main(
    float4 v0 : TEXCOORD0,   // vertex colour / modulate (RGBA)
    float4 v1 : TEXCOORD2,   // clip-space position for depth masking
    float2 v2 : TEXCOORD4,   // base UV
    float4 v3 : TEXCOORD6    // v3.w = per-vertex fog factor (0=near, 1=far)
) : COLOR0
{
    // ── Soft-particle depth mask ──────────────────────────────────────────
    float4 r0;
    r0.x = dot(maskingMapInvVP0, v1);
    r0.y = dot(maskingMapInvVP1, v1);
    r0.z = dot(maskingMapInvVP2, v1);
    r0.w = 1.0;

    float2 maskUV;
    maskUV.x      = dot(maskingMapMatrix0, r0);
    maskUV.y      = dot(maskingMapMatrix1, r0);
    float pDepth  = dot(maskingMapMatrix2, r0);

    float sceneDepth = tex2D(maskingMap, maskUV).x;
    float depthDiff  = sceneDepth - pDepth;   // >= 0 → particle in front of scene

    // ── Main rain texture (scrolling UV via texture matrix) ───────────────
    float3 uvh = float3(v2.x + 1.0, v2.x, v2.y);

    float2 texUV;
    texUV.x  = dot(_sampler_00TexMatrix0.xyw, uvh);
    texUV.y  = dot(_sampler_00TexMatrix1.xyw, uvh);
    texUV   += _sampler_00TexMatrix3.xy;

    float4 rainSample = tex2D(_sampler_00, texUV);

    // ── Distortion UV ─────────────────────────────────────────────────────
    float2 distUV;
    distUV  = rainSample.xy * _sampler_00TexColor.xy;
    distUV  = distUV * distortionAmount + v2;
    distUV  = distUV * vfxDistortionTex_UVScale + vfxDistortionTex_UVScroll.xy;

    // Mip level driven by undistorted v2 gradients so the distortion warp
    // doesn't confuse the hardware's automatic mip calculation.
    float2 dx = ddx(v2);
    float2 dy = ddy(v2);
    float4 r1 = tex2Dgrad(Texture_lookup_2d_1, distUV, dx, dy);

    // ── Compositing ───────────────────────────────────────────────────────
    float4 r2       = r1 * v0;
    float3 fogBlend = fogColor.rgb - v0.rgb * r1.rgb;

    // Depth gate + flat opacity reduction
    float outAlpha  = (depthDiff >= 0.0) ? r2.w : 0.0;
    outAlpha       *= ALPHA_SCALE;

    // Distance fade: fog factor v3.w goes 0 (near) → 1 (far).
    // Lerp between no fade and full fade based on DIST_FADE_STRENGTH.
    float distFade  = 1.0 - v3.w * DIST_FADE_STRENGTH;
    outAlpha       *= distFade;

    float fogAmount  = min(fogColor.w, v3.w);
    float3 outColor  = fogAmount * fogBlend + r2.rgb;
    outColor         = outColor * latitudeParam.x + latitudeParam.y;
    outColor        *= latitudeParam.z;

    return float4(outColor, outAlpha);
}
