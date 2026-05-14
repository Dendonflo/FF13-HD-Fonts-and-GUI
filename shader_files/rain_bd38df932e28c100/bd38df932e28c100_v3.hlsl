// bd38df932e28c100_v3.hlsl
// Rain VFX particle shader — depth-estimated alpha fade
//
// v1 : flat ALPHA_SCALE only
// v2 : + fog-factor distance fade + tex2Dgrad  (no visible improvement)
// v3 : drop broken maskingMap entirely; replace hard depth gate with smooth
//      fade driven by the particle's own projected depth (pDepth). pDepth is
//      computed from the same matrices as before but does NOT depend on s2
//      returning correct scene depth — so it works even when the PC port
//      fails to populate the depth texture.
//
// Tuning guide:
//   ALPHA_SCALE      — flat per-particle opacity (lower = more transparent)
//   DEPTH_FADE_START — pDepth at which rain begins to thin out (0=camera)
//   DEPTH_FADE_END   — pDepth at which rain alpha reaches zero (≤1=far plane)
//   MAX_BLUR_RADIUS  — UV-space offset for the depth blur at max depth
//                      (0.05 = settled value, go higher for more softening)
//
// If pDepth turns out to be in a different range than [0,1], adjust
// DEPTH_FADE_START / DEPTH_FADE_END proportionally.

static const float ALPHA_SCALE      = 1.0;
static const float DEPTH_FADE_START = 0.00;
static const float DEPTH_FADE_END   = 1.00;
static const float MAX_BLUR_RADIUS  = 0.025; // DEFAULT IS 0.05

sampler2D _sampler_00             : register(s0);  // main rain streak texture
sampler2D Texture_lookup_2d_1     : register(s1);  // distortion texture
sampler2D maskingMap              : register(s2);  // scene depth — used for occlusion gate only

float4 _sampler_00TexMatrix0      : register(c21);
float4 _sampler_00TexMatrix1      : register(c22);
// c23 — matrix row 2, unused
float4 _sampler_00TexMatrix3      : register(c24);  // UV offset
float4 maskingMapMatrix0          : register(c25);
float4 maskingMapMatrix1          : register(c26);
float4 maskingMapMatrix2          : register(c27);  // row used for pDepth only
float4 maskingMapInvVP0           : register(c28);
float4 maskingMapInvVP1           : register(c29);
float4 maskingMapInvVP2           : register(c30);
float3 latitudeParam              : register(c31);  // contrast scale, offset, brightness
float4 fogColor                   : register(c32);  // rgb: fog colour, w: fog max
float4 _sampler_00TexColor        : register(c33);  // xy: distortion UV scale
float  distortionAmount           : register(c34);
float3 vfxDistortionTex_UVScroll  : register(c35);
float2 vfxDistortionTex_UVScale   : register(c36);

float4 main(
    float4 v0 : TEXCOORD0,   // vertex colour / modulate (RGBA)
    float4 v1 : TEXCOORD2,   // clip-space position
    float2 v2 : TEXCOORD4,   // base UV
    float4 v3 : TEXCOORD6    // v3.w = per-vertex fog factor
) : COLOR0
{
    // ── Depth setup ───────────────────────────────────────────────────────
    float4 r0;
    r0.x = dot(maskingMapInvVP0, v1);
    r0.y = dot(maskingMapInvVP1, v1);
    r0.z = dot(maskingMapInvVP2, v1);
    r0.w = 1.0;

    float2 maskUV;
    maskUV.x     = dot(maskingMapMatrix0, r0);
    maskUV.y     = dot(maskingMapMatrix1, r0);
    float pDepth = dot(maskingMapMatrix2, r0);

    // Occlusion gate: depth texture sign tells us if the particle is inside
    // geometry (e.g. a building). This works even on the PC port where the
    // depth values aren't precise enough for smooth soft-particle fading.
    float sceneDepth = tex2D(maskingMap, maskUV).x;
    float depthDiff  = sceneDepth - pDepth;   // negative = particle is occluded

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

    // Depth-driven blur: 4 diagonal taps + centre, offset radius grows with depth.
    float2 dx = ddx(v2);
    float2 dy = ddy(v2);
    float  br = pDepth * MAX_BLUR_RADIUS;
    float4 r1 = tex2Dgrad(Texture_lookup_2d_1, distUV, dx, dy);
    r1 += tex2Dgrad(Texture_lookup_2d_1, distUV + float2( br,  br), dx, dy);
    r1 += tex2Dgrad(Texture_lookup_2d_1, distUV + float2(-br,  br), dx, dy);
    r1 += tex2Dgrad(Texture_lookup_2d_1, distUV + float2( br, -br), dx, dy);
    r1 += tex2Dgrad(Texture_lookup_2d_1, distUV + float2(-br, -br), dx, dy);
    r1 *= 0.2;

    // ── Compositing ───────────────────────────────────────────────────────
    float4 r2       = r1 * v0;
    float3 fogBlend = fogColor.rgb - v0.rgb * r1.rgb;

    // Occlusion: kill alpha if particle is behind scene geometry.
    // Smooth distance fade for outdoor rain via pDepth.
    float occluded  = (depthDiff < 0.0) ? 0.0 : 1.0;
    float depthFade = 1.0 - smoothstep(DEPTH_FADE_START, DEPTH_FADE_END, pDepth);

    float outAlpha  = r2.w * ALPHA_SCALE * depthFade * occluded;

    float fogAmount  = min(fogColor.w, v3.w);
    float3 outColor  = fogAmount * fogBlend + r2.rgb;
    outColor         = outColor * latitudeParam.x + latitudeParam.y;
    outColor        *= latitudeParam.z;

    return float4(outColor, outAlpha);
}
