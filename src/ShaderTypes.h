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

// 顶点结构体 - Alignment safe between C++ and Metal
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
};

// 定义每一棵草的数据
struct InstanceData {
    float4x4 modelMatrix; 
};

struct Uniforms {
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3 lightDirection; // 光源方向 (legacy, kept for compatibility)
    float3 lightColor; // 光源颜色 (legacy, kept for compatibility)
    float time; // 时间，用于风吹草动
    float3 cameraPosition; // 相机位置，用于圆柱形广告牌
    float3 sunDirection; // 太阳方向，用于光照计算
    float3 sunColor; // 太阳颜色
    float3 interactorPos; // The world position of the object crushing the grass (legacy, kept for capsule rendering)
    float interactorRadius; // How wide the crushing effect is (e.g., 2.0) (legacy, kept for capsule rendering)
    float interactorStrength; // How hard it pushes (e.g., 1.0) (legacy, kept for compatibility)
    
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