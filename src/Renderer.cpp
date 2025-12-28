#include "Renderer.hpp"
#include "ShaderTypes.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <random>
#include <cmath>

// Helper function to convert glm::mat4 to simd::float4x4
static simd::float4x4 glmToSimd(const glm::mat4& glmMat) {
    simd::float4x4 simdMat;
    const float* glmData = glm::value_ptr(glmMat);
    
    // GLM matrices are column-major, simd::float4x4 is also column-major
    // Copy column by column
    simdMat.columns[0] = simd::float4{glmData[0], glmData[1], glmData[2], glmData[3]};
    simdMat.columns[1] = simd::float4{glmData[4], glmData[5], glmData[6], glmData[7]};
    simdMat.columns[2] = simd::float4{glmData[8], glmData[9], glmData[10], glmData[11]};
    simdMat.columns[3] = simd::float4{glmData[12], glmData[13], glmData[14], glmData[15]};
    
    return simdMat;
}

Renderer::Renderer(MTL::Device* device, CA::MetalLayer* layer)
    : m_device(device)
    , m_metalLayer(layer)
    , m_commandQueue(nullptr)
    , m_pso(nullptr)
    , m_vertexBuffer(nullptr)
    , m_indexBuffer(nullptr)
    , m_instanceBuffer(nullptr)
    , m_uniformBuffer(nullptr)
    , m_depthStencilState(nullptr)
    , m_depthTexture(nullptr)
    , m_texture(nullptr)
    , m_camera(nullptr)
    , m_firstMouse(true)
    , m_lastX(400.0f)
    , m_lastY(300.0f)
{
    // Create a CommandQueue
    m_commandQueue = m_device->newCommandQueue();
    
    // Initialize m_camera at (0, 1, 3)
    m_camera = new Camera(glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
    
    // Build shaders and buffers
    buildShaders();
    buildBuffers();
    buildInstanceBuffer();
    buildTextures();
}

Renderer::~Renderer()
{
    // Release resources if needed
    if (m_commandQueue) {
        m_commandQueue->release();
    }
    if (m_pso) {
        m_pso->release();
    }
    if (m_vertexBuffer) {
        m_vertexBuffer->release();
    }
    if (m_indexBuffer) {
        m_indexBuffer->release();
    }
    if (m_instanceBuffer) {
        m_instanceBuffer->release();
    }
    if (m_depthStencilState) {
        m_depthStencilState->release();
    }
    if (m_depthTexture) {
        m_depthTexture->release();
    }
    if (m_texture) {
        delete m_texture;
    }
    if (m_camera) {
        delete m_camera;
    }
    if (m_uniformBuffer) {
        m_uniformBuffer->release();
    }
}

void Renderer::draw()
{
    // Get Drawable: Call m_metalLayer->nextDrawable() to get the current drawable. If it's null, return early.
    CA::MetalDrawable* drawable = m_metalLayer->nextDrawable();
    if (!drawable) {
        return;
    }
    
    // Create a CommandBuffer
    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
    
    // Create Descriptor: Create a new MTL::RenderPassDescriptor using MTL::RenderPassDescriptor::alloc()->init()
    MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    
    // Setup Color Attachment:
    // Access renderPassDescriptor->colorAttachments()->object(0)
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    // Set its texture to drawable->texture()
    colorAttachment->setTexture(drawable->texture());
    // Set loadAction to MTL::LoadActionClear
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    // Set clearColor to sky blue (0.5, 0.7, 1.0, 1.0)
    renderPassDescriptor->colorAttachments()->object(0)->setClearColor(MTL::ClearColor(0.5, 0.7, 1.0, 1.0));
    // Set storeAction to MTL::StoreActionStore
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    
    // Setup Depth Attachment (Crucial):
    // Check if m_depthTexture is not null
    if (m_depthTexture) {
        // Access renderPassDescriptor->depthAttachment()
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = renderPassDescriptor->depthAttachment();
        // Set its texture to m_depthTexture
        depthAttachment->setTexture(m_depthTexture);
        // Set loadAction to MTL::LoadActionClear
        depthAttachment->setLoadAction(MTL::LoadActionClear);
        // Set storeAction to MTL::StoreActionDontCare
        depthAttachment->setStoreAction(MTL::StoreActionDontCare);
        // Set clearDepth to 1.0
        depthAttachment->setClearDepth(1.0);
    }
    
    // Update Uniforms struct in m_uniformBuffer
    if (m_uniformBuffer && m_camera) {
        // Get drawable size for projection matrix
        MTL::Texture* drawableTexture = drawable->texture();
        float width = static_cast<float>(drawableTexture->width());
        float height = static_cast<float>(drawableTexture->height());
        
        // Get view and projection matrices from camera
        glm::mat4 viewMatrix = m_camera->getViewMatrix();
        glm::mat4 projectionMatrix = m_camera->getProjectionMatrix(width, height);
        
        // Convert glm::mat4 to simd::float4x4 manually
        Uniforms uniforms;
        uniforms.viewMatrix = glmToSimd(viewMatrix);
        uniforms.projectionMatrix = glmToSimd(projectionMatrix);
        // Update uniforms.time before copying it to the buffer
        uniforms.time = static_cast<float>(glfwGetTime());
        
        // Set uniforms.lightDirection. Use simd::normalize(simd::make_float3(1.0f, 1.0f, -1.0f)) (Simulating a sun from the side)
        uniforms.lightDirection = simd::normalize(simd::make_float3(1.0f, 1.0f, -1.0f));
        
        // Set uniforms.lightColor. Use simd::make_float3(1.0f, 1.0f, 0.9f) (Warm sunlight)
        uniforms.lightColor = simd::make_float3(1.0f, 1.0f, 0.9f);
        
        // Copy uniforms to buffer
        void* uniformContents = m_uniformBuffer->contents();
        memcpy(uniformContents, &uniforms, sizeof(Uniforms));
    }
    
    // Encode & Commit: Use this manually created descriptor to create the encoder, draw primitives, present the drawable, and commit
    // Create a RenderCommandEncoder
    MTL::RenderCommandEncoder* renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);
    
    // Set render pipeline state
    renderEncoder->setRenderPipelineState(m_pso);
    
    // Set depth stencil state
    renderEncoder->setDepthStencilState(m_depthStencilState);
    
    // Set vertex buffer
    renderEncoder->setVertexBuffer(m_vertexBuffer, 0, BufferIndexMeshPositions);
    
    // Set instance buffer
    renderEncoder->setVertexBuffer(m_instanceBuffer, 0, BufferIndexInstanceData);
    
    // Bind the uniform buffer: encoder->setVertexBuffer(m_uniformBuffer, 0, BufferIndexUniforms)
    renderEncoder->setVertexBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
    
    // Set fragment texture (CRITICAL FIX)
    renderEncoder->setFragmentTexture(m_texture->getMetalTexture(), 0);
    
    // Set index buffer and draw indexed primitives with instancing
    renderEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(6), MTL::IndexTypeUInt16, m_indexBuffer, NS::UInteger(0), NS::UInteger(10000));
    
    // End encoding
    renderEncoder->endEncoding();
    
    // Present the drawable
    commandBuffer->presentDrawable(drawable);
    
    // Commit the command buffer
    commandBuffer->commit();
    
    // Memory Management: Release the descriptor (created with alloc()->init(), so we need to release it)
    renderPassDescriptor->release();
    drawable->release();
}

