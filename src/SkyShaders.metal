#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// Fullscreen triangle vertex shader (no vertex buffer needed)
struct SkyRasterizerData {
    float4 position [[position]];
    float2 uv;
};

vertex SkyRasterizerData vertexSkyFullscreen(uint vertexID [[vertex_id]]) {
    SkyRasterizerData out;
    
    // Fullscreen triangle using vertex_id (0, 1, 2)
    // Generate positions that cover the entire screen
    float2 pos;
    if (vertexID == 0) {
        pos = float2(-1.0, -1.0);  // Bottom-left
    } else if (vertexID == 1) {
        pos = float2(3.0, -1.0);    // Bottom-right (extended)
    } else {
        pos = float2(-1.0, 3.0);   // Top-left (extended)
    }
    
    out.position = float4(pos, 0.0, 1.0);
    
    // Generate UV coordinates (0..1, y=0 at top, y=1 at bottom for gradient)
    if (vertexID == 0) {
        out.uv = float2(0.0, 1.0);  // Bottom-left (y=1 at bottom)
    } else if (vertexID == 1) {
        out.uv = float2(2.0, 1.0);  // Bottom-right (y=1 at bottom)
    } else {
        out.uv = float2(0.0, 0.0); // Top-left (y=0 at top)
    }
    
    return out;
}

fragment float4 fragmentSkyGradient(SkyRasterizerData in [[stage_in]]) {
    float2 uv = in.uv;

    // Fix vertical direction:
    float y = 1.0 - saturate(uv.y); // 0 = horizon, 1 = top

    // More saturated, clearer "Ghibli-ish" palette (not washed out)
    float3 topColor     = float3(0.16, 0.38, 0.82);
    float3 horizonColor = float3(0.72, 0.90, 0.98);  // keep
    float3 hazeColor    = float3(0.80, 0.90, 0.98);

    // Base gradient: keep contrast
    float t = smoothstep(0.0, 1.0, y);
    float3 col = mix(horizonColor, topColor, t);

    // Haze: strongest near horizon, dies quickly upward (avoid whole-sky whitening)
    float haze = exp2(-y * 10.0);
    col = mix(col, hazeColor, haze * 0.25);

    return float4(saturate(col), 1.0);
}

