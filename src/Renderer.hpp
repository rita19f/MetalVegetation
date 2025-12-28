#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp> // metal-cpp CA::MetalLayer
#include "Texture.hpp"
#include "Camera.hpp"

struct GLFWwindow;

class Renderer {
public:
    Renderer(MTL::Device* device, CA::MetalLayer* layer);
    ~Renderer();

    void draw();
    void resize(int width, int height);
    void update(GLFWwindow* window, float deltaTime);

private:
    MTL::Device* m_device;
    MTL::CommandQueue* m_commandQueue;
    CA::MetalLayer* m_metalLayer; 
    MTL::RenderPipelineState* m_pso; // 管线状态
    MTL::Buffer* m_vertexBuffer;     // 存放顶点数据
    MTL::Buffer* m_indexBuffer;      // 存放索引数据
    MTL::Buffer* m_instanceBuffer;   // 存放实例数据
    MTL::Buffer* m_uniformBuffer;    // 存放uniform数据
    MTL::DepthStencilState* m_depthStencilState;
    MTL::Texture* m_depthTexture;    // 深度纹理
    Texture* m_texture;               // 纹理
    Camera* m_camera;                 // 相机
    
    // Mouse input tracking
    bool m_firstMouse;
    float m_lastX;
    float m_lastY;
    
    void buildShaders();
    void buildBuffers(); // 新增：用来创建顶点数据
    void buildInstanceBuffer(); // 创建实例缓冲区
    void buildTextures(); // 创建纹理
};