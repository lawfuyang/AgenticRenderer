#pragma once

#include "pch.h"
#include "Camera.h"

// Minimal scene representation for glTF meshes/nodes/materials/textures
class Scene
{
public:
    struct Primitive
    {
        uint32_t m_VertexOffset = 0;
        uint32_t m_VertexCount = 0;
        uint32_t m_IndexOffset = 0;
        uint32_t m_IndexCount = 0;
        int m_MaterialIndex = -1;
    };

    struct Mesh
    {
        std::vector<Primitive> m_Primitives;
        // local AABB
        Vector3 m_AabbMin{};
        Vector3 m_AabbMax{};
    };

    struct Node
    {
        std::string m_Name;
        int m_MeshIndex = -1;
        int m_Parent = -1;
        std::vector<int> m_Children;
        // local transform
        Matrix m_LocalTransform{};
        // world transform (computed)
        Matrix m_WorldTransform{};
        // world AABB
        Vector3 m_AabbMin{};
        Vector3 m_AabbMax{};
        // Camera/Light indices
        int m_CameraIndex = -1;
        int m_LightIndex = -1;
    };

    struct Material
    {
        std::string m_Name;
        Vector4 m_BaseColorFactor = Vector4{1.0f, 1.0f, 1.0f, 1.0f};
        int m_BaseColorTexture = -1; // index into m_Textures
        float m_RoughnessFactor = 1.0f;
        float m_MetallicFactor = 0.0f;
    };

    struct Texture
    {
        std::string m_Uri;
    };

    struct Camera
    {
        std::string m_Name;
        ProjectionParams m_Projection;
        // Associated node index for transform
        int m_NodeIndex = -1;
    };

    struct Light
    {
        std::string m_Name;
        enum Type { Directional, Point, Spot };
        Type m_Type;
        Vector3 m_Color = Vector3{1.0f, 1.0f, 1.0f};
        float m_Intensity = 1.0f;
        float m_Range = 0.0f; // 0 = infinite
        // Spot
        float m_SpotInnerConeAngle = 0.0f;
        float m_SpotOuterConeAngle = DirectX::XM_PIDIV4; // 45deg
        // Associated node index for transform
        int m_NodeIndex = -1;
    };

    // Public scene storage (instance members)
    std::vector<Mesh> m_Meshes;
    std::vector<Node> m_Nodes;
    std::vector<Material> m_Materials;
    std::vector<Texture> m_Textures;
    std::vector<Camera> m_Cameras;
    std::vector<Light> m_Lights;

    // GPU buffers created for the scene
    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;
    nvrhi::BufferHandle m_MaterialConstantsBuffer;

    // Load the scene from the path configured in `Config::Get().m_GltfScene`.
    // Only mesh vertex/index data and node hierarchy are loaded for now.
    bool LoadScene();
    // Release GPU resources and clear scene data
    void Shutdown();
};
