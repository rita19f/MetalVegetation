#include "Renderer.hpp"
#include "ShaderTypes.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <CoreGraphics/CoreGraphics.h>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <random>
#include <cmath>
#include <vector>

// Grass mesh configuration: vertical segments for smooth bending
static constexpr int kGrassSegments = 7;                 // At least 7 height segments
static constexpr int kGrassRows = kGrassSegments + 1;    // Rows of vertices
static constexpr int kGrassVertsPerRow = 2;              // Left + right per row
static constexpr int kGrassVertexCount = kGrassRows * kGrassVertsPerRow;
static constexpr int kGrassIndicesPerSegment = 6;        // 2 triangles per segment
static constexpr int kGrassIndexCount = kGrassSegments * kGrassIndicesPerSegment;

// Scene size: shared constant for ground plane and grass field
// Ground and grass will both span from -SCENE_SIZE to +SCENE_SIZE (total size = 2 * SCENE_SIZE)
static constexpr float SCENE_SIZE = 15.0f;              // Half-size, so total scene is 30x30 (compact, high-density)

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
    , m_groundPSO(nullptr)
    , m_ballPSO(nullptr)
    , m_skyPSO(nullptr)
    , m_vertexBuffer(nullptr)
    , m_indexBuffer(nullptr)
    , m_instanceBuffer(nullptr)
    , m_uniformBuffer(nullptr)
    , m_groundVertexBuffer(nullptr)
    , m_ballVertexBuffer(nullptr)
    , m_ballIndexBuffer(nullptr)
    , m_ballIndexCount(0)
    , m_depthStencilState(nullptr)
    , m_skyDepthStencilState(nullptr)
    , m_depthTexture(nullptr)
    , m_msaaColorTexture(nullptr)
    , m_msaaDepthTexture(nullptr)
    , m_texture(nullptr)
    , m_groundTexture(nullptr)
    , m_camera(nullptr)
    , m_firstMouse(true)
    , m_lastX(400.0f)
    , m_lastY(300.0f)
    , m_trampleMapA(nullptr)
    , m_trampleMapB(nullptr)
    , m_trampleMapSwap(false)
    , m_trampleComputePSO(nullptr)
    , m_showTrampleMap(false)
    , m_prevTKeyState(false)
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
    buildGround();
    buildTrampleMaps();
    
    // Initialize MSAA textures with initial layer size
    // Get drawable size from metal layer
    CGSize drawableSize = m_metalLayer->drawableSize();
    resize(static_cast<int>(drawableSize.width), static_cast<int>(drawableSize.height));
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
    if (m_groundPSO) {
        m_groundPSO->release();
    }
    if (m_groundVertexBuffer) {
        m_groundVertexBuffer->release();
    }
    if (m_groundTexture) {
        delete m_groundTexture;
    }
    if (m_ballPSO) {
        m_ballPSO->release();
    }
    if (m_skyPSO) {
        m_skyPSO->release();
    }
    if (m_skyDepthStencilState) {
        m_skyDepthStencilState->release();
    }
    if (m_trampleMapA) {
        m_trampleMapA->release();
    }
    if (m_trampleMapB) {
        m_trampleMapB->release();
    }
    if (m_trampleComputePSO) {
        m_trampleComputePSO->release();
    }
    if (m_ballVertexBuffer) {
        m_ballVertexBuffer->release();
    }
    if (m_ballIndexBuffer) {
        m_ballIndexBuffer->release();
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
    
    // Setup Color Attachment with MSAA:
    // Access renderPassDescriptor->colorAttachments()->object(0)
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    
    // Render to MSAA texture (4x multisample)
    if (m_msaaColorTexture) {
        colorAttachment->setTexture(m_msaaColorTexture);
        // Resolve to drawable texture
        colorAttachment->setResolveTexture(drawable->texture());
    } else {
        // Fallback: render directly to drawable if MSAA texture not available
        colorAttachment->setTexture(drawable->texture());
    }
    
    // Set loadAction to MTL::LoadActionClear
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    // Set clearColor to match fog color (0.4, 0.6, 0.9, 1.0) for seamless blending
    renderPassDescriptor->colorAttachments()->object(0)->setClearColor(MTL::ClearColor(0.4, 0.6, 0.9, 1.0));
    
    // Set storeAction: Store and resolve if MSAA, otherwise just store
    if (m_msaaColorTexture) {
        colorAttachment->setStoreAction(MTL::StoreActionMultisampleResolve);
    } else {
        colorAttachment->setStoreAction(MTL::StoreActionStore);
    }
    
    // Setup Depth Attachment with MSAA:
    // Check if MSAA depth texture is available
    if (m_msaaDepthTexture && m_depthTexture) {
        // Access renderPassDescriptor->depthAttachment()
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = renderPassDescriptor->depthAttachment();
        // Render to MSAA depth texture
        depthAttachment->setTexture(m_msaaDepthTexture);
        // Resolve to non-MSAA depth texture
        depthAttachment->setResolveTexture(m_depthTexture);
        // Set loadAction to MTL::LoadActionClear
        depthAttachment->setLoadAction(MTL::LoadActionClear);
        // Set storeAction to resolve MSAA depth
        depthAttachment->setStoreAction(MTL::StoreActionMultisampleResolve);
        // Set clearDepth to 1.0
        depthAttachment->setClearDepth(1.0);
    } else if (m_depthTexture) {
        // Fallback: use non-MSAA depth texture
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = renderPassDescriptor->depthAttachment();
        depthAttachment->setTexture(m_depthTexture);
        depthAttachment->setLoadAction(MTL::LoadActionClear);
        depthAttachment->setStoreAction(MTL::StoreActionDontCare);
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
        
        // Set uniforms.sunDirection for stylized foliage lighting (normalize(1.0, 1.0, 0.5))
        uniforms.sunDirection = simd::normalize(simd::make_float3(1.0f, 1.0f, 0.5f));
        
        // Set uniforms.sunColor (bright warm sunlight)
        uniforms.sunColor = simd::make_float3(1.0f, 0.95f, 0.85f);
        
        // Set uniforms.cameraPosition for cylindrical billboarding
        glm::vec3 camPos = m_camera->position;
        uniforms.cameraPosition = simd::make_float3(camPos.x, camPos.y, camPos.z);
        
        // Set interactor position (circular motion for demonstration)
        float radius = 3.0f;
        float speed = 1.0f;
        float time = uniforms.time;
        simd::float3 currentInteractorPos = simd::make_float3(
            sin(time * speed) * radius,
            0.0f,  // Ball Y position: ground is at -0.5f, ball radius is 0.5f, so center at 0.0f to touch ground
            cos(time * speed) * radius
        );
        uniforms.interactorPos = currentInteractorPos;
        uniforms.interactorRadius = 1.0f;
        
        // Trample map system: Set ball position and radius
        uniforms.ballWorldPos = currentInteractorPos;
        uniforms.ballRadius = uniforms.interactorRadius;
        
        // Ground bounds for world->UV mapping (matches SCENE_SIZE)
        uniforms.groundMinXZ = simd::make_float2(-SCENE_SIZE, -SCENE_SIZE);
        uniforms.groundMaxXZ = simd::make_float2(SCENE_SIZE, SCENE_SIZE);
        
        // Time step (delta time) - approximate from frame time
        // For now, use a fixed small value (will be improved with proper delta time tracking)
        static float lastTime = 0.0f;
        float currentTime = uniforms.time;
        uniforms.dt = (currentTime - lastTime > 0.0f) ? (currentTime - lastTime) : (1.0f / 60.0f); // Default to 60fps if no delta
        lastTime = currentTime;
        
        // Trample decay rate (default: 0.35 for ~3 seconds recovery)
        uniforms.trampleDecayRate = 0.35f;
        
        // Debug visualization toggle
        uniforms.showTrampleMap = m_showTrampleMap ? 1.0f : 0.0f;
        
        // Soft interaction parameters (Ghibli-like)
        uniforms.flattenBandWidth = uniforms.ballRadius * 0.35f;
        uniforms.flattenStrength = 0.75f;
        uniforms.contactShadowRadius = uniforms.ballRadius * 0.90f;
        uniforms.contactShadowStrength = 0.55f;
        
        // Copy uniforms to buffer
        void* uniformContents = m_uniformBuffer->contents();
        memcpy(uniformContents, &uniforms, sizeof(Uniforms));
    }
    
    // ============================================================
    // UPDATE TRAMPLE MAP (Compute Shader)
    // ============================================================
    if (m_trampleComputePSO && m_trampleMapA && m_trampleMapB) {
        // Create compute command encoder
        MTL::ComputeCommandEncoder* computeEncoder = commandBuffer->computeCommandEncoder();
        
        if (computeEncoder) {
            // Set compute pipeline
            computeEncoder->setComputePipelineState(m_trampleComputePSO);
            
            // Determine input and output textures (ping-pong)
            MTL::Texture* inputTexture = m_trampleMapSwap ? m_trampleMapB : m_trampleMapA;
            MTL::Texture* outputTexture = m_trampleMapSwap ? m_trampleMapA : m_trampleMapB;
            
            // Set textures
            computeEncoder->setTexture(inputTexture, 0);  // Input (read)
            computeEncoder->setTexture(outputTexture, 1); // Output (write)
            
            // Set uniform buffer
            computeEncoder->setBuffer(m_uniformBuffer, 0, 0);
            
            // Calculate threadgroup size (assuming 16x16 threads per group)
            const int threadGroupSize = 16;
            MTL::Size threadgroupSize = MTL::Size(threadGroupSize, threadGroupSize, 1);
            MTL::Size threadgroupCount = MTL::Size(
                (inputTexture->width() + threadGroupSize - 1) / threadGroupSize,
                (inputTexture->height() + threadGroupSize - 1) / threadGroupSize,
                1
            );
            
            // Dispatch compute shader
            computeEncoder->dispatchThreadgroups(threadgroupCount, threadgroupSize);
            
            // End encoding
            computeEncoder->endEncoding();
            computeEncoder->release();
            
            // Swap ping-pong buffers
            m_trampleMapSwap = !m_trampleMapSwap;
        }
    }
    
    // Encode & Commit: Use this manually created descriptor to create the encoder, draw primitives, present the drawable, and commit
    // Create a RenderCommandEncoder
    MTL::RenderCommandEncoder* renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);
    
    // Pass 0: Sky (fullscreen gradient, always behind everything)
    if (m_skyPSO && m_skyDepthStencilState) {
        // Set sky pipeline state
        renderEncoder->setRenderPipelineState(m_skyPSO);
        
        // Set sky depth stencil state (always pass, no write)
        renderEncoder->setDepthStencilState(m_skyDepthStencilState);
        
        // Draw fullscreen triangle (no vertex buffer needed)
        renderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    }
    
    // Set depth stencil state (shared for all other passes)
    renderEncoder->setDepthStencilState(m_depthStencilState);
    
    // Pass 1: Ground

    if (m_groundPSO && m_groundVertexBuffer && m_groundTexture && m_groundTexture->getMetalTexture()) {
        // Explicit Binding: Set the correct PSO
        renderEncoder->setRenderPipelineState(m_groundPSO);
        
        // Explicit Binding: Bind the ground vertex buffer
        renderEncoder->setVertexBuffer(m_groundVertexBuffer, 0, BufferIndexMeshPositions);
        
        // Explicit Binding: Bind the uniform buffer (for both vertex and fragment shaders)
        renderEncoder->setVertexBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
        renderEncoder->setFragmentBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
        
        // Explicit Binding: Bind the ground texture
        renderEncoder->setFragmentTexture(m_groundTexture->getMetalTexture(), 0);
        
        // Draw the ground (6 vertices = 2 triangles)
        renderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
    } else {
        std::cerr << "Warning: Ground rendering skipped - missing resources" << std::endl;
    }
    
    // Pass 2: Grass
    
    // Explicit Binding: Set the correct PSO
    renderEncoder->setRenderPipelineState(m_pso);
    
    // Explicit Binding: Re-bind Vertex Buffer
    renderEncoder->setVertexBuffer(m_vertexBuffer, 0, BufferIndexMeshPositions);
    
    // Explicit Binding: Bind Instance Buffer
    renderEncoder->setVertexBuffer(m_instanceBuffer, 0, BufferIndexInstanceData);
    
    // Explicit Binding: Bind Uniform Buffer
    renderEncoder->setVertexBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
    
    // Explicit Binding: Bind Grass Texture
    renderEncoder->setFragmentTexture(m_texture->getMetalTexture(), TextureIndexGrass);
    
    // Bind Trample Map to grass shader
    MTL::Texture* currentTrampleMap = m_trampleMapSwap ? m_trampleMapA : m_trampleMapB;
    if (currentTrampleMap) {
        renderEncoder->setFragmentTexture(currentTrampleMap, TextureIndexTrampleMap);
    }
    
    // Draw Instanced Grass
    renderEncoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        NS::UInteger(kGrassIndexCount),
        MTL::IndexTypeUInt16,
        m_indexBuffer,
        NS::UInteger(0),
        NS::UInteger(30000));
    
    // Pass 3: Ball (Interactor Visualization)
    if (m_ballPSO && m_ballVertexBuffer && m_ballIndexBuffer && m_ballIndexCount > 0) {
        // Set ball pipeline state
        renderEncoder->setRenderPipelineState(m_ballPSO);
        
        // Set depth stencil state (standard read/write)
        renderEncoder->setDepthStencilState(m_depthStencilState);
        
        // Set vertex buffer (ball mesh)
        renderEncoder->setVertexBuffer(m_ballVertexBuffer, 0, BufferIndexMeshPositions);
        
        // Set uniform buffer
        renderEncoder->setVertexBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
        renderEncoder->setFragmentBuffer(m_uniformBuffer, 0, BufferIndexUniforms);
        
        // Draw ball
        renderEncoder->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            NS::UInteger(m_ballIndexCount),
            MTL::IndexTypeUInt16,
            m_ballIndexBuffer,
            NS::UInteger(0));
    }
    
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
    
    // Enable 4x MSAA for grass rendering
    pipelineDescriptor->setSampleCount(4);
    
    // Enable Alpha-to-Coverage for smooth grass edges
    pipelineDescriptor->setAlphaToCoverageEnabled(true);
    
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
    
    // Release resources (keep library for ground shaders)
    vertexFunction->release();
    fragmentFunction->release();
    vertexFunctionName->release();
    fragmentFunctionName->release();
    
    // Load Ground Shaders
    NS::String* groundVertexFunctionName = NS::String::string("groundVertexMain", NS::ASCIIStringEncoding);
    NS::String* groundFragmentFunctionName = NS::String::string("groundFragmentMain", NS::ASCIIStringEncoding);
    
    MTL::Function* groundVertexFunction = library->newFunction(groundVertexFunctionName);
    MTL::Function* groundFragmentFunction = library->newFunction(groundFragmentFunctionName);
    
    if (!groundVertexFunction || !groundFragmentFunction) {
        std::cerr << "Failed to load ground shader functions" << std::endl;
        if (groundVertexFunction) groundVertexFunction->release();
        if (groundFragmentFunction) groundFragmentFunction->release();
        groundVertexFunctionName->release();
        groundFragmentFunctionName->release();
        library->release();
        pipelineDescriptor->release();
        return;
    }
    
    // Create ground pipeline descriptor
    MTL::RenderPipelineDescriptor* groundPipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    groundPipelineDescriptor->setVertexFunction(groundVertexFunction);
    groundPipelineDescriptor->setFragmentFunction(groundFragmentFunction);
    
    // Set color attachment pixel format
    MTL::RenderPipelineColorAttachmentDescriptor* groundColorAttachment = groundPipelineDescriptor->colorAttachments()->object(0);
    groundColorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    
    // Set depth pixel format
    groundPipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    // Enable 4x MSAA for ground rendering (optional, but consistent)
    groundPipelineDescriptor->setSampleCount(4);
    
    // Create m_groundPSO
    m_groundPSO = m_device->newRenderPipelineState(groundPipelineDescriptor, &error);
    
    if (!m_groundPSO) {
        if (error) {
            NS::String* errorString = error->localizedDescription();
            std::cerr << "Failed to create ground render pipeline state: " << errorString->utf8String() << std::endl;
            errorString->release();
        } else {
            std::cerr << "Failed to create ground render pipeline state" << std::endl;
        }
    }
    
    // Release ground shader resources
    groundVertexFunction->release();
    groundFragmentFunction->release();
    groundPipelineDescriptor->release();
    groundVertexFunctionName->release();
    groundFragmentFunctionName->release();
    
    // Load Ball Shaders
    NS::String* ballVertexFunctionName = NS::String::string("vertexBall", NS::ASCIIStringEncoding);
    NS::String* ballFragmentFunctionName = NS::String::string("fragmentBall", NS::ASCIIStringEncoding);
    
    MTL::Function* ballVertexFunction = library->newFunction(ballVertexFunctionName);
    MTL::Function* ballFragmentFunction = library->newFunction(ballFragmentFunctionName);
    
    if (!ballVertexFunction || !ballFragmentFunction) {
        std::cerr << "Failed to load ball shader functions" << std::endl;
        if (ballVertexFunction) ballVertexFunction->release();
        if (ballFragmentFunction) ballFragmentFunction->release();
        ballVertexFunctionName->release();
        ballFragmentFunctionName->release();
    } else {
        // Create ball pipeline descriptor
        MTL::RenderPipelineDescriptor* ballPipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        ballPipelineDescriptor->setVertexFunction(ballVertexFunction);
        ballPipelineDescriptor->setFragmentFunction(ballFragmentFunction);
        
        // Set color attachment pixel format
        MTL::RenderPipelineColorAttachmentDescriptor* ballColorAttachment = ballPipelineDescriptor->colorAttachments()->object(0);
        ballColorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        
        // Set depth pixel format
        ballPipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        
        // Enable 4x MSAA for consistency
        ballPipelineDescriptor->setSampleCount(4);
        
        // Create m_ballPSO
        m_ballPSO = m_device->newRenderPipelineState(ballPipelineDescriptor, &error);
        
        if (!m_ballPSO) {
            if (error) {
                NS::String* errorString = error->localizedDescription();
                std::cerr << "Failed to create ball render pipeline state: " << errorString->utf8String() << std::endl;
                errorString->release();
            } else {
                std::cerr << "Failed to create ball render pipeline state" << std::endl;
            }
        }
        
        // Release ball shader resources
        ballVertexFunction->release();
        ballFragmentFunction->release();
        ballPipelineDescriptor->release();
        ballVertexFunctionName->release();
        ballFragmentFunctionName->release();
    }
    
    // Load Sky Shaders
    MTL::Library* skyLibrary = m_device->newDefaultLibrary();
    if (skyLibrary) {
        NS::String* skyVertexFunctionName = NS::String::string("vertexSkyFullscreen", NS::ASCIIStringEncoding);
        NS::String* skyFragmentFunctionName = NS::String::string("fragmentSkyGradient", NS::ASCIIStringEncoding);
        
        MTL::Function* skyVertexFunction = skyLibrary->newFunction(skyVertexFunctionName);
        MTL::Function* skyFragmentFunction = skyLibrary->newFunction(skyFragmentFunctionName);
        
        if (!skyVertexFunction || !skyFragmentFunction) {
            std::cerr << "Failed to load sky shader functions" << std::endl;
            if (skyVertexFunction) skyVertexFunction->release();
            if (skyFragmentFunction) skyFragmentFunction->release();
            skyVertexFunctionName->release();
            skyFragmentFunctionName->release();
        } else {
            // Create sky pipeline descriptor
            MTL::RenderPipelineDescriptor* skyPipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
            skyPipelineDescriptor->setVertexFunction(skyVertexFunction);
            skyPipelineDescriptor->setFragmentFunction(skyFragmentFunction);
            
            // Set color attachment pixel format (match existing PSOs)
            MTL::RenderPipelineColorAttachmentDescriptor* skyColorAttachment = skyPipelineDescriptor->colorAttachments()->object(0);
            skyColorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
            
            // Set depth pixel format (same as others, but we'll disable depth test for sky)
            skyPipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
            
            // No MSAA for sky (fullscreen triangle, no need)
            skyPipelineDescriptor->setSampleCount(1);
            
            // Create m_skyPSO
            m_skyPSO = m_device->newRenderPipelineState(skyPipelineDescriptor, &error);
            
            if (!m_skyPSO) {
                if (error) {
                    NS::String* errorString = error->localizedDescription();
                    std::cerr << "Failed to create sky render pipeline state: " << errorString->utf8String() << std::endl;
                    errorString->release();
                } else {
                    std::cerr << "Failed to create sky render pipeline state" << std::endl;
                }
            }
            
            // Release sky shader resources
            skyVertexFunction->release();
            skyFragmentFunction->release();
            skyPipelineDescriptor->release();
            skyVertexFunctionName->release();
            skyFragmentFunctionName->release();
        }
        
        skyLibrary->release();
    }
    
    // Release library and pipeline descriptor
    library->release();
    pipelineDescriptor->release();
    
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
    
    // Create Sky Depth Stencil State (always pass, no write - sky always behind)
    MTL::DepthStencilDescriptor* skyDepthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    skyDepthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunctionAlways); // Always pass
    skyDepthStencilDescriptor->setDepthWriteEnabled(false); // Don't write depth
    
    m_skyDepthStencilState = m_device->newDepthStencilState(skyDepthStencilDescriptor);
    
    if (!m_skyDepthStencilState) {
        std::cerr << "Failed to create sky depth stencil state" << std::endl;
    }
    
    skyDepthStencilDescriptor->release();
    
    // Load Trample Compute Shader
    MTL::Library* computeLibrary = m_device->newDefaultLibrary();
    if (computeLibrary) {
        NS::String* computeFunctionName = NS::String::string("updateTrampleMap", NS::ASCIIStringEncoding);
        MTL::Function* computeFunction = computeLibrary->newFunction(computeFunctionName);
        
        if (computeFunction) {
            // Create compute pipeline
            NS::Error* computeError = nullptr;
            m_trampleComputePSO = m_device->newComputePipelineState(computeFunction, &computeError);
            
            if (!m_trampleComputePSO) {
                if (computeError) {
                    NS::String* errorString = computeError->localizedDescription();
                    std::cerr << "Failed to create trample compute pipeline: " << errorString->utf8String() << std::endl;
                    errorString->release();
                } else {
                    std::cerr << "Failed to create trample compute pipeline" << std::endl;
                }
            }
            
            computeFunction->release();
        } else {
            std::cerr << "Failed to load updateTrampleMap function" << std::endl;
        }
        
        computeFunctionName->release();
        computeLibrary->release();
    } else {
        std::cerr << "Failed to load default library for compute shader" << std::endl;
    }
}

