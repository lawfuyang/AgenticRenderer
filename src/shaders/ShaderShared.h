////////////////////////////////////////////////////////////////////////////////
// This file is shared between C++ and HLSL. It uses #ifdef __cplusplus
// to conditionally include C++ headers and define types. When modifying, ensure compatibility
// for both languages. Structs are defined with the same layout for CPU/GPU data sharing.
// Always test compilation in both C++ and HLSL contexts after changes.
////////////////////////////////////////////////////////////////////////////////

#ifndef SHADER_SHARED_H
#define SHADER_SHARED_H

// Minimal language-specific macro layer. The rest of this file uses the macros below so both C++ and HLSL follow the exact same code path.

#if !defined(__cplusplus)
  // HLSL path
  typedef uint uint32_t;
  typedef uint2 Vector2U;
  typedef float2 Vector2;
  typedef float3 Vector3;
  typedef float4 Vector4;
  typedef row_major float4x4 Matrix; // Ensure HLSL uses row-major layout to match DirectX::XMFLOAT4X4 on CPU
  typedef float4 Color;

  #include "srrhi/hlsl/Common.hlsli" // TODO: remove this when everything gets converted to srrhi
#else
  #include "srrhi/cpp/Common.h" // TODO: remove this when everything gets converted to srrhi
#endif // __cplusplus

// Shared per-frame data structure (one definition used by both C++ and HLSL).
struct ForwardLightingPerFrameData
{
  srrhi::PlanarViewConstants m_View;
  srrhi::PlanarViewConstants m_PrevView;
  Vector4 m_FrustumPlanes[5];
  Vector4 m_CameraPos; // xyz: camera world-space position, w: unused
  Vector4 m_CullingCameraPos; // xyz: culling camera position
  //
  uint32_t m_LightCount;
  uint32_t m_EnableRTShadows;
  uint32_t m_DebugMode;
  uint32_t m_EnableFrustumCulling;
  //
  uint32_t m_EnableConeCulling;
  uint32_t m_EnableOcclusionCulling;
  uint32_t m_HZBWidth;
  uint32_t m_HZBHeight;
  //
  float m_P00;
  float m_P11;
  Vector2 m_OpaqueColorDimensions;
  //
  Vector3 m_SunDirection;
  uint32_t m_EnableSky;
  //
  uint32_t m_RenderingMode;
  uint32_t m_RadianceMipCount;
  uint32_t m_OpaqueColorMipCount;  // Mip count for g_OpaqueColor texture (for LOD clamping)
  uint32_t pad1;
};

struct DeferredLightingConstants
{
  srrhi::PlanarViewConstants m_View;
  Vector4 m_CameraPos;
  //
  Vector3 m_SunDirection;
  uint32_t m_LightCount;
  //
  uint32_t m_EnableRTShadows;
  uint32_t m_DebugMode;
  uint32_t m_EnableSky;
  uint32_t m_RenderingMode;
  //
  uint32_t m_RadianceMipCount;
  uint32_t m_UseReSTIRDI;         // 1 = read ReSTIR DI illumination textures instead of computing direct lighting
  uint32_t m_UseReSTIRDIDenoised; // 1 = t8/t9 are denoised diffuse/specular illumination
  uint32_t pad0;
};

struct PathTracerConstants
{
    srrhi::PlanarViewConstants m_View;
    Vector4 m_CameraPos;
    uint32_t m_LightCount;
    uint32_t m_AccumulationIndex;
    uint32_t m_FrameIndex;
    uint32_t m_MaxBounces;
    // 
    Vector2 m_Jitter;
    Vector2 pad2;
    //
    Vector3 m_SunDirection;
    float m_CosSunAngularRadius; // cos(half-angle of sun disc)
};

struct CullingConstants
{
  Vector4 m_FrustumPlanes[5];
  Matrix m_View;
  Matrix m_ViewProj;
  uint32_t m_NumPrimitives;
  uint32_t m_EnableFrustumCulling;
  uint32_t m_EnableOcclusionCulling;
  uint32_t m_HZBWidth;
  uint32_t m_HZBHeight;
  uint32_t m_Phase; // 0 = Phase 1 (test all against HZB), 1 = Phase 2 (test occluded against new HZB)
  uint32_t m_UseMeshletRendering;
  float m_P00;
  float m_P11;
  int m_ForcedLOD; // -1 for auto, 0+ for forced
  uint32_t m_InstanceBaseIndex;
};

struct ResizeToNextLowestPowerOfTwoConstants
{
  uint32_t m_Width;
  uint32_t m_Height;
  uint32_t m_SamplerIdx;
};

