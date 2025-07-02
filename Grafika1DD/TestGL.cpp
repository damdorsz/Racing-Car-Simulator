#define _USE_MATH_DEFINES
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// Shader sources
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform float lightIntensity;
uniform bool useTexture;
uniform sampler2D ourTexture;

// Nowe uniformy
uniform vec3 emissiveColor;
uniform vec3 spotDir;
uniform float spotCutOff;

void main() {
    vec3 baseColor = useTexture ? texture(ourTexture, TexCoord).rgb : objectColor;

    // ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;

    // diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    // spotlight factor
    float theta = dot(lightDir, normalize(-spotDir));
    float intensitySpot = (theta > spotCutOff) ? pow(theta, 4.0) : 0.0;

    float diff = max(dot(norm, lightDir), 0.0) * intensitySpot;
    vec3 diffuse = diff * lightColor;

    // specular
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32) * intensitySpot;
    vec3 specular = specularStrength * spec * lightColor;

    // final
    vec3 result = (ambient + diffuse + specular) * baseColor * lightIntensity
                + emissiveColor;
    FragColor = vec4(result, 1.0);
}
)";

// Global variables
GLFWwindow* window;
unsigned int shaderProgram;
unsigned int VBO, VAO, EBO;
int SCR_WIDTH = 1200;
int SCR_HEIGHT = 800;

// Camera system
enum CameraMode { CHASE, COCKPIT, SIDE, ORBITAL, FREECAM };
CameraMode currentCamera = CHASE;
glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, 10.0f);
glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
float cameraAngle = 0.0f;
float orbitalDirection = 1.0f;

// Car physics
glm::vec3 carPos = glm::vec3(0.0f, 0.5f, 0.0f);
float carRotation = 0.0f;
float carSpeed = 0.0f;
float wheelRotation = 0.0f;
float steerAngle = 0.0f;

// Environment
bool isNight = false;
float timeOfDay = 0.5f; // 0 = night, 1 = day
bool headlightsOn = false;
float trackRotation = 0.0f;
float treeSize = 1.0f;
glm::vec3 treeColor = glm::vec3(0.2f, 0.8f, 0.2f);
bool treeShapeIsRound = false;

// Input
bool keys[1024];
double lastX = SCR_WIDTH / 2.0;
double lastY = SCR_HEIGHT / 2.0;
bool firstMouse = true;
float mouseYaw = -90.0f;
float mousePitch = 0.0f;

// Texture & mouse
unsigned int textureGround, textureTrack, textureCar, textureBuilding;
float mouseSensitivity = 0.1f;
bool mouseControlEnabled = false;
float cameraDistance = 12.0f;  // dystans kamery od obiektu
float cameraPanX = 0.0f;       // przesuniêcie w poziomie
float cameraPanY = 0.0f;       // przesuniêcie w pionie

float freecamDistance = 12.0f;
float freecamYaw = -90.0f;
float freecamPitch = 0.0f;
float freecamPanX = 0.0f;
float freecamPanY = 0.0f;

bool leftMousePressed = false;
bool rightMousePressed = false;

void setUniforms(const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& objectColor,
    bool useTexture = false,
    const glm::vec3& emissiveColor = glm::vec3(0.0f));

std::vector<float> generateCone(int segments = 32) {
    std::vector<float> data;
    // wierzcho³ek (szczyt sto¿ka)
    glm::vec3 apex(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * M_PI * i / segments;
        float a1 = 2.0f * M_PI * (i + 1) / segments;
        float x0 = cos(a0), z0 = sin(a0);
        float x1 = cos(a1), z1 = sin(a1);
        // normal dla boku
        glm::vec3 v0(x0, 0.0f, z0), v1(x1, 0.0f, z1);
        glm::vec3 edge1 = v0 - apex, edge2 = v1 - apex;
        glm::vec3 normal = normalize(cross(edge2, edge1));
        // trójk¹t bok
        data.insert(data.end(),
            { apex.x,apex.y,apex.z, normal.x,normal.y,normal.z, 0.5f,1.0f,
              x0,0.0f,z0,      normal.x,normal.y,normal.z, 0.0f,0.0f,
              x1,0.0f,z1,      normal.x,normal.y,normal.z, 1.0f,0.0f });
        // podstawa (trójk¹t fan)
        glm::vec3 baseNormal(0.0f, -1.0f, 0.0f);
        data.insert(data.end(),
            { 0.0f,0.0f,0.0f, baseNormal.x,baseNormal.y,baseNormal.z, 0.5f,0.5f,
              x1,0.0f,z1,      baseNormal.x,baseNormal.y,baseNormal.z, (x1 + 1) * 0.5f,(z1 + 1) * 0.5f,
              x0,0.0f,z0,      baseNormal.x,baseNormal.y,baseNormal.z, (x0 + 1) * 0.5f,(z0 + 1) * 0.5f });
    }
    return data;
}

