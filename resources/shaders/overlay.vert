#version 330 core

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 inNormal;

uniform mat4 uMvp;

out vec4 vColor;
out vec3 vNormal;
out vec3 vWorldPos;

void main()
{
    gl_Position = uMvp * vec4(inPosition, 1.0);
    vColor = inColor;
    vNormal = inNormal;
    vWorldPos = inPosition;
}
