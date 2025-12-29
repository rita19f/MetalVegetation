#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <QuartzCore/QuartzCore.h>
#include <Cocoa/Cocoa.h>
#include <Metal/Metal.h>

#include "MetalLayerBridge.h"

void* GetMetalLayerFromGLFW(void* glfwWindow) {
    // Cast the input void* to GLFWwindow*
    GLFWwindow* window = static_cast<GLFWwindow*>(glfwWindow);
    
    // Get the NSWindow using glfwGetCocoaWindow
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    
    // Get the contentView of the window
    NSView* view = [nsWindow contentView];
    
    // Create a CAMetalLayer
    CAMetalLayer* layer = [CAMetalLayer layer];
    
    // Set the layer's device to the system default device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    [layer setDevice:device];
    
    // Set the layer's pixelFormat to MTLPixelFormatBGRA8Unorm
    [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
    
    // Enable 4x MSAA on the Metal layer
    [layer setDrawableSize:[view bounds].size];
    // Note: CAMetalLayer doesn't have a direct sampleCount property
    // MSAA is handled through render pipeline and textures
    
    // Set view.layer = layer and view.wantsLayer = YES
    [view setLayer:layer];
    [view setWantsLayer:YES];
    
    // Return the layer object cast to void*
    return (void*)layer;
}

