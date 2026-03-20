#version 330 core
in float vIntensity;
in float vDistance;

uniform int uColorMode;

out vec4 fragColor;

vec3 heatMap(float value)
{
    return mix(vec3(0.10, 0.35, 0.95), vec3(0.95, 0.15, 0.10), 1.0 - value);
}

void main()
{
    float value = (uColorMode == 0) ? vIntensity : vDistance;
    fragColor = vec4(heatMap(value), 1.0);
}