// 2. RENDEROWANIE STO¯KA
void renderCone(const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& color,
    int segments,
    const glm::vec3& emissiveColor)
{
    static unsigned int coneVAO = 0, coneVBO = 0;
    if (coneVAO == 0) {
        auto verts = generateCone(segments);
        glGenVertexArrays(1, &coneVAO);
        glGenBuffers(1, &coneVBO);

        glBindVertexArray(coneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, coneVBO);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(float),
            verts.data(),
            GL_STATIC_DRAW);

        // aPos
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // aNormal
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
            8 * sizeof(float),
            (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // aTexCoord
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
            8 * sizeof(float),
            (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    // Przekazujemy color oraz emotyColor do shaderów
    setUniforms(model, view, projection,
        color,
        false,
        emissiveColor);

    glBindVertexArray(coneVAO);
    int vertexCount = segments * 6; // 2 trójk¹ty (bok + podstawa) na segment
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (!mouseControlEnabled) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT)
        leftMousePressed = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        rightMousePressed = (action == GLFW_PRESS);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!mouseControlEnabled || currentCamera != FREECAM) return;

    freecamDistance -= yoffset * 0.5f;
    if (freecamDistance < 2.0f) freecamDistance = 2.0f;
    if (freecamDistance > 50.0f) freecamDistance = 50.0f;
}

