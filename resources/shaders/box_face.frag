#version 330 core

in vec4 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uCameraPos;

out vec4 fragColor;

void main()
{
    // Простое освещение: каждая грань чуть отличается по яркости
    vec3 lightDir = normalize(uCameraPos - vWorldPos);
    float diffuse = abs(dot(normalize(vNormal), lightDir));
    float brightness = 0.4 + 0.6 * diffuse;  // Минимум 40% яркости

    fragColor = vec4(vColor.rgb * brightness, vColor.a);
}