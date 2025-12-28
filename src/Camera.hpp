#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch);
    
    glm::mat4 getViewMatrix();
    glm::mat4 getProjectionMatrix(float width, float height);
    
    // 键盘输入 (WASD)
    void processKeyboard(int key, float deltaTime);
    // 鼠标移动 (视角)
    void processMouseMovement(float xoffset, float yoffset);


    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    
    float yaw;
    float pitch;
    float movementSpeed = 2.5f;
    float mouseSensitivity = 0.1f;
    
private:
    void updateCameraVectors();
};