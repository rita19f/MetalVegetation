#pragma once
#include <simd/simd.h>

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

// 顶点结构体
struct Vertex {
    simd::float3 position;
    simd::float3 normal;
    simd::float2 texcoord;
};

// 定义每一棵草的数据
struct InstanceData {
    simd::float4x4 modelMatrix; 
};

struct Uniforms {
    simd::float4x4 viewMatrix;
    simd::float4x4 projectionMatrix;
    simd::float3 lightDirection; // 光源方向
    simd::float3 lightColor; // 光源颜色
    float time; // 时间，用于风吹草动
};