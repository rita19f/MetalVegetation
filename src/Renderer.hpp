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
    MTL::RenderPipelineState* m_pso; // Grass pipeline state
    MTL::RenderPipelineState* m_groundPSO; // Ground pipeline state
    MTL::RenderPipelineState* m_ballPSO; // Ball pipeline state
    MTL::RenderPipelineState* m_skyPSO; // Sky pipeline state
    MTL::Buffer* m_vertexBuffer;     // Vertex data buffer
    MTL::Buffer* m_indexBuffer;      // Index data buffer
    MTL::Buffer* m_instanceBuffer;   // Instance data buffer
    MTL::Buffer* m_uniformBuffer;    // Uniform data buffer
    MTL::Buffer* m_groundVertexBuffer; // Ground vertex data buffer
    MTL::Buffer* m_ballVertexBuffer; // Ball vertex data buffer
    MTL::Buffer* m_ballIndexBuffer;  // Ball index data buffer
    NS::UInteger m_ballIndexCount;   // Ball index count
    MTL::DepthStencilState* m_depthStencilState;
    MTL::DepthStencilState* m_skyDepthStencilState; // Sky depth state (always pass, no write)
    MTL::Texture* m_depthTexture;    // Depth texture (resolve target)
    MTL::Texture* m_msaaColorTexture; // MSAA color render target
    MTL::Texture* m_msaaDepthTexture; // MSAA depth render target
    Texture* m_texture;               // Grass texture
    Texture* m_groundTexture;         // Ground texture
    Camera* m_camera;                 // Camera
    
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
    void buildBuffers(); // Create vertex data
    void buildInstanceBuffer(); // Create instance buffer
    void buildTextures(); // Create textures
    void buildGround(); // Create ground mesh
    void buildTrampleMaps(); // Create trample map textures
    void createSphereMesh(float radius, int radialSegments, int verticalSegments,
                          std::vector<Vertex>& vertices, std::vector<uint16_t>& indices);
};