// Shader compilation with better error handling
unsigned int compileShader(unsigned int type, const char* source) {
    // Check if OpenGL context is available
    if (!glfwGetCurrentContext()) {
        std::cout << "ERROR: No OpenGL context available!" << std::endl;
        return 0;
    }

    unsigned int shader = glCreateShader(type);
    if (shader == 0) {
        std::cout << "ERROR: Failed to create shader object!" << std::endl;
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    // Check compilation status
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "ERROR: Shader compilation failed!" << std::endl;
        std::cout << "Shader type: " << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT") << std::endl;
        std::cout << "Error log: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

void initShaders() {
    // Check if OpenGL context is available
    if (!glfwGetCurrentContext()) {
        std::cout << "ERROR: No OpenGL context available for shader initialization!" << std::endl;
        return;
    }

    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    if (vertexShader == 0 || fragmentShader == 0) {
        std::cout << "ERROR: Failed to compile shaders!" << std::endl;
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return;
    }

    shaderProgram = glCreateProgram();
    if (shaderProgram == 0) {
        std::cout << "ERROR: Failed to create shader program!" << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return;
    }

    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check linking status
    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR: Shader program linking failed!" << std::endl;
        std::cout << "Link error log: " << infoLog << std::endl;
    }
    else {
        std::cout << "Shaders compiled and linked successfully!" << std::endl;
    }

    // Clean up individual shaders (they're now part of the program)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

// Generate basic geometries
std::vector<float> generateCube() {
    return {
        // Front face
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,

        // Back face
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,

        // Left face
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,

        // Right face
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,

         // Top face
         -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
          0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
          0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
         -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,

         // Bottom face
         -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
          0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
          0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
         -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f
    };
}

std::vector<unsigned int> generateCubeIndices() {
    return {
        0, 1, 2,   2, 3, 0,     // Front
        4, 5, 6,   6, 7, 4,     // Back
        8, 9, 10,  10, 11, 8,   // Left
        12, 13, 14, 14, 15, 12, // Right
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };
}

std::vector<float> generateCylinder(int segments = 32) {
    std::vector<float> vertices;

    // Œrodek dolnej podstawy
    float centerDown[] = { 0.0f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f };
    // Œrodek górnej podstawy
    float centerUp[] = { 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f };

    for (int i = 0; i < segments; ++i) {
        float angle0 = 2.0f * M_PI * i / segments;
        float angle1 = 2.0f * M_PI * (i + 1) / segments;

        float x0 = cos(angle0), z0 = sin(angle0);
        float x1 = cos(angle1), z1 = sin(angle1);

        float u0 = (float)i / segments;
        float u1 = (float)(i + 1) / segments;

        // --- Dolna podstawa - trójk¹t fan ---
        vertices.insert(vertices.end(), std::begin(centerDown), std::end(centerDown));
        vertices.insert(vertices.end(), { x1, -0.5f, z1, 0.0f, -1.0f, 0.0f, (u1 + 1) * 0.5f, (1 - u1) * 0.5f });
        vertices.insert(vertices.end(), { x0, -0.5f, z0, 0.0f, -1.0f, 0.0f, (u0 + 1) * 0.5f, (1 - u0) * 0.5f });

        // --- Górna podstawa - trójk¹t fan ---
        vertices.insert(vertices.end(), std::begin(centerUp), std::end(centerUp));
        vertices.insert(vertices.end(), { x0, 0.5f, z0, 0.0f, 1.0f, 0.0f, (u0 + 1) * 0.5f, (1 - u0) * 0.5f });
        vertices.insert(vertices.end(), { x1, 0.5f, z1, 0.0f, 1.0f, 0.0f, (u1 + 1) * 0.5f, (1 - u1) * 0.5f });

        // Trójk¹t 1
        vertices.insert(vertices.end(), { x0, -0.5f, z0, x0, 0.0f, z0, u0, 0.0f });
        vertices.insert(vertices.end(), { x0, 0.5f, z0, x0, 0.0f, z0, u0, 1.0f });
        vertices.insert(vertices.end(), { x1, 0.5f, z1, x1, 0.0f, z1, u1, 1.0f });

        // Trójk¹t 2
        vertices.insert(vertices.end(), { x0, -0.5f, z0, x0, 0.0f, z0, u0, 0.0f });
        vertices.insert(vertices.end(), { x1, 0.5f, z1, x1, 0.0f, z1, u1, 1.0f });
        vertices.insert(vertices.end(), { x1, -0.5f, z1, x1, 0.0f, z1, u1, 0.0f });
    }

    return vertices;
}

// Object rendering functions
void setUniforms(const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& objectColor,
    bool useTexture ,
    const glm::vec3& emissiveColor)
{
    // Transformacje
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"),
        1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"),
        1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"),
        1, GL_FALSE, glm::value_ptr(projection));

    // Materia³
    glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"),
        1, glm::value_ptr(objectColor));
    glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"),
        useTexture);

    // Emisja
    glUniform3fv(glGetUniformLocation(shaderProgram, "emissiveColor"),
        1, glm::value_ptr(emissiveColor));

    // Œwiat³o globalne (dzieñ/noc) + ewentualnie spotlight
    glm::vec3 lightPos = isNight ? glm::vec3(0, 10, 0) : glm::vec3(10, 20, 10);
    glm::vec3 lightCol = isNight ? glm::vec3(0.3f, 0.3f, 0.5f)
        : glm::vec3(1.0f, 1.0f, 0.9f);
    float intensity = isNight ? 0.6f : 3.0f;

    if (headlightsOn) {
        lightPos = carPos + glm::vec3(
            1.5f * sin(glm::radians(carRotation)), 0.8f,
            1.5f * cos(glm::radians(carRotation))
        );
        lightCol = glm::vec3(1.0f, 1.0f, 0.9f);
        intensity = 2.0f;

        // Spotlight
        glm::vec3 forward = glm::vec3(
            sin(glm::radians(carRotation)), 0.0f,
            cos(glm::radians(carRotation))
        );
        glUniform3fv(glGetUniformLocation(shaderProgram, "spotDir"),
            1, glm::value_ptr(forward));
        glUniform1f(glGetUniformLocation(shaderProgram, "spotCutOff"),
            cos(glm::radians(20.0f)));
    }
    else {
        glUniform3f(glGetUniformLocation(shaderProgram, "spotDir"), 0, 0, 0);
        glUniform1f(glGetUniformLocation(shaderProgram, "spotCutOff"), -1.0f);
    }

    // Wysy³amy pozosta³e uniformy
    glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"),
        1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(shaderProgram, "lightColor"),
        1, glm::value_ptr(lightCol));
    glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"),
        1, glm::value_ptr(cameraPos));
    glUniform1f(glGetUniformLocation(shaderProgram, "lightIntensity"),
        intensity);
    glUniform1i(glGetUniformLocation(shaderProgram, "ourTexture"), 0);
}

