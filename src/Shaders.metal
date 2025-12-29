#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// 定义顶点着色器的输出（光栅化插值数据）
struct RasterizerData {
    float4 position [[position]];
    float2 texcoord;
    float3 normal;
    float3 worldPos; // World position for view direction calculation
    float instanceHash; // Hash value from instanceID for color variation
};

// Pseudo-random function based on instance ID
float random(float seed) {
    return fract(sin(seed * 12.9898) * 43758.5453);
}

// Hash function for instanceID (returns 0.0 to 1.0)
float hash(uint instanceID) {
    return random(float(instanceID) * 0.12345 + 500.0);
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
    
    InstanceVariation variation;
    // Random rotation: 0 to 360 degrees
    variation.rotation = rand1 * 6.28318; // 2 * PI
    
    // Random scale: 0.8 to 1.2
    variation.scale = 0.8 + rand2 * 0.4;
    
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
    // 3. Hybrid Normals: Forward-Up blend for directional shading
    // ============================================================================
    // Calculate forward normal (the direction the blade face is looking)
    // The blade faces forward in local space (0, 0, 1), rotated by billboard rotation
    float3 forwardLocal = float3(0.0, 0.0, 1.0); // Forward direction in local space
    float3 forwardNormal = (billboardRotation * float4(forwardLocal, 0.0)).xyz;
    
    // Mix forward direction (50%) with up direction (80%) for stylized normal
    // This keeps softness but adds enough directional shading so blades catch light
    float3 stylizedNormal = normalize(forwardNormal * 0.5 + float3(0.0, 1.0, 0.0) * 0.8);
    
    // Output position and attributes
    out.position = pos;
    out.texcoord = texcoord;
    out.normal = stylizedNormal; // Use hybrid forward-up normal for directional shading
    out.worldPos = worldPos; // Pass world position for view direction calculation
    out.instanceHash = hash(instanceID); // Pass instance hash for color variation
    
    return out;
}

fragment float4 fragmentMain(
    RasterizerData in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
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
    // Calculate height factor: t=0.0 at root (bottom), t=1.0 at tip (top)
    // Assuming texture Y=1 is bottom and Y=0 is top (common in Metal/stb_image)
    // We want t=0 at bottom, t=1 at top for the gradient
    float t = 1.0 - in.texcoord.y; // t=0 at bottom (texcoord.y=1), t=1 at top (texcoord.y=0)
    
    // Define Colors (Lush Green Style)
    float3 rootColor = float3(0.05, 0.2, 0.05);   // Very Dark Green (almost black) at roots
    float3 midColor  = float3(0.1, 0.5, 0.1);     // Healthy Base Green in middle
    float3 tipColor  = float3(0.4, 0.8, 0.2);     // Vibrant but not neon green at tips
    
    // Multi-stop gradient for better look
    float3 gradientColor;
    if (t < 0.5) {
        // Bottom half: root to mid
        gradientColor = mix(rootColor, midColor, t * 2.0);
    } else {
        // Top half: mid to tip
        gradientColor = mix(midColor, tipColor, (t - 0.5) * 2.0);
    }
    
    // ============================================================================
    // 3. Per-Instance Color Variation: Lush Green Variations
    // ============================================================================
    // Use instance hash for per-blade distinctness (passed from vertex shader)
    float noise = in.instanceHash; // Returns 0.0 to 1.0
    
    // Create lighter green variation (fresh, not dead)
    float3 lighterGreen = float3(0.2, 0.7, 0.3); // Fresh, lighter green variation
    
    // Mix base gradient color with lighter variation - subtle mix for natural variation
    float3 variedColor = mix(gradientColor, lighterGreen, noise * 0.3); // Max 30% variation
    
    // ============================================================================
    // 4. Half-Lambert Diffuse + Translucency (Ghibli-style lighting)
    // ============================================================================
    float3 normal = normalize(in.normal);
    float3 sunDir = normalize(uniforms.sunDirection);
    
    // Half-Lambert: wraps light around, simulating translucency
    float NdotL = dot(normal, sunDir);
    float halfLambert = NdotL * 0.5 + 0.5; // Maps -1..1 to 0..1
    
    // Ambient light
    float ambient = 0.3;
    
    // Combine diffuse lighting
    float3 lighting = uniforms.sunColor * (halfLambert + ambient);
    
    // ============================================================================
    // 5. Specular Highlight (Waxy look, mainly near tips)
    // ============================================================================
    // Calculate view direction
    float3 viewDir = normalize(uniforms.cameraPosition - in.worldPos);
    
    // Blinn-Phong half vector
    float3 halfDir = normalize(sunDir + viewDir);
    
    // Specular calculation (softened for less plastic look)
    float specularStrength = 0.1; // Reduced from 0.15 for softer highlights
    float specularPower = 64.0; // Crisp, tight highlight
    float spec = pow(max(0.0, dot(normal, halfDir)), specularPower) * specularStrength;
    
    // Make specular stronger near tips (where UV.y is closer to 0)
    float tipFactor = 1.0 - in.texcoord.y; // 0 at bottom, 1 at top
    spec *= tipFactor; // Specular is stronger at tips
    
    // Add specular to lighting
    float3 specularLight = uniforms.sunColor * spec;
    
    // ============================================================================
    // 6. Final Color Composition
    // ============================================================================
    // FinalColor = (GradientColor * RandomVariation) * (HalfLambert + Ambient) + Specular
    float3 finalColor = variedColor * lighting + specularLight;
    
    // ============================================================================
    // 7. Stronger Ambient Occlusion (AO) at Roots
    // ============================================================================
    // Strictly enforce darker roots to blend with dark green ground
    // t=0.0 at bottom (root), t=1.0 at top (tip)
    // We want darkening ONLY at the bottom 40%
    // smoothstep(0.0, 0.4, t) gives 0.0 at t=0 (root), 1.0 at t=0.4 (transition point)
    float occlusion = smoothstep(0.0, 0.4, t);
    
    // Apply intensity: 0.3 brightness at root (dark), 1.0 brightness at tip (bright)
    float aoFactor = 0.3 + 0.7 * occlusion;
    
    // Apply AO to the color: roots are shadow-dark, tips are fully lit
    finalColor *= aoFactor;
    
    // Return final color with smoothed opacity for Alpha-to-Coverage
    return float4(finalColor, opacity);
}