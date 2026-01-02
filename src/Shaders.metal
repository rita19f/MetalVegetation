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
    float windStrength; // Wind bend amount for "Wind Sheen" effect (Ghibli style)
    float isYellow; // Flag for yellow-green withered grass (0.0 = normal, 1.0 = yellow)
    float influence; // Interaction influence factor (1.0 = fully crushed, 0.0 = unaffected)
};

// Pseudo-random function based on instance ID
float random(float seed) {
    return fract(sin(seed * 12.9898) * 43758.5453);
}

// Hash function for instanceID (returns 0.0 to 1.0)
float hash(uint instanceID) {
    return random(float(instanceID) * 0.12345 + 500.0);
}

// ---------------------------------------------------------
// IMPROVED NOISE FUNCTION (Bilinear Smooth)
// ---------------------------------------------------------
float noise2D(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f); // Cubic smoothing
    
    // Hash function integrated
    float2 magic = float2(12.9898, 78.233);
    float a = fract(sin(dot(i, magic)) * 43758.5453);
    float b = fract(sin(dot(i + float2(1.0, 0.0), magic)) * 43758.5453);
    float c = fract(sin(dot(i + float2(0.0, 1.0), magic)) * 43758.5453);
    float d = fract(sin(dot(i + float2(1.0, 1.0), magic)) * 43758.5453);
    
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
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

// Rodrigues' rotation formula - preserves both length and width/volume
float3 rotateVector(float3 v, float3 axis, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return v * c + axis * dot(axis, v) * (1.0 - c) + cross(axis, v) * s;
}

