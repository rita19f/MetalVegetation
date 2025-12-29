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
};