#pragma once

#ifdef __METAL_VERSION__
    #include <metal_stdlib>
    using namespace metal;
    // Metal has built-in types: float2, float3, float4, float4x4
#else
    #include <simd/simd.h>
    using float2 = simd::float2;
    using float3 = simd::float3;
    using float4 = simd::float4;
    using float4x4 = simd::float4x4;
#endif

enum VertexAttributes {
    VertexAttributePosition = 0,
    VertexAttributeNormal   = 1, 
    VertexAttributeTexcoord = 2  
};

enum BufferIndices {
    BufferIndexMeshPositions = 0,
    BufferIndexInstanceData  = 1, 
    BufferIndexUniforms      = 2
};

enum TextureIndices {
    TextureIndexGrass = 0,
    TextureIndexTrampleMap = 1
};

// Vertex structure - alignment safe between C++ and Metal
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
};

// Instance data for each grass blade
struct InstanceData {
    float4x4 modelMatrix; 
};

struct Uniforms {
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3 lightDirection; // Light direction (used by ground shader)
    float3 lightColor; // Light color (used by ground shader)
    float time; // Time for wind animation
    float3 cameraPosition; // Camera position for billboard calculations
    float3 sunDirection; // Sun direction for lighting calculations
    float3 sunColor; // Sun color
    float3 interactorPos; // World position of the ball (used for ball shader positioning)
    float interactorRadius; // Ball radius (used to set ballRadius)
    
    // Trample map system
    float3 ballWorldPos; // Ball center position (world space)
    float ballRadius; // Ball radius for trample map
    float2 groundMinXZ; // Ground bounds min (X, Z) for world->UV mapping
    float2 groundMaxXZ; // Ground bounds max (X, Z) for world->UV mapping
    float dt; // Time step for trample decay
    float trampleDecayRate; // Decay rate (default: 0.35)
    float showTrampleMap; // Debug flag: 1.0 to visualize trample map, 0.0 for normal rendering
    
    // Soft interaction parameters (Ghibli-like)
    float flattenBandWidth; // Width of flatten ring around ball
    float flattenStrength; // Strength of flatten compression (0-1)
    float contactShadowRadius; // Radius of contact shadow effect
    float contactShadowStrength; // Strength of contact shadow darkening (0-1)
};