// ---------------------------------------------------------
// OPTIMIZED VERTEX SHADER (FIXED SWAY & NORMALS)
// ---------------------------------------------------------
vertex RasterizerData vertexMain(
    uint vertexID [[vertex_id]],
    uint instanceID [[instance_id]],
    constant Vertex *vertices [[buffer(BufferIndexMeshPositions)]],
    constant InstanceData *instances [[buffer(BufferIndexInstanceData)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    RasterizerData out;
    InstanceData instance = instances[instanceID];
    
    // 1. Get Base Instance World Position
    float3 instanceWorldPos = float3(
        instance.modelMatrix.columns[3].x, 
        instance.modelMatrix.columns[3].y, 
        instance.modelMatrix.columns[3].z
    );
    
    // 2. Randomization & Attributes
    InstanceVariation variation = getInstanceVariation(instanceID);
    float randomRotation = variation.rotation;
    
    // 3. Billboard Rotation Calculation
    float3 toCamera = normalize(uniforms.cameraPosition - instanceWorldPos);
    float3 toCameraXZ = normalize(float3(toCamera.x, 0.0, toCamera.z));
    float billboardAngle = atan2(toCameraXZ.x, toCameraXZ.z);
    float finalRotation = billboardAngle + randomRotation;
    float4x4 billboardRotation = rotationY(finalRotation);
    
    // 4. Vertex Setup
    float3 vertexPosition = vertices[vertexID].position;
    float2 texcoord = vertices[vertexID].texcoord;
    float t = 1.0 - texcoord.y; // 0=Root, 1=Tip
    
    // 5. Initial Tilt
    float initialTiltAngle = getInitialTilt(instanceID);
    float tiltAxisChoice = hash(instanceID); 
    float c = cos(initialTiltAngle);
    float s = sin(initialTiltAngle);
    if (tiltAxisChoice < 0.5) {
        vertexPosition.y = vertexPosition.y * c - vertexPosition.z * s; vertexPosition.z = vertexPosition.y * s + vertexPosition.z * c;
    } else {
        vertexPosition.x = vertexPosition.x * c - vertexPosition.y * s; vertexPosition.y = vertexPosition.x * s + vertexPosition.y * c;
    }
    vertexPosition *= variation.scale;
    
    // 6. Define Initial World Position (Pre-Wind)
    float4 rotatedPos = billboardRotation * float4(vertexPosition, 1.0);
    float3 finalWorldPos = instanceWorldPos + rotatedPos.xyz;
    
    // ============================================================
    // [START] WIND PHYSICS & ROTATION LOGIC
    // ============================================================
    
    // 1. Idle Chaos (呼吸感)
    float tTime = uniforms.time;
    float seed = float(instanceID);
    float idleFreq = 2.0 + hash(instanceID) * 1.5; 
    float idlePhase = seed * 13.0;
    // 微风幅度稍微加大，确保静止时也有动态
    float idleStrength = sin(tTime * idleFreq + idlePhase) * 0.08; 
    
    // 2. Roystan-Style Fluid Wind (主风浪)
    float2 windDir = normalize(float2(1.0, 0.5)); 
    
    // A. Main Swell (大波浪)
    // [关键修复] 移除 +0.5 的强制正向偏移，允许负值回弹
    float swellPhase = dot(instanceWorldPos.xz, windDir);
    float rawSine = sin(swellPhase * 0.05 - tTime * 1.0); // -1 到 1
    
    // 映射到 [-0.3, 1.0] 区间
    // 1.0 (向右大倒伏) -> 0.0 (直立) -> -0.3 (向左惯性回弹)
    // 这样草就会有"来回摆动"的感觉，而不仅仅是"磕头"
    float mainSwell = rawSine * 0.65 + 0.35; 
    
    // B. Turbulence (细节噪声)
    float2 noiseUV = instanceWorldPos.xz * 0.2 - windDir * tTime * 2.0;
    float turbulence = noise2D(noiseUV); // 0..1
    // 让噪声也能稍微向左扰动 (-0.2 到 0.8)
    turbulence = turbulence - 0.2;
    
    // C. Composite (混合)
    float fluidWind = mix(mainSwell, turbulence, 0.3);
    
    // 3. Combine Forces
    float totalWindStrength = fluidWind + idleStrength;
    
    // 4. Calculate Rotation Angle (Linear Rigid)
    // 增加一点灵敏度 (1.0 -> 1.2) 让摆动更明显
    float bendAngle = totalWindStrength * t * 1.2; 
    
    // 5. Calculate Rotation Axis (With Jitter)
    float axisJitter = (hash(seed) - 0.5) * 0.2; 
    float3 windVector3D = normalize(float3(windDir.x, 0.0, windDir.y));
    float3 jitteredWind = normalize(windVector3D + float3(windDir.y, 0.0, -windDir.x) * axisJitter);
    
    float3 upVector = float3(0.0, 1.0, 0.0);
    float3 bendAxis = normalize(cross(upVector, jitteredWind));
    
    // 6. Apply Rodrigues Rotation to GEOMETRY
    float3 localPos = finalWorldPos - instanceWorldPos;
    localPos = rotateVector(localPos, bendAxis, bendAngle);
    finalWorldPos = instanceWorldPos + localPos;
    
    // ============================================================
    // [NEW FIX] ROTATE NORMALS (解决叶尖不正常/发黑问题)
    // ============================================================
    // 我们必须把法线也绕着同样的轴转动，否则光照会觉得草是直的
    
    // 1. 获取原始 Billboard 法线 (指向 Z 轴，随 Billboard 旋转)
    float3 forwardLocal = float3(0.0, 0.0, 1.0);
    float3 initialNormal = (billboardRotation * float4(forwardLocal, 0.0)).xyz;
    
    // 2. [关键] 应用完全相同的风力旋转
    // 只有这样，法线才能正确反映弯曲后的几何体朝向
    float3 rotatedNormal = rotateVector(initialNormal, bendAxis, bendAngle);
    
    // 3. 吉卜力风格化 (Stylized Up-Vector Blend)
    // 混合一点 (0,1,0) 让光照更柔和，但基础必须是 rotatedNormal
    float3 stylizedNormal = normalize(rotatedNormal * 0.6 + float3(0.0, 1.0, 0.0) * 0.6);

    // ============================================================
    // [END] PHYSICS
    // ============================================================
    
    // ============================================================
    // [START] ADVANCED PHYSICS: CRUSH & BEND TRAMPLING EFFECT
    // ============================================================
    // Calculate distance from grass instance base to interactor
    float3 interactorPos = uniforms.interactorPos;
    float3 instancePos = instanceWorldPos; // Base position of this grass blade
    float3 toInteractor = instancePos - interactorPos;
    float dist = length(toInteractor);
    
    // Calculate influence factor: 1.0 at center (fully crushed), 0.0 at edge
    float pushRadius = uniforms.interactorRadius;
    // smoothstep(pushRadius, pushRadius * 0.4, dist) gives:
    // - 1.0 when dist <= pushRadius * 0.4 (center, fully affected)
    // - 0.0 when dist >= pushRadius (edge, unaffected)
    // - Smooth transition in between
    float influence = 1.0 - smoothstep(pushRadius * 0.4, pushRadius, dist);
    
    // Store influence for fragment shader (for shadow darkening)
    out.influence = influence;
    
    // Apply deformation only if there's any influence
    if (influence > 0.001) {
        // Calculate push direction (outward from interactor)
        float3 pushDir = normalize(toInteractor);
        // Handle edge case where toInteractor is zero
        if (length(pushDir) < 0.001) {
            pushDir = float3(1.0, 0.0, 0.0); // Default direction
        }
        
        // Get local position relative to instance base
        float3 localPos = finalWorldPos - instancePos;
        float originalHeight = localPos.y; // Store original height
        
        // ============================================================
        // DEFORMATION A: VERTICAL CRUSH (The Weight)
        // ============================================================
        // Grass under the capsule should be flattened
        // Squash height down to 10% at center, full height at edge
        float crushFactor = mix(1.0, 0.1, influence);
        localPos.y *= crushFactor;
        
        // ============================================================
        // DEFORMATION B: RADIAL BEND (The Curve)
        // ============================================================
        // Push grass outward, but apply exponentially based on height
        // Tip bends much more than root (simulates natural bending)
        // t = 1.0 at tip, 0.0 at root
        float bendFactor = influence * (t * t); // Quadratic: tip bends much more
        
        // Push tip outward strongly, root stays fixed
        float2 pushXZ = pushDir.xz * bendFactor * 2.0;
        localPos.x += pushXZ.x;
        localPos.z += pushXZ.y;
        
        // Update final world position
        finalWorldPos = instancePos + localPos;
        
        // Ensure grass doesn't go below ground level
        finalWorldPos.y = max(instancePos.y, finalWorldPos.y);
    } else {
        // No influence, set to 0.0
        out.influence = 0.0;
    }
    // ============================================================
    // [END] ADVANCED PHYSICS: CRUSH & BEND TRAMPLING EFFECT
    // ============================================================
    
    // Pass Output
    // 高光只响应正向强风 (mainSwell)，忽略回弹阶段
    out.windStrength = max(0.0, fluidWind); 

    float4 pos = uniforms.projectionMatrix * uniforms.viewMatrix * float4(finalWorldPos, 1.0);
    
    out.position = pos;
    out.texcoord = texcoord;
    out.normal = stylizedNormal; // 使用修正后的法线
    out.worldPos = finalWorldPos;
    out.instanceHash = hash(instanceID);
    
    // Determine if this blade should be yellow-green (withered) - 10% probability
    // Use a different hash seed to ensure independence from instanceHash
    float yellowHash = hash(instanceID * 7 + 1000);
    out.isYellow = (yellowHash > 0.9) ? 1.0 : 0.0;
    
    return out;
}

fragment float4 fragmentMain(
    RasterizerData in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    // Define a constexpr sampler inside the shader function
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    
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
    // 3. Per-Instance Color Variation: Lush Green Variations + Withered Yellow
    // ============================================================================
    // Use instance hash for per-blade distinctness (passed from vertex shader)
    float noise = in.instanceHash; // Returns 0.0 to 1.0
    
    // Create lighter green variation (fresh, not dead)
    float3 lighterGreen = float3(0.2, 0.7, 0.3); // Fresh, lighter green variation
    
    // Mix base gradient color with lighter variation - subtle mix for natural variation
    float3 variedColor = mix(gradientColor, lighterGreen, noise * 0.3); // Max 30% variation
    
    // Yellow-green withered color for ~10% of blades
    float3 dryColor = float3(0.85, 0.75, 0.3); // Ghibli-style yellow-green
    
    // Mix between normal green and yellow-green based on isYellow flag
    variedColor = mix(variedColor, dryColor, in.isYellow);
    
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
    // 6. "Wind Sheen" Effect (Smart Sheen - Avoids Over-Exposure)
    // ============================================================================
    // Smart Sheen Logic (Avoids white-out)
    float t_height = 1.0 - in.texcoord.y;
    
    // 1. Tip Mask: Only top 20%
    float tipMask = smoothstep(0.8, 1.0, t_height);
    
    // 2. Wind Mask: Only show sheen on wave peaks
    // fluidWind is 0..1. We only want sheen when wind > 0.6
    float windMask = smoothstep(0.6, 1.0, in.windStrength);
    
    // 3. Color & Mix
    float3 sheenColor = float3(0.9, 0.95, 0.8); // Pale Sunlit Yellow-Green
    float sheenIntensity = tipMask * windMask * 0.6; // Max 60% opacity
    
    float3 windTintedColor = mix(variedColor, sheenColor, sheenIntensity);
    
    // ============================================================================
    // 7. Final Color Composition
    // ============================================================================
    // FinalColor = (WindTintedColor * RandomVariation) * (HalfLambert + Ambient) + Specular
    float3 finalColor = windTintedColor * lighting + specularLight;
    
    // ============================================================================
    // 8. Stronger Ambient Occlusion (AO) at Roots
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
    
    // ============================================================================
    // 8.5. GROUNDING SHADOWS (Crushed Grass Darkening)
    // ============================================================================
    // Grass that is crushed (high influence) must be darker to simulate
    // self-shadowing and contact shadows from being flattened
    // Darken by 50% at the crush center (influence = 1.0)
    float crushShadow = mix(1.0, 0.5, in.influence);
    finalColor *= crushShadow;
    
    // ============================================================================
    // 9. FAKE BLOB SHADOW (Capsule Grounding)
    // ============================================================================
    // Calculate horizontal distance from grass to interactor (ignore height)
    float2 grassPosXZ = in.worldPos.xz;
    float2 interactorPosXZ = uniforms.interactorPos.xz;
    float shadowDist = distance(grassPosXZ, interactorPosXZ);
    
    // Shadow radius slightly larger than the collider for soft falloff
    float shadowRadius = uniforms.interactorRadius * 1.2;
    
    // Create soft gradient: 0.0 = center (dark), 1.0 = edge (bright)
    // smoothstep creates a smooth transition from dark center to bright edge
    float shadowFactor = smoothstep(0.0, shadowRadius, shadowDist);
    
    // Clamp minimum brightness so shadow doesn't get too dark (maintains visibility)
    // This simulates ambient light even in shadowed areas
    shadowFactor = saturate(shadowFactor + 0.4); // Min brightness 0.4 (40% of original)
    
    // Apply shadow darkening to grass color
    finalColor *= shadowFactor;
    
    // ============================================================================
    // 10. GHIBLI STYLE DISTANCE FOG
    // ============================================================================
    // Calculate distance from camera to this fragment
    float dist = length(in.worldPos - uniforms.cameraPosition);
    
    // Fog Parameters (Adjust these based on your scene scale)
    // Start: Fog begins at 15 meters (grass is clear close up)
    // End: Fog becomes fully opaque at 45 meters (hides the edge of the plane)
    float fogStart = 15.0;
    float fogEnd = 45.0;
    
    // Calculate Fog Factor (0.0 = No Fog, 1.0 = Full Fog)
    // smoothstep creates a soft, non-linear transition that looks more natural
    float fogFactor = smoothstep(fogStart, fogEnd, dist);
    
           // Fog Color: Needs to match Clear Color!
    // Ghibli Style: A nice, soft sky blue (not dull gray)
    float3 fogColor = float3(0.4, 0.6, 0.9); 
    
    // Mix the grass color with the fog color based on distance
    finalColor = mix(finalColor, fogColor, fogFactor);
    
    // Return final color with smoothed opacity for Alpha-to-Coverage
    return float4(finalColor, opacity);
}

// ============================================================================
// CAPSULE SHADERS (Interactor Visualization)
// ============================================================================

struct CapsuleRasterizerData {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
};

vertex CapsuleRasterizerData vertexCapsule(
    uint vertexID [[vertex_id]],
    constant Vertex *vertices [[buffer(BufferIndexMeshPositions)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    CapsuleRasterizerData out;
    
    // Get vertex position in local space
    float3 localPos = vertices[vertexID].position;
    
    // Translate to interactor position
    float3 worldPos = localPos + uniforms.interactorPos;
    
    // Transform to clip space
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * float4(worldPos, 1.0);
    out.worldPos = worldPos;
    
    // Pass normal in object space (no rotation applied, so object space = world space)
    // Normalize to ensure it's unit length after interpolation
    out.normal = normalize(vertices[vertexID].normal);
    
    return out;
}

fragment float4 fragmentCapsule(
    CapsuleRasterizerData in [[stage_in]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    // Blinn-Phong Lighting for Shiny Plastic Material
    
    // Normalize interpolated normal
    float3 normal = normalize(in.normal);
    
    // Hardcoded directional light from top-right
    float3 lightDir = normalize(float3(1.0, 2.0, 1.0));
    
    // Calculate view direction (from fragment to camera)
    float3 viewDir = normalize(uniforms.cameraPosition - in.worldPos);
    
    // Material properties
    float3 diffuseColor = float3(0.9, 0.9, 0.9); // Bright white/grey
    float specularStrength = 0.8; // High specular for glossy look
    float shininess = 64.0; // Sharp, glossy highlight
    
    // Ambient component
    float3 ambient = float3(0.2, 0.2, 0.2);
    
    // Diffuse component (Lambertian)
    float NdotL = max(0.0, dot(normal, lightDir));
    float3 diffuse = diffuseColor * NdotL;
    
    // Specular component (Blinn-Phong)
    float3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(0.0, dot(normal, halfDir));
    float specular = pow(NdotH, shininess) * specularStrength;
    float3 specularColor = float3(1.0, 1.0, 1.0) * specular;
    
    // Final color: Ambient + Diffuse + Specular
    float3 finalColor = ambient + diffuse + specularColor;
    
    return float4(finalColor, 1.0);
}
