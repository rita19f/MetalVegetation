#pragma once
#include <Metal/Metal.hpp>
#include <string>

class Texture {
public:
    Texture(MTL::Device* device, const std::string& filepath);
    ~Texture();

    MTL::Texture* getMetalTexture() const { return m_texture; }

private:
    MTL::Texture* m_texture;
    int m_width, m_height, m_channels;
};