void renderCylinder(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection,
    const glm::vec3& color, int segments = 32) {
    static unsigned int cylinderVAO = 0, cylinderVBO;

    if (cylinderVAO == 0) {
        auto vertices = generateCylinder(segments);

        glGenVertexArrays(1, &cylinderVAO);
        glGenBuffers(1, &cylinderVBO);

        glBindVertexArray(cylinderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cylinderVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        // Positions
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Normals
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Texture coordinates
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    setUniforms(model, view, projection, color);

    glBindVertexArray(cylinderVAO);

    int vertexCount = segments * 12; // jeœli masz pokrywa

    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

unsigned int createSimpleTexture(int width, int height, unsigned char r, unsigned char g, unsigned char b) {
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Utwórz prost¹ teksturê jednolitego koloru
    std::vector<unsigned char> data(width * height * 3);
    for (int i = 0; i < width * height * 3; i += 3) {
        data[i] = r;     // R
        data[i + 1] = g; // G
        data[i + 2] = b; // B
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return texture;
}



void renderCube(const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& color,
    bool useTexture = false,
    const glm::vec3& emissiveColor = glm::vec3(0.0f))
{
    static unsigned int cubeVAO = 0, cubeVBO, cubeEBO;

    if (cubeVAO == 0) {
        auto vertices = generateCube();
        auto indices = generateCubeIndices();

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glGenBuffers(1, &cubeEBO);

        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    setUniforms(model, view, projection, color, useTexture, emissiveColor);

    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

void renderCar(const glm::mat4& view, const glm::mat4& projection) {
    // 1) wspólna transformacja karoserii
    glm::mat4 carModel = glm::translate(glm::mat4(1.0f), carPos);
    carModel = glm::rotate(carModel, glm::radians(carRotation),
        glm::vec3(0, 1, 0));

    // === 2) Karoseria z tekstur¹ ===
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureCar);
    glm::mat4 body = glm::scale(carModel, glm::vec3(2.0f, 0.8f, 4.0f));
    renderCube(body, view, projection,
        glm::vec3(0.8f, 0.2f, 0.2f), true);

    // === 3) Spoiler z ty³u ===
    {
        glm::mat4 spoilerM = glm::translate(carModel,
            glm::vec3(0.0f, 0.6f, -2.2f));
        spoilerM = glm::scale(spoilerM,
            glm::vec3(1.8f, 0.3f, 0.4f));
        renderCube(spoilerM, view, projection,
            glm::vec3(0.1f, 0.1f, 0.1f));
    }

    // === 4) Sto¿ki-reflektory ===
    {
        // 1) Bazowa macierz auta: translate + obrót Y
        glm::mat4 base = glm::translate(glm::mat4(1.0f), carPos);
        base = glm::rotate(base,
            glm::radians(carRotation),
            glm::vec3(0, 1, 0));

        // 2) Offsets po bokach (x), wysokoœæ (y), g³êbokoœæ (z)
        glm::vec3 offsets[2] = {
            glm::vec3(-0.5f, 0, 2.1f),   // lewy reflektor
            glm::vec3(0.5f, 0, 2.1f)    // prawy reflektor
        };
        float inwardDeg = 15.0f;

        glm::vec3 offCol(0.2f, 0.2f, 0.2f),  // przygaszony
            onCol(1.0f, 1.0f, 0.9f);  // jasny
        glm::vec3 offEm = offCol * 0.2f,     // s³aba emisja
            onEm = onCol * 4.0f;     // mocna emisja

        for (int i = 0; i < 2; ++i) {
            glm::mat4 M = base;

            M = glm::translate(M, offsets[i]);

            M = glm::rotate(M,
                glm::radians(270.0f),
                glm::vec3(1, 0, 0));

            float zA = glm::radians((i == 0) ? -inwardDeg : +inwardDeg);
            M = glm::rotate(M, zA, glm::vec3(0, 0, 1));

            M = glm::scale(M, glm::vec3(0.3f, 1.5f, 0.3f));

            bool on = headlightsOn;
            glm::vec3 col = on ? onCol : offCol;
            glm::vec3 emi = on ? onEm : offEm;

            renderCone(M, view, projection,
                col,    // objectColor
                16,     // segments
                emi);   // emissiveColor
        }
    }

    // === 5) Tylne œwiat³a stopu ===
    {
        glm::vec3 tailOffs[2] = {
            glm::vec3(-0.5f, 0.2f, -2.0f),
            glm::vec3(0.5f, 0.2f, -2.0f)
        };

        bool braking = keys[GLFW_KEY_S];
        // kolor i emissive w jednym
        glm::vec3 baseCol = braking
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.3f, 0.0f, 0.0f);
        glm::vec3 emissiveCol = baseCol * (braking ? 3.0f : 1.0f);

        for (auto& off : tailOffs) {
            glm::mat4 tM = glm::translate(carModel, off);
            tM = glm::scale(tM, glm::vec3(0.2f, 0.2f, 0.1f));

            // teraz z emissiveCol w ostatnim argumencie
            renderCube(tM, view, projection,
                baseCol,        // objectColor
                false,          // useTexture
                emissiveCol);   // emissiveColor
        }
    }
    // === 6) Ko³a ===
    {
        glm::vec3 wheelPos[4] = {
            {-1.2f,0,1.5f},{1.2f,0,1.5f},
            {-1.2f,0,-1.5f},{1.2f,0,-1.5f}
        };
        for (int i = 0; i < 4; ++i) {
            glm::mat4 wM = glm::translate(carModel, wheelPos[i]);
            wM = glm::rotate(wM, wheelRotation,
                glm::vec3(1, 0, 0));
            wM = glm::rotate(wM, glm::radians(90.0f),
                glm::vec3(0, 0, 1));
            wM = glm::scale(wM,
                glm::vec3(0.6f, 0.6f, 0.6f));
            renderCylinder(wM, view, projection,
                glm::vec3(0.1f, 0.1f, 0.1f));
        }
    }
}

void renderTrack(const glm::mat4& view, const glm::mat4& projection) {
    glm::mat4 trackModel = glm::mat4(1.0f);
    trackModel = glm::rotate(trackModel, glm::radians(trackRotation), glm::vec3(0.0f, 1.0f, 0.0f));

    // === G³ówna powierzchnia toru z tekstur¹ ===
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureTrack);

    glm::mat4 surfaceModel = trackModel;
    surfaceModel = glm::translate(surfaceModel, glm::vec3(0.0f, 0.0f, 0.0f));
    surfaceModel = glm::scale(surfaceModel, glm::vec3(20.0f, 0.1f, 40.0f));
    renderCube(surfaceModel, view, projection, glm::vec3(0.3f, 0.3f, 0.3f), true);

    // === Bariery bez tekstur ===
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = -1; i <= 1; i += 2) {
        glm::mat4 barrierModel = trackModel;
        barrierModel = glm::translate(barrierModel, glm::vec3(i * 11.0f, 0.5f, 0.0f));
        barrierModel = glm::scale(barrierModel, glm::vec3(0.5f, 1.0f, 42.0f));
        renderCube(barrierModel, view, projection, glm::vec3(0.9f, 0.9f, 0.9f));
    }
}

void renderEnvironment(const glm::mat4& view, const glm::mat4& projection) {
    // === Ground/grass z tekstur¹ ===
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureGround);

    glm::mat4 groundModel = glm::mat4(1.0f);
    groundModel = glm::translate(groundModel, glm::vec3(0.0f, -0.1f, 0.0f));
    groundModel = glm::scale(groundModel, glm::vec3(100.0f, 0.1f, 100.0f));

    renderCube(groundModel, view, projection, glm::vec3(0.2f, 0.6f, 0.2f), true);

    // === Trees (bez tekstur) ===
    glBindTexture(GL_TEXTURE_2D, 0); // Wy³¹cz tekstury

    glm::vec3 treePositions[] = {
        glm::vec3(-15.0f, 0.0f, -15.0f),
        glm::vec3(15.0f, 0.0f, -15.0f),
        glm::vec3(-15.0f, 0.0f, 15.0f),
        glm::vec3(15.0f, 0.0f, 15.0f),
        glm::vec3(-25.0f, 0.0f, 0.0f),
        glm::vec3(25.0f, 0.0f, 0.0f)
    };

    for (const auto& pos : treePositions) {
        // Tree trunk
        glm::mat4 trunkModel = glm::mat4(1.0f);
        trunkModel = glm::translate(trunkModel, pos + glm::vec3(0.0f, 1.0f, 0.0f));
        trunkModel = glm::scale(trunkModel, glm::vec3(0.3f, 2.0f, 0.3f) * treeSize);
        renderCube(trunkModel, view, projection, glm::vec3(0.4f, 0.2f, 0.1f));

        // Tree crown
        glm::mat4 crownModel = glm::mat4(1.0f);
        crownModel = glm::translate(crownModel, pos + glm::vec3(0.0f, 2.5f, 0.0f));
        if (treeShapeIsRound) {
            crownModel = glm::scale(crownModel, glm::vec3(1.5f, 1.5f, 1.5f) * treeSize);
        }
        else {
            crownModel = glm::scale(crownModel, glm::vec3(1.2f, 2.0f, 1.2f) * treeSize);
        }
        renderCube(crownModel, view, projection, treeColor);
    }

    // === Buildings/Tribunes z tekstur¹ ===
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureBuilding);

    glm::vec3 buildingPositions[] = {
        glm::vec3(0.0f, 0.0f, -30.0f),
        glm::vec3(-20.0f, 0.0f, -25.0f),
        glm::vec3(20.0f, 0.0f, -25.0f)
    };

    for (const auto& pos : buildingPositions) {
        glm::mat4 buildingModel = glm::mat4(1.0f);
        buildingModel = glm::translate(buildingModel, pos + glm::vec3(0.0f, 3.0f, 0.0f));
        buildingModel = glm::scale(buildingModel, glm::vec3(8.0f, 6.0f, 4.0f));
        renderCube(buildingModel, view, projection, glm::vec3(0.7f, 0.7f, 0.8f), true);
    }
}

