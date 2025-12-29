#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Texture.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

Texture::Texture(MTL::Device* device, MTL::CommandQueue* commandQueue, const std::string& filepath)
    : m_texture(nullptr)
    , m_width(0)
    , m_height(0)
    , m_channels(0)
{
    // Load image using stb_image
    int width, height, channels;
    unsigned char* imageData = stbi_load(filepath.c_str(), &width, &height, &channels, 4); // Force 4 channels (RGBA)
    
    if (!imageData) {
        std::string errorMsg = "Failed to load image: " + filepath;
        if (stbi_failure_reason()) {
            errorMsg += " - " + std::string(stbi_failure_reason());
        }
        std::cerr << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }
    
    m_width = width;
    m_height = height;
    m_channels = 4; // We forced 4 channels
    
    // Create a MTL::TextureDescriptor
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    
    // 1. Disable SRGB: Use MTLPixelFormatRGBA8Unorm (not _sRGB) for linear values
    // This is important for alpha masks - we need linear values, not color-corrected ones
    descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    
    // Width/Height from loaded image
    descriptor->setWidth(m_width);
    descriptor->setHeight(m_height);
    
    // 2. Enable Mipmaps: Calculate mipmap level count
    // Formula: floor(log2(max(width, height))) + 1
    int maxDimension = std::max(m_width, m_height);
    int mipmapLevelCount = static_cast<int>(std::floor(std::log2(static_cast<double>(maxDimension)))) + 1;
    descriptor->setMipmapLevelCount(mipmapLevelCount);
    
    // Set texture type and usage
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    descriptor->setStorageMode(MTL::StorageModeShared);
    
    // Create the texture using device->newTexture(descriptor)
    m_texture = device->newTexture(descriptor);
    
    if (!m_texture) {
        stbi_image_free(imageData);
        descriptor->release();
        throw std::runtime_error("Failed to create Metal texture");
    }
    
    // Copy data: Use texture->replaceRegion to copy pixel data from stbi_load result to the texture
    // CRITICAL: Always use 4 channels (RGBA) regardless of original file format
    // stbi_load with 4 as last argument forces conversion to RGBA, so we must use 4 in calculations
    MTL::Region region = MTL::Region::Make2D(0, 0, m_width, m_height);
    NS::UInteger bytesPerRow = m_width * 4; // 4 bytes per pixel (RGBA) - DO NOT use 'channels' variable
    
    m_texture->replaceRegion(region, 0, imageData, bytesPerRow);
    
    // Free the image memory with stbi_image_free
    stbi_image_free(imageData);
    
    // 2. Generate Mipmaps using BlitCommandEncoder
    if (commandQueue && mipmapLevelCount > 1) {
        MTL::CommandBuffer* commandBuffer = commandQueue->commandBuffer();
        if (commandBuffer) {
            MTL::BlitCommandEncoder* blitEncoder = commandBuffer->blitCommandEncoder();
            if (blitEncoder) {
                blitEncoder->generateMipmaps(m_texture);
                blitEncoder->endEncoding();
                commandBuffer->commit();
                commandBuffer->waitUntilCompleted();
            }
            commandBuffer->release();
        }
    }
    
    // Release descriptor
    descriptor->release();
}

Texture::~Texture()
{
    if (m_texture) {
        m_texture->release();
    }
}

