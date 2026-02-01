#pragma once

#include "pch.h"

#include <nvrhi/nvrhi.h>
#include <memory>

class ITextureDataReader
{
public:
    virtual ~ITextureDataReader() = default;
    virtual const void* GetData() const = 0;
    virtual size_t GetSize() const = 0;
};

bool LoadTexture(std::string_view filePath, nvrhi::TextureDesc& desc, std::unique_ptr<ITextureDataReader>& data);
void LoadDDSTexture(std::string_view filePath, nvrhi::TextureDesc& desc, std::unique_ptr<ITextureDataReader>& data);
void LoadSTBITexture(std::string_view filePath, nvrhi::TextureDesc& desc, std::unique_ptr<ITextureDataReader>& data);