void Renderer::buildBuffers()
{
    // Define a vertical strip for a blade of grass with multiple height segments.
    // This increases geometric detail so bending in the vertex shader looks smooth and organic.
    //
    // Coordinate system for a single blade in local space:
    //  - Y from -0.5 (root) to +0.5 (tip)
    //  - X is half-width; we taper from a wider base to a very thin tip
    //  - Z stays 0 in local space; billboarding handles facing the camera
    //
    // We generate kGrassRows rows, each with left and right vertices.
    // UV.y goes from 1.0 at the bottom to 0.0 at the top.

    // Fix texture distortion: Use rectangular strip (or very slightly tapered)
    // The texture alpha defines the shape, NOT the mesh geometry
    // Making it rectangular prevents texture squeezing at the top
    const float baseWidth = 0.25f;   // Slightly wider base
    const float tipWidth = 0.25f;    // SAME as base width - rectangular strip (no pinching)
    const float heightScale = 0.7f;  // Reduce height by 30% (0.7 = 70% of original)

    Vertex vertices[kGrassVertexCount];

    for (int row = 0; row < kGrassRows; ++row) {
        float t = static_cast<float>(row) / static_cast<float>(kGrassSegments); // 0 bottom -> 1 top

        // Rectangular strip: constant width (or very slight taper if tipWidth < baseWidth)
        // Since baseWidth == tipWidth, this creates a perfect rectangle
        float width = baseWidth + (tipWidth - baseWidth) * t; // Linear interpolation (or constant if equal)
        float halfWidth = width;

        // Reduced height: -0.35 .. +0.35 (30% shorter than original -0.5 .. +0.5)
        float y = (-0.5f + t * 1.0f) * heightScale;  // Apply height reduction
        float uvY = 1.0f - t;              // 1 at bottom, 0 at top

        // Left vertex
        int leftIndex = row * kGrassVertsPerRow + 0;
        vertices[leftIndex].position = { -halfWidth, y, 0.0f };
        vertices[leftIndex].normal   = { 0.0f, 1.0f, 0.0f };
        vertices[leftIndex].texcoord = { 0.0f, uvY };

        // Right vertex
        int rightIndex = row * kGrassVertsPerRow + 1;
        vertices[rightIndex].position = { halfWidth, y, 0.0f };
        vertices[rightIndex].normal   = { 0.0f, 1.0f, 0.0f };
        vertices[rightIndex].texcoord = { 1.0f, uvY };
    }

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
    
    // Create index buffer for the strip: each vertical segment becomes 2 triangles.
    // Row r has vertices (Lr, Rr) = (2r, 2r+1)
    // Row r+1 has (Lr1, Rr1) = (2(r+1), 2(r+1)+1)
    // Triangles: (Lr, Rr, Lr1) and (Rr, Rr1, Lr1)
    uint16_t indices[kGrassIndexCount];

    int idx = 0;
    for (int seg = 0; seg < kGrassSegments; ++seg) {
        uint16_t Lr  = static_cast<uint16_t>(seg * kGrassVertsPerRow + 0);
        uint16_t Rr  = static_cast<uint16_t>(seg * kGrassVertsPerRow + 1);
        uint16_t Lr1 = static_cast<uint16_t>((seg + 1) * kGrassVertsPerRow + 0);
        uint16_t Rr1 = static_cast<uint16_t>((seg + 1) * kGrassVertsPerRow + 1);

        // First triangle
        indices[idx++] = Lr;
        indices[idx++] = Rr;
        indices[idx++] = Lr1;

        // Second triangle
        indices[idx++] = Rr;
        indices[idx++] = Rr1;
        indices[idx++] = Lr1;
    }
    
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
    
    // Generate ball (sphere) mesh
    std::vector<Vertex> ballVertices;
    std::vector<uint16_t> ballIndices;
    createSphereMesh(0.5f, 64, 32, ballVertices, ballIndices);
    
    // Create ball vertex buffer
    size_t ballVertexDataSize = ballVertices.size() * sizeof(Vertex);
    m_ballVertexBuffer = m_device->newBuffer(ballVertexDataSize, MTL::ResourceStorageModeShared);
    
    if (m_ballVertexBuffer) {
        void* ballBufferContents = m_ballVertexBuffer->contents();
        memcpy(ballBufferContents, ballVertices.data(), ballVertexDataSize);
    } else {
        std::cerr << "Failed to create ball vertex buffer" << std::endl;
    }
    
    // Create ball index buffer
    m_ballIndexCount = ballIndices.size();
    size_t ballIndexDataSize = ballIndices.size() * sizeof(uint16_t);
    m_ballIndexBuffer = m_device->newBuffer(ballIndexDataSize, MTL::ResourceStorageModeShared);
    
    if (m_ballIndexBuffer) {
        void* ballIndexBufferContents = m_ballIndexBuffer->contents();
        memcpy(ballIndexBufferContents, ballIndices.data(), ballIndexDataSize);
    } else {
        std::cerr << "Failed to create ball index buffer" << std::endl;
    }
}