void Renderer::buildShaders()
{
    // Load the library from Shaders.metal (using device->newDefaultLibrary())
    NS::Error* error = nullptr;
    MTL::Library* library = m_device->newDefaultLibrary();
    
    if (!library) {
        std::cerr << "Failed to load default Metal library" << std::endl;
        return;
    }
    
    // Get "vertexMain" and "fragmentMain" functions
    NS::String* vertexFunctionName = NS::String::string("vertexMain", NS::ASCIIStringEncoding);
    NS::String* fragmentFunctionName = NS::String::string("fragmentMain", NS::ASCIIStringEncoding);
    
    MTL::Function* vertexFunction = library->newFunction(vertexFunctionName);
    MTL::Function* fragmentFunction = library->newFunction(fragmentFunctionName);
    
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Failed to load shader functions" << std::endl;
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        library->release();
        return;
    }
    
    // Create a MTL::RenderPipelineDescriptor
    MTL::RenderPipelineDescriptor* pipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    
    // Set vertex/fragment functions
    pipelineDescriptor->setVertexFunction(vertexFunction);
    pipelineDescriptor->setFragmentFunction(fragmentFunction);
    
    // Set colorAttachments[0].pixelFormat to MTLPixelFormatBGRA8Unorm
    MTL::RenderPipelineColorAttachmentDescriptor* colorAttachment = pipelineDescriptor->colorAttachments()->object(0);
    colorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    
    // Set depth pixel format
    pipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    // Create m_pso using device->newRenderPipelineState. Handle errors if any.
    m_pso = m_device->newRenderPipelineState(pipelineDescriptor, &error);
    
    if (!m_pso) {
        if (error) {
            NS::String* errorString = error->localizedDescription();
            std::cerr << "Failed to create render pipeline state: " << errorString->utf8String() << std::endl;
            errorString->release();
        } else {
            std::cerr << "Failed to create render pipeline state" << std::endl;
        }
    }
    
    // Release resources
    vertexFunction->release();
    fragmentFunction->release();
    library->release();
    pipelineDescriptor->release();
    vertexFunctionName->release();
    fragmentFunctionName->release();
    
    // Create a MTL::DepthStencilDescriptor
    MTL::DepthStencilDescriptor* depthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    
    // Set depthCompareFunction to MTL::CompareFunctionLess
    depthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunctionLess);
    
    // Set depthWriteEnabled to true
    depthStencilDescriptor->setDepthWriteEnabled(true);
    
    // Create m_depthStencilState using device->newDepthStencilState
    m_depthStencilState = m_device->newDepthStencilState(depthStencilDescriptor);
    
    if (!m_depthStencilState) {
        std::cerr << "Failed to create depth stencil state" << std::endl;
    }
    
    // Release descriptor
    depthStencilDescriptor->release();
}