void updateCamera() {
    switch (currentCamera) {
    case CHASE:
        cameraPos = carPos + glm::vec3(
            -8.0f * sin(glm::radians(carRotation)),
            4.0f,
            -8.0f * cos(glm::radians(carRotation))
        );
        cameraTarget = carPos;
        break;

    case COCKPIT:
        cameraPos = carPos + glm::vec3(0.0f, 1.2f, 0.0f);
        cameraTarget = carPos + glm::vec3(
            10.0f * sin(glm::radians(carRotation)),
            1.2f,
            10.0f * cos(glm::radians(carRotation))
        );
        break;

    case SIDE:
        cameraPos = glm::vec3(15.0f, 5.0f, carPos.z);
        cameraTarget = carPos;
        break;

    case ORBITAL:
        cameraAngle += 0.05f * orbitalDirection;
        if (cameraAngle > 360.0f) cameraAngle -= 360.0f;
        cameraPos = carPos + glm::vec3(
            12.0f * cos(glm::radians(cameraAngle)),
            6.0f,
            12.0f * sin(glm::radians(cameraAngle))
        );
        cameraTarget = carPos;
        break;

    case FREECAM:
    
        float radius = freecamDistance;
        float yawRad = glm::radians(freecamYaw);
        float pitchRad = glm::radians(freecamPitch);

        cameraPos.x = carPos.x + radius * cos(pitchRad) * cos(yawRad) + freecamPanX;
        cameraPos.y = carPos.y + radius * sin(pitchRad) + freecamPanY;
        cameraPos.z = carPos.z + radius * cos(pitchRad) * sin(yawRad);

        cameraTarget = carPos + glm::vec3(freecamPanX, freecamPanY, 0.0f);
        break;
    }
}