void Renderer::buildInstanceBuffer()
{
    const int instanceCount = 30000;  // High density for lush Ghibli look in compact 30x30 area
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    // Use SCENE_SIZE to match ground plane dimensions
    // Grass positions range from -SCENE_SIZE to +SCENE_SIZE
    std::uniform_real_distribution<float> posDist(-SCENE_SIZE, SCENE_SIZE);
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);
    
    // Generate 10,000 InstanceData structs
    InstanceData instances[instanceCount];
    
    for (int i = 0; i < instanceCount; ++i) {
        // Position: Random x and z within scene bounds (from -SCENE_SIZE to +SCENE_SIZE). y is 0.
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
    // Update metal layer drawable size
    m_metalLayer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
    
    // Release old textures
    if (m_depthTexture) {
        m_depthTexture->release();
        m_depthTexture = nullptr;
    }
    if (m_msaaColorTexture) {
        m_msaaColorTexture->release();
        m_msaaColorTexture = nullptr;
    }
    if (m_msaaDepthTexture) {
        m_msaaDepthTexture->release();
        m_msaaDepthTexture = nullptr;
    }
    
    // Create MSAA Color Texture (4x multisample)
    MTL::TextureDescriptor* msaaColorDescriptor = MTL::TextureDescriptor::alloc()->init();
    msaaColorDescriptor->setWidth(width);
    msaaColorDescriptor->setHeight(height);
    msaaColorDescriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    msaaColorDescriptor->setTextureType(MTL::TextureType2DMultisample);
    msaaColorDescriptor->setSampleCount(4);
    msaaColorDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    msaaColorDescriptor->setStorageMode(MTL::StorageModePrivate);
    
    m_msaaColorTexture = m_device->newTexture(msaaColorDescriptor);
    if (!m_msaaColorTexture) {
        std::cerr << "Failed to create MSAA color texture" << std::endl;
    }
    msaaColorDescriptor->release();
    
    // Create MSAA Depth Texture (4x multisample)
    MTL::TextureDescriptor* msaaDepthDescriptor = MTL::TextureDescriptor::alloc()->init();
    msaaDepthDescriptor->setWidth(width);
    msaaDepthDescriptor->setHeight(height);
    msaaDepthDescriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    msaaDepthDescriptor->setTextureType(MTL::TextureType2DMultisample);
    msaaDepthDescriptor->setSampleCount(4);
    msaaDepthDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    msaaDepthDescriptor->setStorageMode(MTL::StorageModePrivate);
    
    m_msaaDepthTexture = m_device->newTexture(msaaDepthDescriptor);
    if (!m_msaaDepthTexture) {
        std::cerr << "Failed to create MSAA depth texture" << std::endl;
    }
    msaaDepthDescriptor->release();
    
    // Create resolve Depth Texture (non-multisample, for resolve target)
    MTL::TextureDescriptor* depthDescriptor = MTL::TextureDescriptor::alloc()->init();
    depthDescriptor->setWidth(width);
    depthDescriptor->setHeight(height);
    depthDescriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    depthDescriptor->setTextureType(MTL::TextureType2D);
    depthDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    depthDescriptor->setStorageMode(MTL::StorageModePrivate);
    
    m_depthTexture = m_device->newTexture(depthDescriptor);
    if (!m_depthTexture) {
        std::cerr << "Failed to create depth texture" << std::endl;
    }
    depthDescriptor->release();
}

