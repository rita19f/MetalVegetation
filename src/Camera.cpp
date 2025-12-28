#include "Camera.hpp"
#include <algorithm>

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : position(position)
    , worldUp(up)
    , yaw(yaw)
    , pitch(pitch)
    , front(glm::vec3(0.0f, 0.0f, -1.0f))
    , movementSpeed(2.5f)
    , mouseSensitivity(0.1f)
{
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix()
{
    // Use glm::lookAt(position, position + front, up)
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix(float width, float height)
{
    // Use glm::perspective(glm::radians(45.0f), width / height, 0.1f, 100.0f)
    return glm::perspective(glm::radians(45.0f), width / height, 0.1f, 100.0f);
}

void Camera::processKeyboard(int key, float deltaTime)
{
    // Standard WASD movement. Update position based on front and right vectors.
    float velocity = movementSpeed * deltaTime;
    
    if (key == 'W' || key == 'w') {
        // Move forward
        position += front * velocity;
    }
    if (key == 'S' || key == 's') {
        // Move backward
        position -= front * velocity;
    }
    if (key == 'A' || key == 'a') {
        // Move left
        position -= right * velocity;
    }
    if (key == 'D' || key == 'd') {
        // Move right
        position += right * velocity;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset)
{
    // Update yaw and pitch
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;
    
    yaw += xoffset;
    pitch += yoffset;
    
    // Constrain pitch between -89.0f and 89.0f
    if (pitch > 89.0f) {
        pitch = 89.0f;
    }
    if (pitch < -89.0f) {
        pitch = -89.0f;
    }
    
    // Call updateCameraVectors()
    updateCameraVectors();
}

void Camera::updateCameraVectors()
{
    // Calculate new front, right, and up vectors using Euler angles (standard implementation)
    
    // Calculate the new front vector
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    
    // Calculate the right vector (normalize the cross product of front and worldUp)
    right = glm::normalize(glm::cross(front, worldUp));
    
    // Calculate the up vector (normalize the cross product of right and front)
    up = glm::normalize(glm::cross(right, front));
}