void updateCarPhysics(float deltaTime) {
    const float maxSpeed = 15.0f;
    const float acceleration = 8.0f;
    const float deceleration = 5.0f;
    const float turnSpeed = 90.0f;

    // Forward/backward movement
    if (keys[GLFW_KEY_W]) {
        carSpeed = std::min(carSpeed + acceleration * deltaTime, maxSpeed);
    }
    else if (keys[GLFW_KEY_S]) {
        carSpeed = std::max(carSpeed - acceleration * deltaTime, -maxSpeed * 0.5f);
    }
    else {
        // Natural deceleration
        if (carSpeed > 0) {
            carSpeed = std::max(0.0f, carSpeed - deceleration * deltaTime);
        }
        else if (carSpeed < 0) {
            carSpeed = std::min(0.0f, carSpeed + deceleration * deltaTime);
        }
    }

    // Steering
    if (keys[GLFW_KEY_A] && std::abs(carSpeed) > 0.1f) {
        carRotation += turnSpeed * deltaTime * (carSpeed / maxSpeed);
    }
    if (keys[GLFW_KEY_D] && std::abs(carSpeed) > 0.1f) {
        carRotation -= turnSpeed * deltaTime * (carSpeed / maxSpeed);
    }

    // Update position
    carPos.x += carSpeed * sin(glm::radians(carRotation)) * deltaTime;
    carPos.z += carSpeed * cos(glm::radians(carRotation)) * deltaTime;

    // Wheel rotation
    wheelRotation += carSpeed * deltaTime * 2.0f;

    // Reset car position
    if (keys[GLFW_KEY_R]) {
        carPos = glm::vec3(0.0f, 0.5f, 0.0f);
        carRotation = 0.0f;
        carSpeed = 0.0f;
    }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_RELEASE) {
        bool pressed = (action == GLFW_PRESS);

        if (key >= 0 && key < 1024) {
            keys[key] = pressed;
        }

        if (action == GLFW_PRESS) {
            switch (key) {
            case GLFW_KEY_1: currentCamera = CHASE; break;
            case GLFW_KEY_2: currentCamera = COCKPIT; break;
            case GLFW_KEY_3: currentCamera = SIDE; break;
            case GLFW_KEY_4:
                if (currentCamera == ORBITAL)
                    orbitalDirection *= -1.0f; // zmiana kierunku
                else
                    currentCamera = ORBITAL;
                break;
            case GLFW_KEY_5:
                currentCamera = FREECAM;
                break;
            case GLFW_KEY_L: headlightsOn = !headlightsOn; break;
            case GLFW_KEY_N:
                isNight = !isNight;
                timeOfDay = isNight ? 0.0f : 1.0f;
                break;
            case GLFW_KEY_T: trackRotation += 15.0f; break;
            case GLFW_KEY_Y: trackRotation += 45.0f; break;
            case GLFW_KEY_G:
                treeColor = glm::vec3(
                    static_cast<float>(rand()) / RAND_MAX,
                    static_cast<float>(rand()) / RAND_MAX,
                    static_cast<float>(rand()) / RAND_MAX
                );
                break;
            case GLFW_KEY_H:
                treeSize = (treeSize > 1.5f) ? 0.5f : treeSize + 0.3f;
                break;
            case GLFW_KEY_J: treeShapeIsRound = !treeShapeIsRound; break;
            case GLFW_KEY_U: carRotation += 90.0f; break;
            case GLFW_KEY_M:
                mouseControlEnabled = !mouseControlEnabled;  // NOWE: w³¹cz/wy³¹cz mysz
                if (mouseControlEnabled) {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    firstMouse = true;
                }
                else {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                break;
            case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, true); break;
            }
        }
    }
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!mouseControlEnabled || currentCamera != FREECAM) return;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    if (leftMousePressed) {
        freecamYaw += xoffset * 2.0f;
        freecamPitch += yoffset * 2.0f;

        // ograniczenia pitch
        if (freecamPitch > 89.0f) freecamPitch = 89.0f;
        if (freecamPitch < -89.0f) freecamPitch = -89.0f;
    }

    if (rightMousePressed) {
        freecamPanX += xoffset * 0.05f;
        freecamPanY += yoffset * 0.05f;
    }
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    glViewport(0, 0, width, height);
}



