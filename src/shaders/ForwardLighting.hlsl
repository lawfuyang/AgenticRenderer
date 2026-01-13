// Basic Forward Lighting shaders (VS + PS)

// Define forward-lighting specific macro so shared header exposes relevant types
#define FORWARD_LIGHTING_DEFINE
// Include shared types (VertexInput, PerObjectData)
#include "ShaderShared.hlsl"

// Define the cbuffer here using the shared PerObjectData struct
cbuffer PerObjectCB : register(b0)
{
    PerObjectData perObject;
};

// Hardcoded directional light (in world space)
static const float3 kLightDirConst = float3(0.6f, -0.7f, -0.4f);

struct VSOut
{
    float4 Position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(VertexInput input)
{
    VSOut o;
    float4 worldPos = mul(float4(input.pos, 1.0f), perObject.World);
    o.Position = mul(worldPos, perObject.ViewProj);
    o.normal = normalize(mul((float3x3)perObject.World, input.normal));
    o.uv = input.uv;
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float3 lightDir = normalize(kLightDirConst);
    float NdotL = saturate(dot(input.normal, lightDir));
    float3 baseColor = perObject.BaseColor.xyz;
    float alpha = perObject.BaseColor.w;
    float3 color = baseColor * (0.1f + 0.9f * NdotL);
    return float4(color, alpha);
}
