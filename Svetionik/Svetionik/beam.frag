#version 330 core

in vec2 chPos;
out vec4 outCol;

uniform vec2 uLamp;
uniform float uBeamAngle;
uniform float uBeamIntensity;
uniform float uAspect;

// Ista funkcija kao u basic.frag
float beamFactor(vec2 p)
{
    vec2 d = vec2((p.x - uLamp.x) * uAspect, p.y - uLamp.y);
    float dist = length(d);
    if (dist < 0.0001) return uBeamIntensity;
    vec2 dir = vec2(cos(uBeamAngle), sin(uBeamAngle));
    float ang = acos(clamp(dot(d / dist, dir), -1.0, 1.0));
    float cone = 1.0 - smoothstep(0.07, 0.22, ang);
    float fade = 1.0 - 0.5 * smoothstep(0.5, 4.5, dist);
    return uBeamIntensity * cone * fade;
}

void main()
{
    float f = beamFactor(chPos);
    float dist = length(vec2((chPos.x - uLamp.x) * uAspect, chPos.y - uLamp.y));
    float glow = uBeamIntensity * 0.7 * exp(-dist * 10.0); // sjaj oko lampe
    float a = clamp(f * 0.6 + glow, 0.0, 1.0);
    outCol = vec4(1.0, 0.95, 0.7, a);
}