void Renderer::buildBuffers()
{
    // Define 4 vertices for a quad (blade of grass)
    // Bottom-Left: {-0.1, -0.5, 0}, UV: {0, 1}
    // Bottom-Right: { 0.1, -0.5, 0}, UV: {1, 1}
    // Top-Left: {-0.1, 0.5, 0}, UV: {0, 0}
    // Top-Right: { 0.1, 0.5, 0}, UV: {1, 0}
    // Normal: Point normals straight UP (0, 1, 0) for all vertices
    // This makes grass look soft and evenly lit by the sun, avoiding black appearance when rotated away
    Vertex vertices[4] = {
        { {-0.1f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },  // Bottom-Left
        { { 0.1f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },  // Bottom-Right
        { {-0.1f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },  // Top-Left
        { { 0.1f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} }   // Top-Right
    };
    
    // Create m_vertexBuffer using device->newBuffer with the data size and MTL::ResourceStorageModeShared
    size_t vertexDataSize = sizeof(vertices);
    m_vertexBuffer = m_device->newBuffer(vertexDataSize, MTL::ResourceStorageModeShared);
    
    if (m_vertexBuffer) {
        // Copy vertex data to buffer
        void* bufferContents = m_vertexBuffer->contents();
        memcpy(bufferContents, vertices, vertexDataSize);
    } else {
        std::cerr << "Failed to create vertex buffer" << std::endl;
        return;
    }
    
    // Create index buffer for the quad (indices: 0, 1, 2, 1, 3, 2)
    uint16_t indices[6] = {
        0, 1, 2,  // First triangle: Bottom-Left, Bottom-Right, Top-Left
        1, 3, 2   // Second triangle: Bottom-Right, Top-Right, Top-Left
    };
    
    size_t indexDataSize = sizeof(indices);
    m_indexBuffer = m_device->newBuffer(indexDataSize, MTL::ResourceStorageModeShared);
    
    if (m_indexBuffer) {
        // Copy index data to buffer
        void* indexBufferContents = m_indexBuffer->contents();
        memcpy(indexBufferContents, indices, indexDataSize);
    } else {
        std::cerr << "Failed to create index buffer" << std::endl;
    }
    
    // Create uniform buffer
    size_t uniformDataSize = sizeof(Uniforms);
    m_uniformBuffer = m_device->newBuffer(uniformDataSize, MTL::ResourceStorageModeShared);
    
    if (!m_uniformBuffer) {
        std::cerr << "Failed to create uniform buffer" << std::endl;
    }
}

void Renderer::buildInstanceBuffer()
{
    const int instanceCount = 10000;
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);
    
    // Generate 10,000 InstanceData structs
    InstanceData instances[instanceCount];
    
    for (int i = 0; i < instanceCount; ++i) {
        // Position: Random x between -10 and 10, Random z between -10 and 10. y is 0.
        float x = posDist(gen);
        float z = posDist(gen);
        float y = 0.0f;
        
        // Rotation: Random rotation around Y-axis (0 to 360 degrees)
        float rotationY = glm::radians(rotDist(gen));
        
        // Scale: Random scale between 0.8 and 1.2 for variety
        float scale = scaleDist(gen);
        
        // Construct a float4x4 model matrix from these transform values (Translate * Rotate * Scale)
        // Using GLM for matrix math
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
        glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, scale));
        
        // Combine: Translate * Rotate * Scale
        glm::mat4 modelMatrix = translation * rotation * scaling;
        
        // Convert GLM matrix to simd::float4x4
        simd::float4x4 simdMatrix;
        const float* glmData = glm::value_ptr(modelMatrix);
        simdMatrix.columns[0] = simd::float4{glmData[0], glmData[1], glmData[2], glmData[3]};
        simdMatrix.columns[1] = simd::float4{glmData[4], glmData[5], glmData[6], glmData[7]};
        simdMatrix.columns[2] = simd::float4{glmData[8], glmData[9], glmData[10], glmData[11]};
        simdMatrix.columns[3] = simd::float4{glmData[12], glmData[13], glmData[14], glmData[15]};
        
        instances[i].modelMatrix = simdMatrix;
    }
    
    // Create m_instanceBuffer (MTL::Buffer) and copy data to it
    size_t instanceDataSize = sizeof(instances);
    m_instanceBuffer = m_device->newBuffer(instanceDataSize, MTL::ResourceStorageModeShared);
    
    if (m_instanceBuffer) {
        // Copy instance data to buffer
        void* instanceBufferContents = m_instanceBuffer->contents();
        memcpy(instanceBufferContents, instances, instanceDataSize);
    } else {
        std::cerr << "Failed to create instance buffer" << std::endl;
    }
}

