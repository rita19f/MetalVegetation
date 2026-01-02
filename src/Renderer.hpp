#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp> // metal-cpp CA::MetalLayer
#include "Texture.hpp"
#include "Camera.hpp"
#include "ShaderTypes.h"

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
    MTL::RenderPipelineState* m_pso; // 管线状态 (grass)
    MTL::RenderPipelineState* m_groundPSO; // 地面管线状态
    MTL::RenderPipelineState* m_ballPSO; // Ball pipeline state
    MTL::Buffer* m_vertexBuffer;     // 存放顶点数据
    MTL::Buffer* m_indexBuffer;      // 存放索引数据
    MTL::Buffer* m_instanceBuffer;   // 存放实例数据
    MTL::Buffer* m_uniformBuffer;    // 存放uniform数据
    MTL::Buffer* m_groundVertexBuffer; // 存放地面顶点数据
    MTL::Buffer* m_ballVertexBuffer; // Ball vertex data
    MTL::Buffer* m_ballIndexBuffer;  // Ball index data
    NS::UInteger m_ballIndexCount;   // Ball index count
    MTL::DepthStencilState* m_depthStencilState;
    MTL::Texture* m_depthTexture;    // 深度纹理 (resolve target)
    MTL::Texture* m_msaaColorTexture; // MSAA color render target
    MTL::Texture* m_msaaDepthTexture; // MSAA depth render target
    Texture* m_texture;               // 纹理 (grass)
    Texture* m_groundTexture;         // 地面纹理
    Camera* m_camera;                 // 相机
    
    // Trample map system
    MTL::Texture* m_trampleMapA;      // Ping-pong texture A
    MTL::Texture* m_trampleMapB;      // Ping-pong texture B
    bool m_trampleMapSwap;            // Current ping-pong state (false = A is current, true = B is current)
    MTL::ComputePipelineState* m_trampleComputePSO; // Compute pipeline for trample map update
    bool m_showTrampleMap;            // Debug toggle to visualize trample map
    bool m_prevTKeyState;             // Previous T key state for toggle detection
    
    // Mouse input tracking
    bool m_firstMouse;
    float m_lastX;
    float m_lastY;
    
    void buildShaders();
    void buildBuffers(); // 新增：用来创建顶点数据
    void buildInstanceBuffer(); // 创建实例缓冲区
    void buildTextures(); // 创建纹理
    void buildGround(); // 创建地面
    void buildTrampleMaps(); // 创建trample map纹理
    void createSphereMesh(float radius, int radialSegments, int verticalSegments,
                          std::vector<Vertex>& vertices, std::vector<uint16_t>& indices);
};