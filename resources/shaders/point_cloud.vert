#version 330 core
layout (location = 0) in vec3 inPosition;
layout (location = 1) in float inIntensity;
layout (location = 2) in float inDistance;

uniform mat4 uMvp;
uniform float uPointSize;
uniform float uMaxDistance;

out float vIntensity;
out float vDistance;

void main()
{
    gl_Position = uMvp * vec4(inPosition, 1.0);
    gl_PointSize = uPointSize;
    vIntensity = clamp(inIntensity / 255.0, 0.0, 1.0);
    vDistance = clamp(inDistance / max(uMaxDistance, 0.001), 0.0, 1.0);
}