void Renderer::buildTextures()
{
    // Create m_texture instance (grass)
    // Note: Make sure to check path. Since we copy assets to bin, relative path "assets/grass_albedo.png" should work.
    try {
        m_texture = new Texture(m_device, m_commandQueue, "assets/grass_albedo.png");
        std::cout << "Successfully loaded grass texture" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load grass texture: " << e.what() << std::endl;
        m_texture = nullptr;
    }
    
    // Create m_groundTexture instance (ground)
    // Try PNG first, then JPG as fallback
    try {
        m_groundTexture = new Texture(m_device, m_commandQueue, "assets/ground_albedo.png");
        std::cout << "Successfully loaded ground texture (PNG)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ground texture (PNG): " << e.what() << std::endl;
        try {
            m_groundTexture = new Texture(m_device, m_commandQueue, "assets/ground_albedo.jpg");
            std::cout << "Successfully loaded ground texture (JPG)" << std::endl;
        } catch (const std::exception& e2) {
            std::cerr << "Failed to load ground texture (JPG): " << e2.what() << std::endl;
            m_groundTexture = nullptr;
        }
    }
}

void Renderer::buildGround()
{
    // Define vertices for a large quad (Plane) centered at (0,0,0)
    // Size: Uses SCENE_SIZE constant (From -SCENE_SIZE to +SCENE_SIZE)
    // Y-Position: -0.5f (Slightly below the grass roots so grass sticks into it)
    // Normals: Point Straight Up (0, 1, 0)
    // UVs: Scale them! Instead of 0..1, use 0..20.0f. This will tile the texture 20 times.
    
    // Use 6 vertices (2 triangles) directly to avoid managing a separate index buffer for the ground
    Vertex groundVertices[6] = {
        // First triangle
        { {-SCENE_SIZE, -0.5f, -SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {0.0f, 20.0f} },   // Bottom-Left
        { { SCENE_SIZE, -0.5f, -SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {20.0f, 20.0f} },  // Bottom-Right
        { {-SCENE_SIZE, -0.5f,  SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },   // Top-Left
        
        // Second triangle
        { { SCENE_SIZE, -0.5f, -SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {20.0f, 20.0f} },  // Bottom-Right
        { { SCENE_SIZE, -0.5f,  SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {20.0f, 0.0f} },   // Top-Right
        { {-SCENE_SIZE, -0.5f,  SCENE_SIZE}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} }     // Top-Left
    };
    
    // Create m_groundVertexBuffer with this data
    size_t groundVertexDataSize = sizeof(groundVertices);
    m_groundVertexBuffer = m_device->newBuffer(groundVertexDataSize, MTL::ResourceStorageModeShared);
    
    if (m_groundVertexBuffer) {
        // Copy vertex data to buffer
        void* bufferContents = m_groundVertexBuffer->contents();
        memcpy(bufferContents, groundVertices, groundVertexDataSize);
    } else {
        std::cerr << "Failed to create ground vertex buffer" << std::endl;
    }
}

void Renderer::buildTrampleMaps()
{
    // Create 1024x1024 single-channel textures for ping-pong trample map
    const int trampleMapSize = 1024;
    
    MTL::TextureDescriptor* textureDesc = MTL::TextureDescriptor::alloc()->init();
    textureDesc->setWidth(trampleMapSize);
    textureDesc->setHeight(trampleMapSize);
    textureDesc->setPixelFormat(MTL::PixelFormatR16Float);
    textureDesc->setTextureType(MTL::TextureType2D);
    textureDesc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    textureDesc->setStorageMode(MTL::StorageModePrivate);
    
    // Create ping-pong textures
    m_trampleMapA = m_device->newTexture(textureDesc);
    m_trampleMapB = m_device->newTexture(textureDesc);
    
    if (!m_trampleMapA || !m_trampleMapB) {
        std::cerr << "Failed to create trample map textures" << std::endl;
    }
    
    // Initialize both textures to zero (no trampling initially)
    // We'll need to clear them using a compute shader or render pass
    // For now, they start at zero by default
    
    textureDesc->release();
}

void Renderer::createSphereMesh(float radius, int radialSegments, int verticalSegments,
                                 std::vector<Vertex>& vertices, std::vector<uint16_t>& indices)
{
    // Generate a perfect sphere mesh
    
    vertices.clear();
    indices.clear();
    
    // Generate sphere vertices
    for (int ring = 0; ring <= verticalSegments; ++ring) {
        float theta = static_cast<float>(ring) / static_cast<float>(verticalSegments) * M_PI; // 0 to PI
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);
        
        for (int seg = 0; seg <= radialSegments; ++seg) {
            float phi = static_cast<float>(seg) / static_cast<float>(radialSegments) * 2.0f * M_PI; // 0 to 2*PI
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);
            
            // Generate sphere vertex position (unit sphere, will scale by radius)
            float sphereY = cosTheta; // -1 to 1
            float sphereX = sinTheta * cosPhi;
            float sphereZ = sinTheta * sinPhi;
            
            // Calculate sphere normal (same as position for unit sphere)
            simd::float3 sphereNormal = {sphereX, sphereY, sphereZ};
            float len = sqrt(sphereNormal.x * sphereNormal.x + sphereNormal.y * sphereNormal.y + sphereNormal.z * sphereNormal.z);
            if (len > 0.0001f) {
                sphereNormal = {sphereNormal.x / len, sphereNormal.y / len, sphereNormal.z / len};
            }
            
            // Scale by radius
            float x = sphereX * radius;
            float y = sphereY * radius;
            float z = sphereZ * radius;
            
            Vertex v;
            v.position = {x, y, z};
            v.normal = sphereNormal;
            v.texcoord = {static_cast<float>(seg) / radialSegments, static_cast<float>(ring) / verticalSegments};
            vertices.push_back(v);
        }
    }
    
    // Generate indices for sphere topology
    for (int ring = 0; ring < verticalSegments; ++ring) {
        int ringStart = ring * (radialSegments + 1);
        int nextRingStart = (ring + 1) * (radialSegments + 1);
        
        for (int seg = 0; seg < radialSegments; ++seg) {
            int current = ringStart + seg;
            int next = ringStart + seg + 1;
            int below = nextRingStart + seg;
            int belowNext = nextRingStart + seg + 1;
            
            // First triangle
            indices.push_back(static_cast<uint16_t>(current));
            indices.push_back(static_cast<uint16_t>(below));
            indices.push_back(static_cast<uint16_t>(next));
            
            // Second triangle
            indices.push_back(static_cast<uint16_t>(next));
            indices.push_back(static_cast<uint16_t>(below));
            indices.push_back(static_cast<uint16_t>(belowNext));
        }
    }
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
    
    // Toggle trample map visualization (T key)
    bool currentTKeyState = (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS);
    if (currentTKeyState && !m_prevTKeyState) {
        // T key was just pressed (toggle)
        m_showTrampleMap = !m_showTrampleMap;
        std::cout << "Trample map visualization: " << (m_showTrampleMap ? "ON" : "OFF") << std::endl;
    }
    m_prevTKeyState = currentTKeyState;
    
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

