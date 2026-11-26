#version 330 core

in vec4 chCol;
in vec2 chUV;
in vec2 chPos;
out vec4 outCol;

// Gradijent (nebo i more): od donje do gornje boje, uz opcioni talasasti sjaj
uniform vec3 uTop;
uniform vec3 uBottom;
uniform float uWave;
uniform float uTime;

void main()
{
    vec3 c = mix(uBottom, uTop, chUV.y);
    float w = sin(chPos.y * 80.0 + uTime * 1.6 + chPos.x * 9.0)
            * sin(chPos.y * 37.0 - uTime * 1.1 + chPos.x * 4.0);
    c *= 1.0 + uWave * w;
    outCol = vec4(c, 1.0);
}
