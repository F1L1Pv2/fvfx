#version 450

layout(location = 0) out vec4 outColor;

layout (location = 0) in vec3 Normal;
layout (location = 1) in vec3 FragPos;

void main() {
    vec3 objectColor = vec3(1.0,0.0,0.0);
    vec3 ambient = vec3(0.2,0.2,0.2);
    vec3 lightPos = vec3(3,2,1);
    vec3 lightColor = vec3(1,1,1);
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    vec3 result = (ambient + diffuse) * objectColor;

    outColor = vec4(result, 1.0);
}