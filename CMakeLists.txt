cmake_minimum_required(VERSION 3.15)
project(VegetationDemo LANGUAGES C CXX OBJC OBJCXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories for header-only libraries
include_directories(
    ${CMAKE_SOURCE_DIR}/external/metal-cpp
    ${CMAKE_SOURCE_DIR}/external/glm
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/tinyobjloader
)

# GLFW - Build as static library
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_DOCS OFF)
option(GLFW_INSTALL OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

# macOS frameworks
find_library(METAL_FRAMEWORK Metal)
find_library(FOUNDATION_FRAMEWORK Foundation)
find_library(QUARTZCORE_FRAMEWORK QuartzCore)
find_library(COCOA_FRAMEWORK Cocoa)
find_library(IOKIT_FRAMEWORK IOKit)


# ImGui - Build as static library
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)

# ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
)

add_library(imgui STATIC ${IMGUI_SOURCES})
set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC 
    glfw
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
)

# Main executable
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.mm" "${CMAKE_SOURCE_DIR}/src/*.m")

if(SOURCES)
    add_executable(VegetationDemo ${SOURCES})
else()
    # Create a placeholder if no sources exist yet
    message(WARNING "No source files found in src/. Creating empty executable.")
    add_executable(VegetationDemo ${CMAKE_SOURCE_DIR}/src/main.cpp)
    # Create a minimal main.cpp if it doesn't exist
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
        file(WRITE ${CMAKE_SOURCE_DIR}/src/main.cpp
            "#include <iostream>\n"
            "int main() {\n"
            "    std::cout << \"VegetationDemo\" << std::endl;\n"
            "    return 0;\n"
            "}\n"
        )
    endif()
endif()

target_include_directories(VegetationDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/metal-cpp
    ${CMAKE_SOURCE_DIR}/external/glm
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/tinyobjloader
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(VegetationDemo PRIVATE
    imgui
    glfw
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${COCOA_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
)

# Set macOS deployment target
if(APPLE)
    set_target_properties(VegetationDemo PROPERTIES
        MACOSX_BUNDLE FALSE
        MACOSX_RPATH ON
    )
    # Set minimum macOS version if needed
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    endif()
    
    # Compile Metal shaders
    set(METAL_SHADER_SOURCE_GRASS ${CMAKE_SOURCE_DIR}/src/Shaders.metal)
    set(METAL_SHADER_SOURCE_GROUND ${CMAKE_SOURCE_DIR}/src/GroundShaders.metal)
    set(METAL_SHADER_IR_GRASS ${CMAKE_BINARY_DIR}/Shaders.air)
    set(METAL_SHADER_IR_GROUND ${CMAKE_BINARY_DIR}/GroundShaders.air)
    set(METAL_SHADER_LIB ${CMAKE_BINARY_DIR}/default.metallib)
    
    # Compile Grass Metal shader to AIR (Apple Intermediate Representation)
    add_custom_command(
        OUTPUT ${METAL_SHADER_IR_GRASS}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SHADER_SOURCE_GRASS} -o ${METAL_SHADER_IR_GRASS} -I${CMAKE_SOURCE_DIR}/src
        DEPENDS ${METAL_SHADER_SOURCE_GRASS} ${CMAKE_SOURCE_DIR}/src/ShaderTypes.h
        COMMENT "Compiling Grass Metal shader to AIR"
    )
    
    # Compile Ground Metal shader to AIR (Apple Intermediate Representation)
    add_custom_command(
        OUTPUT ${METAL_SHADER_IR_GROUND}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SHADER_SOURCE_GROUND} -o ${METAL_SHADER_IR_GROUND} -I${CMAKE_SOURCE_DIR}/src
        DEPENDS ${METAL_SHADER_SOURCE_GROUND} ${CMAKE_SOURCE_DIR}/src/ShaderTypes.h
        COMMENT "Compiling Ground Metal shader to AIR"
    )
    
    # Link AIR files to metallib
    add_custom_command(
        OUTPUT ${METAL_SHADER_LIB}
        COMMAND xcrun -sdk macosx metallib ${METAL_SHADER_IR_GRASS} ${METAL_SHADER_IR_GROUND} -o ${METAL_SHADER_LIB}
        DEPENDS ${METAL_SHADER_IR_GRASS} ${METAL_SHADER_IR_GROUND}
        COMMENT "Linking Metal shader library"
    )
    
    # Add dependency and copy metallib to output directory
    add_custom_target(MetalShaders ALL DEPENDS ${METAL_SHADER_LIB})
    add_dependencies(VegetationDemo MetalShaders)
    
    add_custom_command(
        TARGET VegetationDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${METAL_SHADER_LIB} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/default.metallib
        COMMENT "Copying Metal shader library to output directory"
    )
endif()

file(COPY "${CMAKE_SOURCE_DIR}/assets" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")