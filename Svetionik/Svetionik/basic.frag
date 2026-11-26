#version 330 core

in vec4 chCol;
in vec2 chUV;
in vec2 chPos;
out vec4 outCol;

uniform vec4 uTint;      // mnozi se sa konacnom bojom (ovde se podesava providnost)
uniform int uUseTex;     // 1 - koristi teksturu, 0 - koristi boju temena
uniform sampler2D uTex;

// Osvetljenost snopom (koristi se za brod)
uniform int uLit;            // 1 - providnost objekta zavisi od snopa svetionika
uniform float uAmbient;      // minimalna vidljivost van snopa
uniform vec2 uLamp;          // pozicija lampe svetionika
uniform float uBeamAngle;    // ugao snopa (prosledjuje se iz C++)
uniform float uBeamIntensity;// intenzitet snopa (prosledjuje se iz C++)
uniform float uAspect;       // odnos sirine i visine ekrana

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
    vec4 c = (uUseTex == 1) ? texture(uTex, chUV) : chCol;
    c *= uTint;
    if (uLit == 1)
        c.a *= clamp(max(uAmbient, beamFactor(chPos)), 0.0, 1.0);
    outCol = c;
}
