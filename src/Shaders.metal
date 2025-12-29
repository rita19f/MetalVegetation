#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// 定义顶点着色器的输出（光栅化插值数据）
struct RasterizerData {
    float4 position [[position]];
    float2 texcoord;
    float3 normal;
};

// Pseudo-random function based on instance ID
float random(float seed) {
    return fract(sin(seed * 12.9898) * 43758.5453);
}

// Struct to hold instance variation data
struct InstanceVariation {
    float rotation;
    float scale;
};

// Generate random rotation and scale based on instance ID
InstanceVariation getInstanceVariation(uint instanceID) {
    float rand1 = random(float(instanceID));
    float rand2 = random(float(instanceID) * 0.5 + 100.0);
    float rand3 = random(float(instanceID) * 0.3 + 200.0); // For bend direction
    float rand4 = random(float(instanceID) * 0.7 + 300.0); // For initial tilt
    
    InstanceVariation variation;
    // Random rotation: 0 to 360 degrees
    variation.rotation = rand1 * 6.28318; // 2 * PI
    
    // Random scale: 0.8 to 1.2
    variation.scale = 0.8 + rand2 * 0.4;
    
    // Store additional random values for bend direction and tilt
    // We'll use rand3 and rand4 in the vertex shader directly
    
    return variation;
}

// Generate random bend direction angle (0 to 2*PI)
float getBendDirection(uint instanceID) {
    float rand = random(float(instanceID) * 0.3 + 200.0);
    return rand * 6.28318; // 0 to 2*PI
}

// Generate random initial tilt angle (in radians, small range)
float getInitialTilt(uint instanceID) {
    float rand = random(float(instanceID) * 0.7 + 300.0);
    // Tilt range: -15 to +15 degrees converted to radians
    return (rand - 0.5) * 0.5236; // ±15 degrees = ±0.5236 radians
}

