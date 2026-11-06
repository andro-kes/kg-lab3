#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(std140, set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 viewDirection;
    vec4 ambient;
    vec4 lightDirection;
    vec4 lightColor;
    uvec4 lightCounts;
} scene;
layout(location = 0) out vec3 color;
void main() {
    gl_Position = scene.viewProjection * vec4(inPosition, 1.0);
    color = inColor;
}

