#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// Compute shader to update trample map
// Implements decay + hard-edge ball stamp

kernel void updateTrampleMap(
    texture2d<float, access::read> inputTexture [[texture(0)]],
    texture2d<float, access::write> outputTexture [[texture(1)]],
    constant Uniforms &uniforms [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    // Check bounds
    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) {
        return;
    }
    
    // Read current trample value
    float2 uv = float2(gid) / float2(outputTexture.get_width(), outputTexture.get_height());
    float currentTrample = inputTexture.read(gid).r;
    
    // ============================================================
    // STEP 1: DECAY
    // ============================================================
    // Decay every frame: trample = max(0, trample - decayRate * dt)
    float decayedTrample = max(0.0, currentTrample - uniforms.trampleDecayRate * uniforms.dt);
    
    // ============================================================
    // STEP 2: STAMP BALL FOOTPRINT
    // ============================================================
    // Map pixel UV to world XZ position
    float2 worldXZ = mix(uniforms.groundMinXZ, uniforms.groundMaxXZ, uv);
    
    // Calculate distance from world position to ball center (on XZ plane)
    float2 ballCenterXZ = uniforms.ballWorldPos.xz;
    float2 P = worldXZ;
    float2 toBall = P - ballCenterXZ;
    float dist = length(toBall);
    
    // Hard edge stamp: if d < radius, stamp = 1, else stamp = 0
    float stamp = (dist < uniforms.ballRadius) ? 1.0 : 0.0;
    
    // Update: trample = max(trample, stamp)
    float newTrample = max(decayedTrample, stamp);
    
    // Write output
    outputTexture.write(float4(newTrample, 0.0, 0.0, 1.0), gid);
}