// Build a rotation matrix around Y-axis
float4x4 rotationY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float4x4(
        float4(c, 0, s, 0),
        float4(0, 1, 0, 0),
        float4(-s, 0, c, 0),
        float4(0, 0, 0, 1)
    );
}

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
    
    // Extract world position from instance model matrix (translation component)
    float3 instanceWorldPos = float3(
        instance.modelMatrix.columns[3].x,
        instance.modelMatrix.columns[3].y,
        instance.modelMatrix.columns[3].z
    );
    
    // ============================================================================
    // 3. Randomization: Generate random rotation and scale per instance
    // ============================================================================
    InstanceVariation variation = getInstanceVariation(instanceID);
    float randomRotation = variation.rotation;
    float randomScale = variation.scale;
    
    // ============================================================================
    // 2. Cylindrical Billboarding: Rotate quad to face camera (Y-axis only)
    // ============================================================================
    // Calculate direction from grass position to camera
    float3 toCamera = normalize(uniforms.cameraPosition - instanceWorldPos);
    
    // Project to XZ plane (remove Y component) for cylindrical billboarding
    float3 toCameraXZ = normalize(float3(toCamera.x, 0.0, toCamera.z));
    
    // Calculate angle to rotate around Y-axis to face camera
    float billboardAngle = atan2(toCameraXZ.x, toCameraXZ.z);
    
    // Combine random rotation with billboard rotation
    float finalRotation = billboardAngle + randomRotation;
    
    // Build rotation matrix around Y-axis
    float4x4 billboardRotation = rotationY(finalRotation);
    
    // ============================================================================
    // Apply transformations
    // ============================================================================
    // Read vertex position (local space, relative to quad center)
    float3 vertexPosition = vertices[vertexID].position;
    float2 texcoord = vertices[vertexID].texcoord;
    
    // Normalize Y coordinate: -0.5 (bottom) to 0.5 (top) -> 0.0 (bottom) to 1.0 (top)
    float normalizedY = (vertexPosition.y + 0.5) / 1.0; // Maps -0.5..0.5 to 0..1
    
    // ============================================================================
    // 3. Random Initial Tilt: Real grass doesn't grow straight up
    // ============================================================================
    float initialTiltAngle = getInitialTilt(instanceID);
    float tiltAxisChoice = random(float(instanceID) * 0.9 + 400.0); // 0-1, choose X or Z axis
    
    // Simple tilt: rotate around X or Z axis randomly
    float c = cos(initialTiltAngle);
    float s = sin(initialTiltAngle);
    
    if (tiltAxisChoice < 0.5) {
        // Tilt around X-axis (lean forward/backward)
        float y = vertexPosition.y;
        float z = vertexPosition.z;
        vertexPosition.y = y * c - z * s;
        vertexPosition.z = y * s + z * c;
    } else {
        // Tilt around Z-axis (lean left/right)
        float x = vertexPosition.x;
        float y = vertexPosition.y;
        vertexPosition.x = x * c - y * s;
        vertexPosition.y = x * s + y * c;
    }
    
    // Apply random scale
    vertexPosition *= randomScale;
    
    // ============================================================================
    // 2. Aggressive Bending with Parabolic Drop (Length-Preserving Look)
    // ============================================================================
    // Get random bend direction angle (0 to 2*PI)
    float bendDirectionAngle = getBendDirection(instanceID);
    float3 bendDirection = float3(cos(bendDirectionAngle), 0.0, sin(bendDirectionAngle));
    
    // Wind Animation: Time-based sine wave with instance variation
    float windSpeed = 2.5; // Wind speed multiplier
    float wind = sin(uniforms.time * windSpeed + float(instanceID) * 0.1);
    
    // Bend strength base
    float windIntensity = 0.5; // Overall wind intensity
    float bendBase = wind * windIntensity;
    
    // Parabolic curve: stronger effect towards the tip
    float t = 1.0 - texcoord.y;           // 0 at bottom (uv.y=1), 1 at top (uv.y=0)
    float bendStrength = bendBase * (t * t); // Quadratic dependence on height
    
    // 1. Move tip outwards (XZ plane)
    vertexPosition.xz += bendDirection.xz * bendStrength;
    
    // 2. Move tip downwards to simulate curvature and approximate length preservation
    vertexPosition.y -= abs(bendStrength) * 0.4;
    
    // ============================================================================
    // Apply billboard rotation (rotate around Y-axis to face camera)
    // ============================================================================
    float4 rotatedPos = billboardRotation * float4(vertexPosition, 1.0);
    
    // Translate to world position
    float3 worldPos = instanceWorldPos + rotatedPos.xyz;
    
    // Calculate final position using View/Projection matrices
    float4 pos = uniforms.projectionMatrix * uniforms.viewMatrix * float4(worldPos, 1.0);
    
    // ============================================================================
    // 3. Soften normals: blend up vector with bend direction
    // ============================================================================
    float3 softNormal = normalize(float3(0.0, 1.0, 0.0) + bendDirection * 0.3);
    
    // Output position and attributes
    out.position = pos;
    out.texcoord = texcoord;
    out.normal = softNormal;
    
    return out;
}

fragment float4 fragmentMain(
    RasterizerData in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]]
) {
    // Define a constexpr sampler inside the shader function
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
    
    // ============================================================================
    // 1. Analytic Antialiasing: Smooth alpha edges using derivatives
    // ============================================================================
    // Sample the texture color
    float4 textureSample = colorTexture.sample(textureSampler, in.texcoord);
    float alpha = textureSample.a;
    
    // Calculate how fast alpha is changing relative to screen pixels
    // fwidth() returns the sum of absolute derivatives in x and y screen space
    float px = fwidth(alpha);
    
    // Calculate a smooth opacity based on the 0.5 threshold
    // smoothstep creates a smooth transition around the threshold
    // The transition width is controlled by px (derivative-based)
    float opacity = smoothstep(0.5 - px, 0.5 + px, alpha);
    
    // Apply generic transparency adjustment - discard very transparent fragments
    if (opacity < 0.1) {
        discard_fragment();
    }
    
    // ============================================================================
    // 2. Procedural Coloring: Generate vertical gradient RGB (ignore texture RGB)
    // ============================================================================
    // Define Ghibli-style colors
    float3 darkGreen = float3(0.2, 0.5, 0.2);   // Dark green at bottom (y=0)
    float3 lightGreen = float3(0.4, 0.8, 0.4);  // Light green at top (y=1)
    
    // Create vertical gradient based on UV.y
    // in.texcoord.y: 0 = top of grass, 1 = bottom of grass
    // We want dark at bottom (y=1) and light at top (y=0), so use (1.0 - in.texcoord.y)
    float gradientFactor = 1.0 - in.texcoord.y;
    float3 proceduralColor = mix(darkGreen, lightGreen, gradientFactor);
    
    // Return procedural color with smoothed opacity for Alpha-to-Coverage
    return float4(proceduralColor, opacity);
}