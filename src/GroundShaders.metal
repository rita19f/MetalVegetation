#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// Ground vertex shader output
struct GroundRasterizerData {
    float4 position [[position]];
    float3 normal;
    float2 texcoord;
};

vertex GroundRasterizerData groundVertexMain(
    uint vertexID [[vertex_id]],
    constant Vertex *vertices [[buffer(BufferIndexMeshPositions)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    GroundRasterizerData out;
    
    // Read vertex attributes
    float3 position = vertices[vertexID].position;
    float3 normal = vertices[vertexID].normal;
    float2 texcoord = vertices[vertexID].texcoord;
    
    // Transform position: uniforms.projectionMatrix * uniforms.viewMatrix * float4(in.position, 1.0)
    // The ground is static at (0,0,0) so we don't need a model matrix (vertices are in world space)
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * float4(position, 1.0);
    
    // Pass texcoord and normal to fragment
    out.normal = normal;
    out.texcoord = texcoord;
    
    return out;
}

fragment float4 groundFragmentMain(
    GroundRasterizerData in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    // Define a constexpr sampler with repeat address mode (Crucial for tiling)
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::repeat);
    
    // Sample the texture color
    float4 textureColor = colorTexture.sample(textureSampler, in.texcoord);
    
    // Basic lighting: diffuse (dot product of normal & lightDirection) + ambient
    float3 normal = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightDirection);
    
    // Calculate diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Ambient level
    float ambient = 0.4;
    
    // Mix lighting
    float3 lighting = uniforms.lightColor * (NdotL + ambient);
    
    // Apply lighting to texture color
    float4 finalColor = float4(textureColor.rgb * lighting, textureColor.a);
    
    // No Alpha Discard (Ground is opaque)
    return finalColor;
}