void Renderer::resize(int width, int height)
{
    // Release old depth texture
    if (m_depthTexture) {
        m_depthTexture->release();
        m_depthTexture = nullptr;
    }
    
    // Create a new MTL::TextureDescriptor with MTLPixelFormatDepth32Float
    MTL::TextureDescriptor* depthDescriptor = MTL::TextureDescriptor::alloc()->init();
    depthDescriptor->setWidth(width);
    depthDescriptor->setHeight(height);
    depthDescriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    depthDescriptor->setTextureType(MTL::TextureType2D);
    
    // Usage should be RenderTarget
    depthDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    
    // StorageMode should be Private (GPU only)
    depthDescriptor->setStorageMode(MTL::StorageModePrivate);
    
    // Create m_depthTexture
    m_depthTexture = m_device->newTexture(depthDescriptor);
    
    if (!m_depthTexture) {
        std::cerr << "Failed to create depth texture" << std::endl;
    }
    
    // Release descriptor
    depthDescriptor->release();
}

void Renderer::buildTextures()
{
    // Create m_texture instance
    // Note: Make sure to check path. Since we copy assets to bin, relative path "assets/grass_albedo.png" should work.
    m_texture = new Texture(m_device, "assets/grass_albedo.png");
}

void Renderer::update(GLFWwindow* window, float deltaTime)
{
    if (!window || !m_camera) {
        return;
    }
    
    // Handle keyboard input (WASD)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        m_camera->processKeyboard('W', deltaTime);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        m_camera->processKeyboard('S', deltaTime);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        m_camera->processKeyboard('A', deltaTime);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        m_camera->processKeyboard('D', deltaTime);
    }
    
    // Handle mouse movement
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    if (m_firstMouse) {
        m_lastX = static_cast<float>(xpos);
        m_lastY = static_cast<float>(ypos);
        m_firstMouse = false;
    }
    
    float xoffset = static_cast<float>(xpos) - m_lastX;
    float yoffset = m_lastY - static_cast<float>(ypos); // Reversed since y-coordinates go from bottom to top
    
    m_lastX = static_cast<float>(xpos);
    m_lastY = static_cast<float>(ypos);
    
    m_camera->processMouseMovement(xoffset, yoffset);
}