unsigned int loadTexture(const char* path) {
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrChannels;

    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (!data) {
        std::cout << "Failed to load texture at path: " << path << std::endl;
        return 0;
    }
    else {
        std::cout << "Texture loaded: " << path << " (" << width << "x" << height << ", " << nrChannels << " channels)" << std::endl;
    }

    GLenum format;
    if (nrChannels == 1)
        format = GL_RED;
    else if (nrChannels == 3)
        format = GL_RGB;
    else if (nrChannels == 4)
        format = GL_RGBA;
    else {
        std::cout << "Unsupported texture format: " << nrChannels << " channels" << std::endl;
        stbi_image_free(data);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cout << "OpenGL error after glTexImage2D: " << error << std::endl;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);


    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    std::cout << "Loaded texture: " << path << " (" << width << "x" << height << ", " << nrChannels << " channels)" << std::endl;

    stbi_image_free(data);
    return textureID;
}

void initTextures() {
    int maxTextureSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    std::cout << "Max texture size supported: " << maxTextureSize << std::endl;
    textureGround = loadTexture("textures/grass.jpg");
    textureTrack = loadTexture("textures/asphalt.jpg");
    textureCar = loadTexture("textures/car.jpg");
    textureBuilding = loadTexture("textures / building.jpg");
}

bool initOpenGL() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cout << "ERROR: Failed to initialize GLFW" << std::endl;
        return false;
    }

    // GLFW configuration - more conservative for Intel graphics
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Additional hints for better compatibility
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    // Create window
    window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Racing Car Simulator - OpenGL", NULL, NULL);
    if (!window) {
        std::cout << "ERROR: Failed to create GLFW window" << std::endl;
        std::cout << "Trying with OpenGL 3.0..." << std::endl;

        // Try with lower OpenGL version
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

        window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Racing Car Simulator - OpenGL", NULL, NULL);
        if (!window) {
            std::cout << "ERROR: Failed to create window with OpenGL 3.0" << std::endl;
            glfwTerminate();
            return false;
        }
    }

    // Make context current BEFORE calling any OpenGL functions
    glfwMakeContextCurrent(window);

    // Set callbacks
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Initialize GLEW
    glewExperimental = GL_TRUE; // Important for Intel graphics!
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cout << "ERROR: GLEW initialization failed: " << glewGetErrorString(err) << std::endl;
        glfwTerminate();
        return false;
    }

    // Clear any GLEW initialization errors
    glGetError(); // This clears the error flag

    // Print OpenGL information
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // OpenGL configuration - safer approach for Intel graphics
    std::cout << "Configuring OpenGL states..." << std::endl;

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cout << "ERROR enabling depth test: " << error << std::endl;
        return false;
    }

    // Check if face culling is supported before enabling
    if (glewIsSupported("GL_VERSION_1_1")) {
        glEnable(GL_CULL_FACE);
        error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cout << "WARNING: Face culling not supported: " << error << std::endl;
            std::cout << "Continuing without face culling..." << std::endl;
        }
        else {
            glCullFace(GL_BACK);
            error = glGetError();
            if (error != GL_NO_ERROR) {
                std::cout << "WARNING: Could not set cull face: " << error << std::endl;
            }
        }
    }
    else {
        std::cout << "Face culling not supported by this OpenGL version" << std::endl;
    }

    // Set viewport
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cout << "ERROR setting viewport: " << error << std::endl;
        return false;
    }

    // Final error check
    error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cout << "OpenGL Error after initialization: " << error << std::endl;
        std::cout << "Continuing anyway..." << std::endl;
    }

    std::cout << "OpenGL initialized successfully!" << std::endl;
    return true;
}

