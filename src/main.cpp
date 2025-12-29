#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <GLFW/glfw3.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "MetalLayerBridge.h"
#include "Renderer.hpp"

#include <iostream>
#include <chrono>

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    // Set GLFW to not use any client API
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    // Create a GLFW window (800x600)
    GLFWwindow* window = glfwCreateWindow(800, 600, "VegetationDemo", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    // Set cursor mode to disabled (captured) for first-person camera control
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    // Include "MetalLayerBridge.h" and call GetMetalLayerFromGLFW(window) to get the void* layer
    void* layerPtr = GetMetalLayerFromGLFW(window);
    
    // Cast that void* to CA::MetalLayer* (cpp wrapper)
    CA::MetalLayer* metalLayer = static_cast<CA::MetalLayer*>(layerPtr);
    
    // Create MTL::Device and Renderer
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device) {
        std::cerr << "Failed to create Metal device" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    
    Renderer* renderer = new Renderer(device, metalLayer);
    
    // Set up resize callback to update MSAA textures when window is resized
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        // Get renderer from window user pointer (we'll set it below)
        Renderer* r = static_cast<Renderer*>(glfwGetWindowUserPointer(win));
        if (r) {
            r->resize(width, height);
        }
    });
    glfwSetWindowUserPointer(window, renderer);
    
    // Initialize timing
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Calculate deltaTime
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // Update input and camera
        renderer->update(window, deltaTime);
        
        // Render
        renderer->draw();
        
        // Poll events
        glfwPollEvents();
    }
    
    // Cleanup
    delete renderer;
    device->release();
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}
