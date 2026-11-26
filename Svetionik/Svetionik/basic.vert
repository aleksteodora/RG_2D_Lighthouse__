#version 330 core

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inCol;
layout(location = 2) in vec2 inUV;

uniform vec2 uOffset; // pomeraj objekta tj translacija
uniform vec2 uScale;  // skaliranje objekta

out vec4 chCol;
out vec2 chUV;
out vec2 chPos; // pozicija u ekranskim koordinatama (-1, 1), koristi se u fragment sejderima

void main()
{
    vec2 p = inPos * uScale + uOffset;
    gl_Position = vec4(p, 0.0, 1.0);
    chCol = inCol;
    chUV = inUV;
    chPos = p;
}