// Add this function at the top of your file, after includes
void checkOpenGLError(const char* stmt, const char* fname, int line) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cout << "OpenGL error " << err << " at " << fname << ":" << line << " - for " << stmt << std::endl;
        switch (err) {
        case GL_INVALID_ENUM:
            std::cout << "GL_INVALID_ENUM: An unacceptable value is specified for an enumerated argument." << std::endl;
            break;
        case GL_INVALID_VALUE:
            std::cout << "GL_INVALID_VALUE: A numeric argument is out of range." << std::endl;
            break;
        case GL_INVALID_OPERATION:
            std::cout << "GL_INVALID_OPERATION: The specified operation is not allowed in the current state." << std::endl;
            break;
        case GL_OUT_OF_MEMORY:
            std::cout << "GL_OUT_OF_MEMORY: There is not enough memory left to execute the command." << std::endl;
            break;
        default:
            std::cout << "Unknown OpenGL error." << std::endl;
            break;
        }
    }
}

// Macro for easier debugging
#ifdef _DEBUG
#define GL_CHECK(stmt) do { \
            stmt; \
            checkOpenGLError(#stmt, __FILE__, __LINE__); \
        } while (0)
#else
#define GL_CHECK(stmt) stmt
#endif

// Modified render function with error checking
void render() {
    // Clear screen
    glm::vec3 clearColor = isNight ? glm::vec3(0.1f, 0.1f, 0.2f) : glm::vec3(0.5f, 0.7f, 1.0f);
    GL_CHECK(glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    // Use shader program
    GL_CHECK(glUseProgram(shaderProgram));

    // Set up matrices
    glm::mat4 projection = glm::perspective(glm::radians(45.0f),
        (float)SCR_WIDTH / (float)SCR_HEIGHT,
        0.1f, 100.0f);

    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));

    // Render scene objects
    renderEnvironment(view, projection);
    renderTrack(view, projection);
    renderCar(view, projection);
}

void printControls() {
    std::cout << "\n=== RACING CAR SIMULATOR CONTROLS ===" << std::endl;
    std::cout << "\nCAR MOVEMENT:" << std::endl;
    std::cout << "W - Accelerate forward" << std::endl;
    std::cout << "S - Brake / Reverse (turn on lights stop)" << std::endl;
    std::cout << "A - Turn left" << std::endl;
    std::cout << "D - Turn right" << std::endl;
    std::cout << "R - Reset car position" << std::endl;

    std::cout << "\nCAMERA MODES:" << std::endl;
    std::cout << "1 - Chase camera (behind car)" << std::endl;
    std::cout << "2 - Cockpit camera (inside car)" << std::endl;
    std::cout << "3 - Side camera (track side)" << std::endl;
    std::cout << "4 - Orbital camera (rotating around car)" << std::endl;
    std::cout << "5 - Free camera (mouse control)" << std::endl;
    std::cout << "M - Toggle mouse control (only Free camera mode)" << std::endl;

    std::cout << "\nLIGHT CONTROLS:" << std::endl;
    std::cout << "L - Toggle car headlights (front lights)" << std::endl;
    std::cout << "N - Toggle day/night cycle" << std::endl;

    std::cout << "\nENVIRONMENT CONTROLS:" << std::endl;
    std::cout << "T - Rotate track (15 degrees)" << std::endl;
    std::cout << "Y - Rotate track (45 degrees)" << std::endl;
    std::cout << "G - Change tree colors" << std::endl;
    std::cout << "H - Change tree size" << std::endl;
    std::cout << "J - Toggle tree shape (cone/sphere)" << std::endl;
    std::cout << "U - Rotate car in place" << std::endl;

    std::cout << "\nESC - Exit simulator" << std::endl;
    std::cout << "\n=====================================" << std::endl;
}

int main() {
    // Print controls
    printControls();

    // Initialize OpenGL FIRST
    if (!initOpenGL()) {
        return -1;
    }

    // Initialize shaders AFTER OpenGL/GLEW initialization
    initShaders();

    // Initialize textures AFTER shaders
    initTextures();

    // Initialize timing
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    std::cout << "\nRacing Car Simulator started successfully!" << std::endl;
    std::cout << "Use the controls above to interact with the simulation." << std::endl;
    std::cout << "Press M to enable mouse control in orbital camera mode." << std::endl;

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        // Calculate delta time
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Process input
        glfwPollEvents();

        // Update game state
        updateCarPhysics(deltaTime);
        updateCamera();

        // Render
        render();

        // Swap buffers
        glfwSwapBuffers(window);
    }

    // Cleanup
    glDeleteTextures(1, &textureGround);
    glDeleteTextures(1, &textureTrack);
    glDeleteTextures(1, &textureCar);
    glDeleteTextures(1, &textureBuilding);
    glDeleteProgram(shaderProgram);
    glfwTerminate();

    std::cout << "Racing Car Simulator terminated successfully!" << std::endl;
    return 0;
}