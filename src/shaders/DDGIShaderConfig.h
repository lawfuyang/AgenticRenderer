// DDGI Shader Configuration — included by SDK shaders when RTXGI_DDGI_USE_SHADER_CONFIG_FILE=1
// Consolidates all required shader compilation defines in one place.
#pragma once

// Coordinate system: Right Hand, Y-Up
#define RTXGI_COORDINATE_SYSTEM 2

// Use shader reflection — no explicit register/space declarations needed
#define RTXGI_DDGI_SHADER_REFLECTION 1

// Application manages all DDGI resources (unmanaged mode)
#define RTXGI_DDGI_RESOURCE_MANAGEMENT 0

// Bindless resources via SM 6.6+ descriptor heap
#define RTXGI_DDGI_BINDLESS_RESOURCES 1
#define RTXGI_BINDLESS_TYPE RTXGI_BINDLESS_TYPE_DESCRIPTOR_HEAP

// Probe texel configuration (defaults; override per-volume as needed)
#define RTXGI_DDGI_PROBE_NUM_TEXELS 8
#define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS 6

// Blending configuration
#define RTXGI_DDGI_BLEND_SHARED_MEMORY 0
#define RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY 0

// Wave lane count (D3D12)
#define RTXGI_DDGI_WAVE_LANE_COUNT 32

// Debug visualization (disabled)
#define RTXGI_DDGI_DEBUG_PROBE_INDEXING 0
#define RTXGI_DDGI_DEBUG_OCTAHEDRAL_INDEXING 0
#define RTXGI_DDGI_DEBUG_BORDER_COPY_INDEXING 0

// HLSL context marker (prevents C++ includes in shared headers)
#define HLSL 1
