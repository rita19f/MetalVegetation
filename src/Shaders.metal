#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// Vertex shader output (rasterizer interpolated data)
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
    
// ---------------------------------------------------------
// WIND PHYSICS & ROTATION LOGIC
// ---------------------------------------------------------
    
    // 1. Idle Chaos (breathing effect)
    float tTime = uniforms.time;
    float seed = float(instanceID);
    float idleFreq = 2.0 + hash(instanceID) * 1.5; 
    float idlePhase = seed * 13.0;
    // Slightly increase idle amplitude to ensure motion even when still
    float idleStrength = sin(tTime * idleFreq + idlePhase) * 0.08; 
    
    // 2. Roystan-Style Fluid Wind (main wind wave)
    float2 windDir = normalize(float2(1.0, 0.5)); 
    
    // A. Main Swell (large wave)
    // Key fix: remove +0.5 forced positive offset, allow negative rebound
    float swellPhase = dot(instanceWorldPos.xz, windDir);
    float rawSine = sin(swellPhase * 0.05 - tTime * 1.0); // -1 to 1
    
    // Map to [-0.3, 1.0] range
    // 1.0 (large rightward tilt) -> 0.0 (upright) -> -0.3 (leftward inertia rebound)
    // This creates a "swaying back and forth" feeling, not just "nodding"
    float mainSwell = rawSine * 0.65 + 0.35;
    
    // B. Turbulence (detail noise)
    float2 noiseUV = instanceWorldPos.xz * 0.2 - windDir * tTime * 2.0;
    float turbulence = noise2D(noiseUV); // 0..1
    // Allow noise to also perturb slightly leftward (-0.2 to 0.8)
    turbulence = turbulence - 0.2;
    
    // C. Composite (blend)
    float fluidWind = mix(mainSwell, turbulence, 0.3);
    
    // 3. Combine Forces
    float totalWindStrength = fluidWind + idleStrength;
    
    // 4. Calculate Rotation Angle (Linear Rigid)
    // Increase sensitivity slightly (1.0 -> 1.2) to make swaying more noticeable
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
    
    // ---------------------------------------------------------
    // ROTATE NORMALS (fixes tip darkening issue)
    // ---------------------------------------------------------
    // We must rotate normals around the same axis, otherwise lighting thinks grass is straight
    
    // 1. Get original Billboard normal (points to Z axis, rotates with Billboard)
    float3 forwardLocal = float3(0.0, 0.0, 1.0);
    float3 initialNormal = (billboardRotation * float4(forwardLocal, 0.0)).xyz;
    
    // 2. Apply the same wind rotation to normals
    // Only then can normals correctly reflect the bent geometry orientation
    float3 rotatedNormal = rotateVector(initialNormal, bendAxis, bendAngle);
    
    // 3. Blend a bit of (0,1,0) for softer lighting, but base must be rotatedNormal
    float3 stylizedNormal = normalize(rotatedNormal * 0.6 + float3(0.0, 1.0, 0.0) * 0.6);

    // ---------------------------------------------------------
    // SOFT FLATTEN RING (Ghibli-like compression, no sideways bending)
    // ---------------------------------------------------------
    // Compute distance to the ball in XZ
    float2 P = finalWorldPos.xz;
    float2 ballCenterXZ = uniforms.ballWorldPos.xz;
    float d = length(P - ballCenterXZ);
    
    // Ring mask (narrow, smooth falloff)
    float ring = smoothstep(uniforms.ballRadius + uniforms.flattenBandWidth, 
                           uniforms.ballRadius, d);
    
    // Height weighting (tip > base)
    // Use existing t (0=Root, 1=Tip) for height factor
    float heightW = saturate(t); // Already 0-1
    heightW = heightW * heightW; // Quadratic: stronger tip weighting
    
    // Apply flatten by compressing towards root
    if (ring > 0.001) {
        // Compute root world position for this blade
        float3 rootWorldPos = instanceWorldPos;
        
        // Apply flatten compression (no sideways bending)
        float flatten = ring * heightW * uniforms.flattenStrength;
        finalWorldPos = rootWorldPos + (finalWorldPos - rootWorldPos) * (1.0 - flatten);
    }
    
    // ---------------------------------------------------------
    // Trample map system now handles grass clipping in fragment shader
    // ---------------------------------------------------------
    // Set influence to 0.0 (no longer used for vertex deformation)
    out.influence = 0.0;
    
    // Pass Output
    // Specular only responds to forward strong wind (mainSwell), ignore rebound phase
    out.windStrength = max(0.0, fluidWind); 

    float4 pos = uniforms.projectionMatrix * uniforms.viewMatrix * float4(finalWorldPos, 1.0);
    
    out.position = pos;
    out.texcoord = texcoord;
    out.normal = stylizedNormal; // Use corrected normal
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
    texture2d<float> colorTexture [[texture(TextureIndexGrass)]],
    texture2d<float> trampleMap [[texture(TextureIndexTrampleMap)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    // Define a constexpr sampler inside the shader function
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    
    // ---------------------------------------------------------
    // 0. TRAMPLE MAP: Hard clip grass where trampled
    // ---------------------------------------------------------
    // Map world position to trample map UV
    float2 worldXZ = in.worldPos.xz;
    float2 trampleUV = (worldXZ - uniforms.groundMinXZ) / (uniforms.groundMaxXZ - uniforms.groundMinXZ);
    
    // Sample trample map
    constexpr sampler trampleSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    float trample = trampleMap.sample(trampleSampler, trampleUV).r;
    
    // Hard clip: if trample > 0.5, discard fragment (grass disappears)
    const float killThreshold = 0.5;
    if (trample > killThreshold) {
        discard_fragment();
    }
    
    // ---------------------------------------------------------
    // 1. Analytic Antialiasing: Smooth alpha edges using derivatives
    // ---------------------------------------------------------
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
    
    // ---------------------------------------------------------
    // 2. Procedural Coloring: Generate vertical gradient RGB (ignore texture RGB)
    // ---------------------------------------------------------
    // Calculate height factor: t=0.0 at root (bottom), t=1.0 at tip (top)
    // Assuming texture Y=1 is bottom and Y=0 is top (common in Metal/stb_image)
    // We want t=0 at bottom, t=1 at top for the gradient
    float t = 1.0 - in.texcoord.y; // t=0 at bottom (texcoord.y=1), t=1 at top (texcoord.y=0)
    
    // Define Colors (Lush Green Style)
    float3 rootColor = float3(0.05, 0.2, 0.05);   // Very Dark Green (almost black) at roots
    float3 midColor  = float3(0.1, 0.5, 0.1);     // Healthy Base Green in middle
    float3 tipColor  = float3(0.30, 0.75, 0.25);  // Softer green at tips (reduced harsh yellow-green)
    
    // Multi-stop gradient for better look
    float3 gradientColor;
    if (t < 0.5) {
        // Bottom half: root to mid
        gradientColor = mix(rootColor, midColor, t * 2.0);
    } else {
        // Top half: mid to tip
        gradientColor = mix(midColor, tipColor, (t - 0.5) * 2.0);
    }
    
    // ---------------------------------------------------------
    // 3. Per-Instance Color Variation: Lush Green Variations + Withered Yellow
    // ---------------------------------------------------------
    // Use instance hash for per-blade distinctness (passed from vertex shader)
    float noise = in.instanceHash; // Returns 0.0 to 1.0
    
    // Create lighter green variation (fresh, not dead)
    float3 lighterGreen = float3(0.2, 0.7, 0.3); // Fresh, lighter green variation
    
    // Mix base gradient color with lighter variation - subtle mix for natural variation
    float3 variedColor = mix(gradientColor, lighterGreen, noise * 0.3); // Max 30% variation
    
    // Yellow-green withered color for ~10% of blades (softer pastel, less saturated)
    float3 dryColor = float3(0.70, 0.78, 0.45); // Softer pastel yellow-green (less saturated)
    
    // Apply dryness more near tips than roots (so roots remain grounded)
    float tipFactor = 1.0 - in.texcoord.y;      // 0 bottom, 1 top
    float dryAmount = in.isYellow * smoothstep(0.3, 1.0, tipFactor) * 0.45; // max 45% blend, mostly at tips
    variedColor = mix(variedColor, dryColor, dryAmount);
    
    // ---------------------------------------------------------
    // 4. Wrap Diffuse + Colored Ambient (Ghibli-style lighting)
    // ---------------------------------------------------------
    float3 normal = normalize(in.normal);
    float3 sunDir = normalize(uniforms.sunDirection);
    
    // Wrap diffuse: softer than half-lambert, avoids "everything gets bright" look
    float NdotL = dot(normal, sunDir);
    float wrap = 0.45; // wrap amount (0.3~0.6). Higher = softer.
    float wrapDiffuse = saturate((NdotL + wrap) / (1.0 + wrap));
    
    // Use a slightly colored ambient (Ghibli-ish: cool shadows, warm sun)
    float3 ambientColor = float3(0.22, 0.27, 0.30); // cooler sky-like fill
    float ambientStrength = 0.50;                   // lift midtones (cleaner look)
    
    // Combine diffuse lighting
    float3 lighting = uniforms.sunColor * wrapDiffuse + ambientColor * ambientStrength;
    
    // ---------------------------------------------------------
    // 5. Broad Subtle Specular Highlight (Soft, warm-neutral)
    // ---------------------------------------------------------
    // Calculate view direction
    float3 viewDir = normalize(uniforms.cameraPosition - in.worldPos);
    
    // Blinn-Phong half vector
    float3 halfDir = normalize(sunDir + viewDir);
    
    // Broad highlight: low power, low strength (avoids tight yellow stripes)
    float specularStrength = 0.035; // much smaller
    float specularPower = 12.0;     // much broader
    float spec = pow(max(0.0, dot(normal, halfDir)), specularPower) * specularStrength;
    
    // Keep it mostly near tips, but softer (quadratic weighting)
    // Reuse tipFactor defined earlier in Section 3
    spec *= (tipFactor * tipFactor); // quadratic tip weighting
    
    // Warm-neutral specular tint (avoid yellow)
    float3 specTint = float3(1.0, 1.0, 0.97);
    float3 specularLight = specTint * spec;
    
    // ---------------------------------------------------------
    // 6. Wind Sheen Effect (Brightness lift, not yellow tint)
    // ---------------------------------------------------------
    float t_height = 1.0 - in.texcoord.y;
    
    // Tip mask (top 20%)
    float tipMask = smoothstep(0.80, 1.00, t_height);
    
    // Wind mask (only on peaks)
    float windMask = smoothstep(0.65, 1.00, in.windStrength);
    
    // Small intensity only (gentle brightness lift, not strong yellow mix)
    float sheen = tipMask * windMask * 0.18; // max 18%
    
    // Lift toward warm-white (not yellow-green)
    float3 sheenTarget = float3(0.98, 0.99, 0.95);
    float3 windTintedColor = mix(variedColor, sheenTarget, sheen);
    
    // ---------------------------------------------------------
    // 7. Final Color Composition
    // ---------------------------------------------------------
    // FinalColor = (WindTintedColor * RandomVariation) * (HalfLambert + Ambient) + Specular
    float3 finalColor = windTintedColor * lighting + specularLight;
    
    // LOW-FREQUENCY WORLD COLOR VARIATION (avoid "one-pot green")
    // Very low frequency noise in world space to introduce subtle warm/cool variation.
    // Keep it subtle to avoid "dirty" look.
    float nLow = noise2D(in.worldPos.xz * 0.03);  // very low frequency
    nLow = nLow * 2.0 - 1.0;                      // -1..1
    
    float tVar = 0.5 + 0.5 * nLow;                // 0..1
    float3 coolTint = float3(0.92, 0.98, 1.05);   // slightly bluish (cool shadow feel)
    float3 warmTint = float3(1.05, 1.02, 0.95);   // slightly warm (sun-kissed feel)
    
    float3 tint = mix(coolTint, warmTint, tVar);
    // Max 6% tint influence
    finalColor *= mix(float3(1.0), tint, 0.06);
    
    // ---------------------------------------------------------
    // 8. Stronger Ambient Occlusion (AO) at Roots
    // ---------------------------------------------------------
    // Strictly enforce darker roots to blend with dark green ground
    // t=0.0 at bottom (root), t=1.0 at top (tip)
    // We want darkening ONLY at the bottom 40%
    // smoothstep(0.0, 0.4, t) gives 0.0 at t=0 (root), 1.0 at t=0.4 (transition point)
    float occlusion = smoothstep(0.0, 0.4, t);
    
    // Apply intensity: 0.45 brightness at root (softer dark), 1.0 brightness at tip (bright)
    float aoFactor = 0.45 + 0.55 * occlusion; // softer root darkening (less muddy)
    
    // Apply AO to the color: roots are shadow-dark, tips are fully lit
    finalColor *= aoFactor;
    
    // ---------------------------------------------------------
    // TIP TRANSLUCENCY / BACKLIGHT (Soft leaf-fiber feel, very subtle)
    // ---------------------------------------------------------
    // Only affects tips and only when backlit relative to the sun direction.
    // Keep intensity low to avoid glowing/overexposure.
    float3 normalTrans = normalize(in.normal);
    float3 sunDirTrans = normalize(uniforms.sunDirection);
    float NdotLTrans = dot(normalTrans, sunDirTrans);
    
    float tipMaskTrans = smoothstep(0.60, 1.00, tipFactor);      // only upper portion
    float backlit = smoothstep(0.0, 0.60, -NdotLTrans);          // 0..1 when back-facing
    float trans = backlit * tipMaskTrans * 0.12;                 // cap ~12%
    
    float3 transColor = float3(0.90, 1.00, 0.85);                // slightly warm green
    finalColor = mix(finalColor, finalColor * transColor, trans);
    
    // ---------------------------------------------------------
    // CONTACT SHADOW (Subtle darkening near ball)
    // ---------------------------------------------------------
    // Compute distance (same as vertex shader)
    float2 P = in.worldPos.xz;
    float2 ballCenterXZ = uniforms.ballWorldPos.xz;
    float d = length(P - ballCenterXZ);
    
    // Contact shadow mask (localized, smooth falloff)
    float shadow = smoothstep(uniforms.contactShadowRadius, 0.0, d);
    shadow *= uniforms.contactShadowStrength;
    
    // Apply contact shadow
    finalColor.rgb *= (1.0 - shadow);
    
    // ---------------------------------------------------------
    // 9. FAKE BLOB SHADOW (Ball Grounding)
    // ---------------------------------------------------------
    // Calculate horizontal distance from grass to ball (ignore height)
    float2 grassPosXZ = in.worldPos.xz;
    float2 ballPosXZ = uniforms.ballWorldPos.xz;
    float shadowDist = distance(grassPosXZ, ballPosXZ);
    
    // Shadow radius slightly larger than the ball radius for soft falloff
    float shadowRadius = uniforms.ballRadius * 1.2;
    
    // Create soft gradient: 0.0 = center (dark), 1.0 = edge (bright)
    // smoothstep creates a smooth transition from dark center to bright edge
    float shadowFactor = smoothstep(0.0, shadowRadius, shadowDist);
    
    // Clamp minimum brightness so shadow doesn't get too dark (maintains visibility)
    // This simulates ambient light even in shadowed areas
    shadowFactor = saturate(shadowFactor + 0.4); // Min brightness 0.4 (40% of original)
    
    // Apply shadow darkening to grass color
    finalColor *= shadowFactor;
    
    // ---------------------------------------------------------
    // DISTANCE FOG (Before tone mapping for subtle atmospheric perspective)
    // ---------------------------------------------------------
    // Calculate distance from camera to this fragment
    float dist = length(in.worldPos - uniforms.cameraPosition);
    
    // Fog Parameters (Gentler atmospheric perspective)
    // Start: Fog begins at 12 meters (grass is clear close up)
    // End: Fog becomes fully opaque at 40 meters (hides the edge of the plane)
    float fogStart = 12.0;
    float fogEnd = 40.0;
    
    // Calculate Fog Factor (0.0 = No Fog, 1.0 = Full Fog)
    // Gentler exponential curve with cap to avoid full overwrite
    float fogLin = saturate((dist - fogStart) / (fogEnd - fogStart));
    float fogFactor = 1.0 - exp(-fogLin * 1.2);
    fogFactor = min(fogFactor, 0.75); // never fully overwrite the grass
    
    // Make fog less strong on blade tips (keep silhouette detail)
    fogFactor *= mix(1.0, 0.75, tipFactor);
    
    // Fog Color: Match background/clear color to remove horizon seams
    float3 fogColor = float3(0.40, 0.60, 0.90);
    
    // Mix the grass color with the fog color based on distance (before tone mapping)
    finalColor = mix(finalColor, fogColor, fogFactor);
    
    // ---------------------------------------------------------
    // EXPOSURE + TONE MAPPING (Lift midtones, keep highlights controlled)
    // ---------------------------------------------------------
    // Lift exposure slightly for a cleaner, fresher look.
    // Reinhard will still compress highlights to avoid blow-out.
    finalColor *= 1.12;
    
    // Simple Reinhard tone map per-channel
    finalColor = finalColor / (finalColor + float3(1.0));
    
    // ============================================================================
    // DEBUG: Visualize trample map (when showTrampleMap > 0.5)
    // ============================================================================
    if (uniforms.showTrampleMap > 0.5) {
        // Tint grass by trample value: red where trampled, normal color elsewhere
        float3 trampleTint = mix(float3(1.0, 1.0, 1.0), float3(1.0, 0.0, 0.0), trample);
        finalColor *= trampleTint;
    }
    
    // Return final color with smoothed opacity for Alpha-to-Coverage
    return float4(finalColor, opacity);
}

// ---------------------------------------------------------
// BALL SHADERS (Interactor Visualization)
// ---------------------------------------------------------

struct BallRasterizerData {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
};

vertex BallRasterizerData vertexBall(
    uint vertexID [[vertex_id]],
    constant Vertex *vertices [[buffer(BufferIndexMeshPositions)]],
    constant Uniforms &uniforms [[buffer(BufferIndexUniforms)]]
) {
    BallRasterizerData out;
    
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

fragment float4 fragmentBall(
    BallRasterizerData in [[stage_in]],
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
