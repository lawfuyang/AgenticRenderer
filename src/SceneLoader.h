#pragma once

#include "Scene.h"
#include "Renderer.h"

#include "cgltf.h"

class SceneLoader
{
public:
    // Main GLTF loading function
    static bool LoadGLTFScene(Scene& scene, const std::string& scenePath, std::vector<Vertex>& allVertices, std::vector<uint32_t>& allIndices);

    // Helper functions for processing GLTF data
    static void ProcessMaterialsAndImages(const cgltf_data* data, Scene& scene);
    static void ProcessCameras(const cgltf_data* data, Scene& scene);
    static void ProcessLights(const cgltf_data* data, Scene& scene);
    static void ProcessAnimations(const cgltf_data* data, Scene& scene);
    static void ProcessMeshes(const cgltf_data* data, Scene& scene, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);
    static void ProcessNodesAndHierarchy(const cgltf_data* data, Scene& scene);

    // Texture and GPU buffer functions
    static void LoadTexturesFromImages(Scene& scene, const std::filesystem::path& sceneDir, Renderer* renderer);
    static void UpdateMaterialsAndCreateConstants(Scene& scene, Renderer* renderer);
    static void SetupDirectionalLightAndCamera(Scene& scene, Renderer* renderer);
    static void CreateAndUploadGpuBuffers(Scene& scene, Renderer* renderer, const std::vector<Vertex>& allVertices, const std::vector<uint32_t>& allIndices);

private:
    // Utility functions
    static void SetTextureAndSampler(const cgltf_texture* tex, int& textureIndex, std::vector<bool>& samplerForImageIsWrap, const cgltf_data* data);
    static void ComputeWorldTransforms(Scene& scene, int nodeIndex, const Matrix& parent);
    static cgltf_result decompressMeshopt(cgltf_data* data);
    static const char* cgltf_result_tostring(cgltf_result result);
};
