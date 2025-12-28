#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// 定义顶点着色器的输出（光栅化插值数据）
struct RasterizerData {
    float4 position [[position]];
    float3 normal;
    float2 texcoord;
};

vertex RasterizerData vertexMain(
    uint vertexID [[vertex_id]],
    uint instanceID [[instance_id]],
    constant Vertex *vertices [[buffer(BufferIndexMeshPositions)]],
    constant InstanceData *instances [[buffer(BufferIndexInstanceData)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    RasterizerData out;
    
    // Get the current instance data
    InstanceData instance = instances[instanceID];
    
    // 读取当前顶点的属性
    float3 vertexPosition = vertices[vertexID].position;
    float3 normal = vertices[vertexID].normal; // Normal with attribute(VertexAttributeNormal)
    float2 texcoord = vertices[vertexID].texcoord;
    
    // Convert position to float4 for matrix multiplication
    float4 position4 = float4(vertexPosition, 1.0);
    
    // Apply model matrix first to get world position
    float4 pos = instance.modelMatrix * position4;
    
    // Wind Animation: Create a sine wave function
    // Adding instance.modelMatrix[3].x (world position) inside the sin makes each grass blade sway slightly differently (out of phase)
    float sway = sin(uniforms.time * 2.0f + instance.modelMatrix.columns[3].x) * 0.1f;
    
    // Apply wind displacement: Use UV y-coordinate as a mask
    // If UV.y is 1 (bottom), movement is 0. If UV.y is 0 (top), movement is max.
    // The grass roots (bottom) must stay fixed, so we use (1.0 - texcoord.y) as the mask
    pos.x += sway * (1.0 - texcoord.y);
    
    // Calculate final position using View/Projection matrices
    pos = uniforms.projectionMatrix * uniforms.viewMatrix * pos;
    
    // Transform normal by model matrix (rotation only): Multiply normal by instance.modelMatrix
    // For correctness: out.normal = (instance.modelMatrix * float4(v.normal, 0.0)).xyz;
    float3 transformedNormal = (instance.modelMatrix * float4(normal, 0.0)).xyz;
    
    // Output position
    out.position = pos;
    out.normal = transformedNormal;
    out.texcoord = texcoord;
    
    return out;
}

fragment float4 fragmentMain(
    RasterizerData in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    // Define a constexpr sampler inside the shader function
    // Update the constexpr sampler to use Repeat address mode for both S and T coordinates
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::repeat);
    
    // Sample the texture color
    float4 textureColor = colorTexture.sample(textureSampler, in.texcoord);
    
    // DEBUG: If texture is fully transparent/black, show Red to indicate load failure
    if (textureColor.a == 0.0 && textureColor.r == 0.0 && textureColor.g == 0.0 && textureColor.b == 0.0) {
        return float4(1.0, 0.0, 0.0, 1.0); // Bright Red = Texture is empty/black
    }
    
    // Check Alpha: If color.a < 0.5, call discard_fragment()
    if (textureColor.a < 0.5) {
        discard_fragment();
    }
    
    // Normalize the input normal and light direction
    float3 normal = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightDirection);
    
    // Diffuse: Calculate float NdotL = max(dot(in.normal, uniforms.lightDirection), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Ambient: Define a base ambient level, e.g., 0.4
    float ambient = 0.4;
    
    // Mix: float3 lighting = uniforms.lightColor * (NdotL + 0.4);
    float3 lighting = uniforms.lightColor * (NdotL + ambient);
    
    // Apply lighting to the texture color: float4 finalColor = float4(textureColor.rgb * lighting, textureColor.a);
    float4 finalColor = float4(textureColor.rgb * lighting, textureColor.a);
    
    // Return finalColor
    return finalColor;
}