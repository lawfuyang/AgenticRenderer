// Generic shared definitions for both C++ and HLSL shaders.
// Other shader files can include this header and optionally request
// the declaration of a `PushConstants` struct by defining
// `IMGUI_DEFINE_PUSH_CONSTANTS` before including this file.

#pragma once

// Minimal language-specific macro layer. The rest of this file uses
// the macros below so both C++ and HLSL follow the exact same code path.

// Include Math aliases on C++ side so we can map shared types to them.
#ifdef __cplusplus
#include "Math.h"
#endif

// Map shared scalar/vector/matrix declarations to language-specific forms.
#ifdef __cplusplus
  // In C++, prefer simple POD arrays for scalars to match HLSL layout.
#else
  // HLSL path
  typedef float2 Vector2;
  typedef float3 Vector3;
  typedef float4 Vector4;
  typedef float4x4 Matrix;
  typedef float4 Color;
#endif

// Define PushConstants once using the shared macros above. If the
// including file defines `IMGUI_DEFINE_PUSH_CONSTANTS`, we'll provide
// the struct here in the shared code path.
#ifdef IMGUI_DEFINE_PUSH_CONSTANTS
struct PushConstants
{
	Vector2 uScale;
	Vector2 uTranslate;
};
#endif
