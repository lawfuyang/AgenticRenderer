// DDGI Probe Blending — Distance variant
// Distance probes use 14×14 interior texels (16×16 with border), overriding
// the config defaults (which are set for irradiance: 6×6 interior / 8×8 with border).
#include "DDGIShaderConfig.h"
#define RTXGI_DDGI_BLEND_RADIANCE 0
#define RTXGI_DDGI_BLEND_DISTANCE 1
#undef  RTXGI_DDGI_PROBE_NUM_TEXELS
#define RTXGI_DDGI_PROBE_NUM_TEXELS 16
#undef  RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS
#define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS 14
#include "ddgi/ProbeBlendingCS.hlsl"
