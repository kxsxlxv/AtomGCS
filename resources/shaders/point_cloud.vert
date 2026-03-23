#version 330 core
layout (location = 0) in vec3 inPosition;
layout (location = 1) in float inIntensity;

uniform mat4 uMvp;
uniform mat4 uView;
uniform float uPointSize;
uniform float uMaxDistance;
uniform int uDistancePointSizing;

out float vIntensity;
out float vDistance;

void main()
{
    vec4 viewPosition = uView * vec4(inPosition, 1.0);
    float worldDistance = length(inPosition);
    float viewDistance = max(length(viewPosition.xyz), 0.001);
    float sizeScale = (uDistancePointSizing != 0)
        ? clamp(8.0 / viewDistance, 0.65, 2.4)
        : 1.0;

    gl_Position = uMvp * vec4(inPosition, 1.0);
    gl_PointSize = uPointSize * sizeScale;
    vIntensity = clamp(inIntensity / 255.0, 0.0, 1.0);
    vDistance = clamp(worldDistance / max(uMaxDistance, 0.001), 0.0, 1.0);
}