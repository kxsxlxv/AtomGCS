#version 330 core

in vec4 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

uniform float uNormalSign; // 1.0 или -1.0 для инверсии нормалей

out vec4 fragColor;

void main()
{
    float nLen = length(vNormal);
    if (nLen < 0.001)
    {
        // Плоский цвет (линии, рёбра, грани кубов)
        fragColor = vColor;
    }
    else
    {
        vec3 N = normalize(vNormal) * uNormalSign;
        vec3 lightDir = normalize(vec3(0.3, 1.0, 0.6));
        float diffuse = max(dot(N, lightDir), 0.0);

        vec3 fillDir = normalize(vec3(-0.2, -0.3, 0.5));
        float fill = max(dot(N, fillDir), 0.0);

        float ambient = 0.15;
        float brightness = ambient + diffuse * 0.70 + fill * 0.15;
        fragColor = vec4(vColor.rgb * brightness, 1.0);
    }
}