// ============================================================================
// RTXDIConstants
// RTXDIConstants — GPU-shared constant buffer for ReSTIR DI passes.
//
// Layout mirrors the RTXDI SDK parameter structs so that C++ can fill them
// directly from the ReSTIRDIContext accessors. The #ifdef guards let the same
// source file compile as both a C++ header and an HLSL include.
// ============================================================================

struct RTXDIConstants
{
    srrhi::PlanarViewConstants m_View;
    srrhi::PlanarViewConstants m_PrevView;
    //
    Vector2U m_ViewportSize;             // (width, height) in pixels
    uint32_t m_FrameIndex;
    uint32_t m_LightCount;               // total lights in g_Lights[]
    //
    uint32_t m_NeighborOffsetMask;       // ReSTIRDIStaticParameters.NeighborOffsetCount - 1
    uint32_t m_ActiveCheckerboardField;  // 0 = off
    uint32_t m_LocalLightFirstIndex;
    uint32_t m_LocalLightCount;
    //
    uint32_t m_InfiniteLightFirstIndex;
    uint32_t m_InfiniteLightCount;
    uint32_t m_EnvLightPresent;
    uint32_t m_EnvLightIndex;
    //
    uint32_t m_ReservoirBlockRowPitch;
    uint32_t m_ReservoirArrayPitch;
    uint32_t m_NumLocalLightSamples;     // numPrimaryLocalLightSamples
    uint32_t m_NumInfiniteLightSamples;  // numPrimaryInfiniteLightSamples
    //
    uint32_t m_NumEnvSamples;            // numEnvironmentMapSamples drawn per pixel
    uint32_t m_NumBrdfSamples;           // numPrimaryBrdfSamples
    uint32_t m_LocalLightSamplingMode;   // ReSTIRDI_LocalLightSamplingMode_*
    float    m_BrdfCutoff;               // ReSTIRDI_InitialSamplingParameters.brdfCutoff
    //
    uint32_t m_InitialSamplingOutputBufferIndex;
    uint32_t m_TemporalResamplingInputBufferIndex;
    uint32_t m_TemporalResamplingOutputBufferIndex;
    uint32_t m_SpatialResamplingInputBufferIndex;
    //
    uint32_t m_SpatialResamplingOutputBufferIndex;
    uint32_t m_ShadingInputBufferIndex;
    uint32_t m_EnableSky;
    uint32_t m_SpatialNumSamples;        // number of spatial neighbour candidates
    //
    float    m_SpatialSamplingRadius;    // pixel-space search radius
    uint32_t m_SpatialNumDisocclusionBoostSamples;
    Vector2U m_LocalLightPDFTextureSize; // (width, height) of mip 0
    //
    uint32_t m_LocalRISBufferOffset;
    uint32_t m_LocalRISTileSize;
    uint32_t m_LocalRISTileCount;
    uint32_t m_EnvRISBufferOffset;
    //
    uint32_t m_EnvRISTileSize;
    uint32_t m_EnvRISTileCount;
    uint32_t m_EnvSamplingMode;          // 0=Off  1=BRDF(uniform)  2=ReSTIR-DI(importance)
    uint32_t m_EnableRTShadows;          // enables ray-traced shadows in visibility functions
    //
    Vector2U m_EnvPDFTextureSize;        // (width, height) of env PDF mip-0
    float    m_SunIntensity;
    uint32_t pad1;
    //
    Vector3  m_SunDirection;
    uint32_t m_TemporalMaxHistoryLength;
    //
    uint32_t m_TemporalBiasCorrectionMode;
    float    m_TemporalDepthThreshold;
    float    m_TemporalNormalThreshold;
    uint32_t m_TemporalEnableVisibilityShortcut;
    //
    uint32_t m_TemporalEnablePermutationSampling;
    float    m_TemporalPermutationSamplingThreshold;
    uint32_t m_TemporalUniformRandomNumber;
    uint32_t m_TemporalEnableBoilingFilter;
    //
    float    m_TemporalBoilingFilterStrength;
    uint32_t m_SpatialBiasCorrectionMode;
    float    m_SpatialDepthThreshold;
    float    m_SpatialNormalThreshold;
    //
    uint32_t m_SpatialDiscountNaiveSamples;
    uint32_t m_EnableInitialVisibility;
    uint32_t m_EnableFinalVisibility;
    uint32_t m_ReuseFinalVisibility;
    //
    uint32_t m_DiscardInvisibleSamples;
    uint32_t m_FinalVisibilityMaxAge;
    float    m_FinalVisibilityMaxDistance;
    uint32_t pad0;
};

#endif // SHADER_SHARED_H