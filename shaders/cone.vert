#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(std140, set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 viewDirection;
    vec4 ambient;
    vec4 lightDirection;
    vec4 lightColor;
    uvec4 lightCounts;
} scene;
layout(std140, set = 1, binding = 0) uniform Model {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 coneUV;
void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    gl_Position = scene.viewProjection * world;
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    // The side narrows to zero width at the tip. Interpolate u times that width.
    float width = inNormal.y < 0.0 ? 1.0 : inUV.y;
    // Center the pictures toward the initial camera, with their right edge on the right.
    float u = inNormal.y < 0.0 ? inUV.x : 0.65 - inUV.x;
    coneUV = vec3(u * width, inUV.y, width);
}

