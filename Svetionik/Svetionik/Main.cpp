#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <thread>
#include <chrono>
#include <algorithm>
#include "Util.h"

// ======================================================================
//  1. PODESAVANJA I STANJE PROGRAMA
// ======================================================================
const std::string name = "Teodora Aleksic SV7/2023";   // natpis u donjem desnom uglu

const double FRAME_TIME = 1.0 / 75.0;   // frame limiter: 75 FPS

const float PI = 3.14159265f;

// Snop i brod
const float BEAM_SPEED = 0.8f;          // ugaona brzina snopa (rad/s)
const float SHIP_SPEED = 0.04f;         // brzina broda (NDC/s)
const float SHIP_START_X = -0.85f;
const float SHIP_WAIT_X = 0.05f;        // mesto cekanja tokom oluje
const float SHIP_DOCK_X = 0.36f;        // pozicija u luci (posle svitanja)
const float SHIP_Y = -0.20f;

const float LAMP_X = 0.80f;             // pozicija lampe svetionika
const float LAMP_Y = 0.465f;

// Tamni providni sloj i svitanje
const float OVERLAY_NORMAL = 0.6f;
const float OVERLAY_STORM = 0.9f;
const float OVERLAY_RISE_RATE = 0.04f;
const float DAWN_RATE = 0.15f;

// Tok oluje: treperenje snopa pa ispad napajanja
const float FLICKER_START = 1.5f;       // sekunde od pocetka oluje
const float POWER_FAIL_TIME = 6.0f;

// Intenzitet snopa i trosenje baterije
const float BEAM_NORMAL = 1.0f;
const float BEAM_BACKUP = 0.5f;
const float BEAM_SAVING = 0.2f;
const float DRAIN_BACKUP = 1.0f / 60.0f;    // prazni se za 60 s
const float DRAIN_SAVING = 1.0f / 180.0f;   // prazni se za 180 s (stednja)

// Baterija i lampica (NDC koordinate)
const float BAT_X0 = -0.96f, BAT_Y0 = -0.96f, BAT_X1 = -0.74f, BAT_Y1 = -0.84f;
const float BAT_PAD_X = 0.006f, BAT_PAD_Y = 0.010f;
const float LED_X = -0.66f, LED_Y = -0.90f, LED_R = 0.035f;

enum Phase { CALM, STORM, DAWN };

// Sve promenljive koje se menjaju tokom rada (update ih menja, a glavna petlja ih cita pri crtanju
struct State {
    Phase phase = CALM;
    float beamAngle = 0.0f;
    float beamIntensity = BEAM_NORMAL;
    float overlay = OVERLAY_NORMAL;
    float dawn = 0.0f;               // 0 - noc, 1 - jutro
    float stormTimer = 0.0f;
    bool powerFailed = false;        // glavno napajanje palo
    bool backup = false;             // rezerva ukljucena
    float battery = 1.0f;            // 0..1
    float shipX = SHIP_START_X;
    float flashT = 99.0f;            // vreme od poslednje munje
    float nextFlash = 2.0f;
    bool flickerOn = true;
    float flickerSwitch = 0.0f;
} g;

float g_aspect = 1.0f;   // sirina/visina ekrana

bool dawnDone() { return g.phase == DAWN && g.overlay <= 0.0f && g.dawn >= 1.0f; }

float frand(float a, float b) { return a + (b - a) * (std::rand() / (float)RAND_MAX); }

int endProgram(std::string message) {
    std::cout << message << std::endl;
    glfwTerminate();
    return -1;
}

// ======================================================================
//  2. GEOMETRIJA
// ======================================================================
struct Col { float r, g, b, a; };
struct Pt { float x, y; };

// Skuplja temena u niz koji se zatim salje na GPU
struct Builder {
    std::vector<float> v;

    void vert(Pt p, Col c, float u = 0.0f, float t = 0.0f) {
        v.insert(v.end(), { p.x, p.y, c.r, c.g, c.b, c.a, u, t });
    }
    void tri(Pt a, Pt b, Pt c, Col col) { vert(a, col); vert(b, col); vert(c, col); }
    void quad(Pt a, Pt b, Pt c, Pt d, Col col) { tri(a, b, c, col); tri(a, c, d, col); }

    // Pravougaonik sa UV koordinatama (donji levi ugao teksture je (0,0))
    void rect(float x0, float y0, float x1, float y1, Col col) {
        Pt tl{ x0, y1 }, bl{ x0, y0 }, br{ x1, y0 }, tr{ x1, y1 };
        vert(tl, col, 0, 1); vert(bl, col, 0, 0); vert(br, col, 1, 0);
        vert(tl, col, 0, 1); vert(br, col, 1, 0); vert(tr, col, 1, 1);
    }
    // Konveksan poligon (lepeza trouglova iz prve tacke)
    void poly(const std::vector<Pt>& p, Col col) {
        for (size_t i = 1; i + 1 < p.size(); ++i) tri(p[0], p[i], p[i + 1], col);
    }
    // Poligon "viden" iz tacke anchor (za nekonveksne oblike, npr. stenu)
    void fan(Pt anchor, const std::vector<Pt>& p, Col col) {
        for (size_t i = 0; i + 1 < p.size(); ++i) tri(anchor, p[i], p[i + 1], col);
    }
    // Krug od 'seg' trouglica; X se deli sa g_aspect da krug ne bude elipsa
    void circle(float cx, float cy, float r, Col col, int seg = 40) {
        for (int i = 0; i < seg; ++i) {
            float a0 = i * 2.0f * PI / seg, a1 = (i + 1) * 2.0f * PI / seg;
            tri({ cx, cy },
                { cx + std::cos(a0) * r / g_aspect, cy + std::sin(a0) * r },
                { cx + std::cos(a1) * r / g_aspect, cy + std::sin(a1) * r }, col);
        }
    }
};

// Objekat poslat na GPU: VAO (raspored podataka), VBO (podaci), broj temena
struct Mesh {
    unsigned int VAO = 0, VBO = 0;
    int count = 0;
};

Mesh makeMesh(const std::vector<float>& data) {
    Mesh m;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    const int stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);                    // pozicija
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));  // boja
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));  // UV
    glEnableVertexAttribArray(2);
    m.count = (int)(data.size() / 8);
    return m;
}

// ======================================================================
//  3. UNIFORME I CRTANJE
// ======================================================================
void setF(unsigned int p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
void setI(unsigned int p, const char* n, int v) { glUniform1i(glGetUniformLocation(p, n), v); }
void set2(unsigned int p, const char* n, float a, float b) { glUniform2f(glGetUniformLocation(p, n), a, b); }
void set3(unsigned int p, const char* n, float a, float b, float c) { glUniform3f(glGetUniformLocation(p, n), a, b, c); }
void set4(unsigned int p, const char* n, float a, float b, float c, float d) { glUniform4f(glGetUniformLocation(p, n), a, b, c, d); }

// Salje sejderima ugao i intenzitet snopa
void setBeamUniforms(unsigned int prog, float ambient) {
    glUseProgram(prog);
    set2(prog, "uLamp", LAMP_X, LAMP_Y);
    setF(prog, "uBeamAngle", g.beamAngle);
    setF(prog, "uBeamIntensity", g.beamIntensity);
    setF(prog, "uAspect", g_aspect);
    setF(prog, "uAmbient", ambient);
}

// Opcije crtanja jednog objekta: pomeraj, razmera, boja/providnost, tekstura, osvetljenje snopom
struct DrawOpts {
    float ox = 0, oy = 0, sx = 1, sy = 1;
    float r = 1, g = 1, b = 1, a = 1;
    bool tex = false, lit = false;
};

void drawMesh(unsigned int prog, const Mesh& m, const DrawOpts& o = DrawOpts()) {
    glUseProgram(prog);
    set2(prog, "uOffset", o.ox, o.oy);
    set2(prog, "uScale", o.sx, o.sy);
    set4(prog, "uTint", o.r, o.g, o.b, o.a);
    setI(prog, "uUseTex", o.tex ? 1 : 0);
    setI(prog, "uLit", o.lit ? 1 : 0);
    glBindVertexArray(m.VAO);
    glDrawArrays(GL_TRIANGLES, 0, m.count);
}

// ======================================================================
//  4. TEKSTURA SA IMENOM I KURSOR
// ======================================================================
// Font 5x7, svaki red slova je 5 bita (bit 1 = upaljen piksel).
struct Glyph { char c; unsigned char rows[7]; };
static const Glyph FONT[] = {
    {' ', {0,0,0,0,0,0,0}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}}, {'-', {0,0,0,0x1F,0,0,0}},
    {'.', {0,0,0,0,0,0x0C,0x0C}},
};

// Nepoznat znak se prikazuje kao razmak
const unsigned char* findGlyph(char c) {
    c = (char)std::toupper((unsigned char)c);
    for (const Glyph& gl : FONT) if (gl.c == c) return gl.rows;
    return FONT[0].rows;
}

// Iz fonta generise RGBA sliku i salje je na GPU kao teksturu
unsigned int createTextTexture(const std::string& text, int& outW, int& outH) {
    const int S = 4;     // uvecanje: jedan piksel fonta = S x S piksela
    const int PAD = 6;
    int W = PAD * 2 + (int)text.size() * 6 * S - S;
    int H = PAD * 2 + 7 * S;
    std::vector<unsigned char> img(W * H * 4);
    for (int i = 0; i < W * H; ++i) {   // poluprovidna tamna pozadina
        img[i * 4 + 0] = 10; img[i * 4 + 1] = 15; img[i * 4 + 2] = 40; img[i * 4 + 3] = 170;
    }
    for (size_t ci = 0; ci < text.size(); ++ci) {
        const unsigned char* rows = findGlyph(text[ci]);
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c) {
                if (!((rows[r] >> (4 - c)) & 1)) continue;
                for (int dy = 0; dy < S; ++dy)
                    for (int dx = 0; dx < S; ++dx) {
                        int x = PAD + ((int)ci * 6 + c) * S + dx;
                        int yTop = PAD + r * S + dy;
                        int y = H - 1 - yTop;   // OpenGL: prvi red slike je donji
                        unsigned char* px = &img[(y * W + x) * 4];
                        px[0] = px[1] = px[2] = 255; px[3] = 255;
                    }
            }
    }
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    outW = W; outH = H;
    return tex;
}

// Kursor u obliku svetionika; hotspot je lampa
GLFWcursor* createLighthouseCursor() {
    const int N = 32;
    std::vector<unsigned char> px(N * N * 4, 0);
    auto put = [&](int x, int y, int r, int g, int b) {
        if (x < 0 || x >= N || y < 0 || y >= N) return;
        unsigned char* p = &px[(y * N + x) * 4];
        p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        };
    auto hline = [&](int y, int hw, int r, int g, int b) {
        for (int x = 16 - hw; x < 16 + hw; ++x) put(x, y, r, g, b);
        };
    for (int y = 0; y <= 4; ++y) hline(y, 1 + y, 150, 30, 30);         // krov
    for (int y = 5; y <= 8; ++y) hline(y, 3, 255, 225, 70);            // lampa
    for (int y = 9; y <= 10; ++y) hline(y, 6, 80, 80, 95);             // galerija
    for (int y = 11; y <= 29; ++y) {                                   // toranj sa prugama
        int hw = 4 + (y - 11) * 3 / 18;
        bool red = ((y - 11) / 4) % 2 == 0;
        if (red) hline(y, hw, 210, 35, 35); else hline(y, hw, 240, 240, 240);
    }
    for (int y = 30; y <= 31; ++y) hline(y, 10, 95, 95, 105);          // stena

    // Crna kontura radi vidljivosti na svetloj i tamnoj pozadini
    std::vector<unsigned char> copy = px;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            if (copy[(y * N + x) * 4 + 3]) continue;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < N && ny >= 0 && ny < N && copy[(ny * N + nx) * 4 + 3]) { near = true; break; }
                }
            if (near) put(x, y, 0, 0, 0);
        }
    GLFWimage img;
    img.width = N; img.height = N; img.pixels = px.data();
    return glfwCreateCursor(&img, 16, 6);
}

// ======================================================================
//  5. ULAZ: TASTATURA I MIS
// ======================================================================
void startStorm() {
    g.phase = STORM;
    g.stormTimer = 0.0f;
    g.powerFailed = false;
    g.backup = false;
    g.flashT = 99.0f;
    g.nextFlash = frand(1.5f, 3.0f);
    g.flickerOn = true;
    g.flickerSwitch = 0.0f;
}

void startDawn() {
    g.phase = DAWN;
    g.powerFailed = false;
    g.backup = false;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    else if (key == GLFW_KEY_O) {
        if (g.phase == CALM || dawnDone()) startStorm();
    }
    else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (g.phase == STORM) startDawn();
    }
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    // Pikseli -> NDC (Y osa je obrnuta)
    float x = (float)(mx / w) * 2.0f - 1.0f;
    float y = -((float)(my / h) * 2.0f - 1.0f);
    bool onBattery = x >= BAT_X0 - 0.012f && x <= BAT_X1 + 0.03f && y >= BAT_Y0 - 0.02f && y <= BAT_Y1 + 0.02f;
    // Rezerva se ukljucuje samo posle ispada napajanja
    if (onBattery && g.phase == STORM && g.powerFailed && !g.backup && g.battery > 0.0f) {
        g.backup = true;
    }
}

// ======================================================================
//  6. AZURIRANJE STANJA
// ======================================================================
// Glatko priblizavanje vrednosti cilju
float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::min(1.0f, rate * dt);
}

// Poziva se jednom po frejmu. Sve promene se mnoze sa dt (nezavisno od FPS-a).
void update(float dt, bool sHeld) {
    // Snop se u svim fazama okrece konstantnom brzinom
    g.beamAngle += BEAM_SPEED * dt;
    if (g.beamAngle > 2.0f * PI) g.beamAngle -= 2.0f * PI;

    // Brod ide ka cilju: mesto cekanja u oluji, luka u svitanju
    float target = (g.phase == DAWN) ? SHIP_DOCK_X : SHIP_WAIT_X;
    if (g.shipX < target) g.shipX = std::min(target, g.shipX + SHIP_SPEED * dt);

    g.flashT += dt;

    if (g.phase == CALM) {
        g.overlay = OVERLAY_NORMAL;
        g.beamIntensity = approach(g.beamIntensity, BEAM_NORMAL, 6.0f, dt);
    }
    else if (g.phase == STORM) {
        g.stormTimer += dt;
        g.overlay = std::min(OVERLAY_STORM, g.overlay + OVERLAY_RISE_RATE * dt);
        g.dawn = std::max(0.0f, g.dawn - DAWN_RATE * dt);

        // Munje u nasumicnim razmacima
        g.nextFlash -= dt;
        if (g.nextFlash <= 0.0f) { g.flashT = 0.0f; g.nextFlash = frand(2.5f, 6.0f); }

        // Glavno napajanje: normalno -> treperenje -> ispad
        if (!g.powerFailed) {
            if (g.stormTimer < FLICKER_START) {
                g.beamIntensity = BEAM_NORMAL;
            }
            else if (g.stormTimer < POWER_FAIL_TIME) {
                g.flickerSwitch -= dt;
                if (g.flickerSwitch <= 0.0f) {
                    // Sansa da je snop upaljen opada tokom treperenja
                    float p = 1.0f - (g.stormTimer - FLICKER_START) / (POWER_FAIL_TIME - FLICKER_START);
                    g.flickerOn = frand(0, 1) < p;
                    g.flickerSwitch = frand(0.04f, 0.2f);
                }
                g.beamIntensity = g.flickerOn ? BEAM_NORMAL : 0.05f;
            }
            else {
                g.powerFailed = true;
            }
        }
        // Posle ispada: rezerva (uz stednju na S) ili ugasen snop
        if (g.powerFailed) {
            if (g.backup) {
                float t = sHeld ? BEAM_SAVING : BEAM_BACKUP;
                g.beamIntensity = approach(g.beamIntensity, t, 6.0f, dt);
                g.battery -= (sHeld ? DRAIN_SAVING : DRAIN_BACKUP) * dt;
                if (g.battery <= 0.0f) { g.battery = 0.0f; g.backup = false; }
            }
            else {
                g.beamIntensity = approach(g.beamIntensity, 0.0f, 12.0f, dt);
            }
        }
    }
    else { // DAWN
        g.overlay = std::max(0.0f, g.overlay - DAWN_RATE * dt);
        g.dawn = std::min(1.0f, g.dawn + DAWN_RATE * dt);
        g.battery = std::min(1.0f, g.battery + 0.3f * dt);
        g.beamIntensity = approach(g.beamIntensity, BEAM_NORMAL, 3.0f, dt);
    }
}

// Jacina bljeska munje: brz pad uz drugi, slabiji bljesak
float lightningAlpha() {
    float t = g.flashT, a = 0.0f;
    if (t < 0.6f) {
        a = 0.55f * std::exp(-t * 14.0f);
        if (t > 0.16f) a += 0.4f * std::exp(-(t - 0.16f) * 18.0f);
    }
    return std::min(a, 0.8f);
}

// ======================================================================
//  7. GLAVNI PROGRAM
// ======================================================================
int main()
{
    // --- Prozor
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Svetionik", monitor, NULL);
    if (window == NULL) return endProgram("Prozor nije uspeo da se kreira.");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);   // bez vsync-a; FPS ogranicava sopstveni frame limiter

    if (glewInit() != GLEW_OK) return endProgram("GLEW nije uspeo da se inicijalizuje.");

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    g_aspect = (float)fbW / (float)fbH;

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);

    GLFWcursor* cursor = createLighthouseCursor();
    if (cursor) glfwSetCursor(window, cursor);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Sejderi: obicni objekti, sky nebo i more i snop
    unsigned int basicShader = createShader("basic.vert", "basic.frag");
    unsigned int skyShader = createShader("basic.vert", "sky.frag");
    unsigned int beamShader = createShader("basic.vert", "beam.frag");

    glUseProgram(basicShader);
    setI(basicShader, "uTex", 0);

    // --- Geometrija ---
    const float HORIZON = 0.10f;
    Mesh skyQuad, seaQuad, fullQuad, overlayQuad, flashQuad;
    { Builder b; b.rect(-1, HORIZON, 1, 1, { 1,1,1,1 }); skyQuad = makeMesh(b.v); }
    { Builder b; b.rect(-1, -1, 1, HORIZON, { 1,1,1,1 }); seaQuad = makeMesh(b.v); }
    { Builder b; b.rect(-1, -1, 1, 1, { 1,1,1,1 }); fullQuad = makeMesh(b.v); }         // za snop
    { Builder b; b.rect(-1, -1, 1, 1, { 0,0,0,1 }); overlayQuad = makeMesh(b.v); }      // tamni sloj
    { Builder b; b.rect(-1, -1, 1, 1, { 1,1,1,1 }); flashQuad = makeMesh(b.v); }        // munja

    // Zvezde i mesec
    Mesh starsMesh;
    {
        Builder b;
        std::srand(12345);   // fiksno seme: zvezde su uvek na istim mestima
        for (int i = 0; i < 70; ++i) {
            float x = frand(-1, 1), y = frand(HORIZON + 0.06f, 1.0f), s = frand(0.002f, 0.005f);
            b.rect(x - s / g_aspect, y - s, x + s / g_aspect, y + s, { 1, 1, 0.9f, 1 });
        }
        b.circle(-0.6f, 0.72f, 0.07f, { 0.95f, 0.95f, 0.8f, 1 });
        starsMesh = makeMesh(b.v);
        std::srand((unsigned)std::time(nullptr));
    }

    // Stena, pristaniste i svetionik (staticki mesh)
    Mesh sceneMesh;
    {
        Builder b;
        b.fan({ 0.82f, -0.5f }, {
            {0.50f,-1.0f}, {1.0f,-1.0f}, {1.0f,-0.02f}, {0.92f,0.02f}, {0.86f,-0.01f},
            {0.80f,0.01f}, {0.74f,-0.04f}, {0.68f,-0.10f}, {0.62f,-0.20f}, {0.57f,-0.38f},
            {0.53f,-0.60f}, {0.50f,-1.0f} }, { 0.30f, 0.27f, 0.25f, 1 });
        b.poly({ {0.62f,-0.20f}, {0.68f,-0.10f}, {0.72f,-0.35f}, {0.60f,-0.40f} }, { 0.22f, 0.20f, 0.19f, 1 });

        // Pristaniste
        Col wood{ 0.40f, 0.26f, 0.14f, 1 };
        b.rect(0.47f, -0.32f, 0.485f, -0.19f, wood);
        b.rect(0.54f, -0.32f, 0.555f, -0.19f, wood);
        b.rect(0.61f, -0.32f, 0.625f, -0.19f, wood);
        b.rect(0.46f, -0.19f, 0.68f, -0.16f, { 0.50f, 0.33f, 0.18f, 1 });

        // Svetionik: toranj od 8 prugastih trapeza, galerija, lampa, krov, gromobran
        const float cx = LAMP_X, yB = -0.04f, yT = 0.40f;
        const float hwB = 0.07f, hwT = 0.045f;
        const int stripes = 8;
        for (int i = 0; i < stripes; ++i) {
            float t0 = (float)i / stripes, t1 = (float)(i + 1) / stripes;
            float y0 = yB + (yT - yB) * t0, y1 = yB + (yT - yB) * t1;
            float h0 = hwB + (hwT - hwB) * t0, h1 = hwB + (hwT - hwB) * t1;
            Col c = (i % 2 == 0) ? Col{ 0.85f, 0.12f, 0.12f, 1 } : Col{ 0.93f, 0.93f, 0.93f, 1 };
            b.quad({ cx - h0, y0 }, { cx + h0, y0 }, { cx + h1, y1 }, { cx - h1, y1 }, c);
        }
        b.rect(cx - 0.065f, 0.40f, cx + 0.065f, 0.43f, { 0.25f, 0.25f, 0.30f, 1 });
        b.rect(cx - 0.036f, 0.43f, cx + 0.036f, 0.50f, { 0.15f, 0.15f, 0.18f, 1 });
        b.tri({ cx - 0.05f, 0.50f }, { cx + 0.05f, 0.50f }, { cx, 0.565f }, { 0.55f, 0.10f, 0.10f, 1 });
        b.rect(cx - 0.002f, 0.565f, cx + 0.002f, 0.60f, { 0.2f, 0.2f, 0.2f, 1 });
        sceneMesh = makeMesh(b.v);
    }

    // Staklo lampe (poseban mesh jer mu se boja menja sa intenzitetom snopa)
    Mesh lampMesh;
    { Builder b; b.circle(LAMP_X, LAMP_Y, 0.030f, { 1.0f, 0.95f, 0.55f, 1 }); lampMesh = makeMesh(b.v); }

    // Brod: crta se oko (0,0), a pozicija se zadaje preko uOffset pri crtanju
    Mesh shipMesh;
    {
        Builder b;
        b.poly({ {-0.11f,0.03f}, {-0.08f,-0.04f}, {0.08f,-0.04f}, {0.12f,0.03f} }, { 0.65f, 0.12f, 0.10f, 1 });
        b.rect(-0.10f, 0.012f, 0.11f, 0.024f, { 0.95f, 0.95f, 0.95f, 1 });
        b.rect(-0.05f, 0.03f, 0.05f, 0.08f, { 0.95f, 0.95f, 0.95f, 1 });
        for (int i = 0; i < 3; ++i)
            b.rect(-0.035f + i * 0.03f, 0.05f, -0.015f + i * 0.03f, 0.068f, { 0.15f, 0.25f, 0.45f, 1 });
        b.rect(-0.015f, 0.08f, 0.02f, 0.115f, { 0.95f, 0.75f, 0.10f, 1 });
        b.rect(-0.015f, 0.105f, 0.02f, 0.115f, { 0.10f, 0.10f, 0.10f, 1 });
        b.rect(0.065f, 0.03f, 0.07f, 0.13f, { 0.30f, 0.20f, 0.12f, 1 });
        b.tri({ 0.07f, 0.13f }, { 0.07f, 0.095f }, { 0.11f, 0.112f }, { 0.9f, 0.1f, 0.1f, 1 });
        shipMesh = makeMesh(b.v);
    }

    // Baterija i lampica
    Mesh batFrame, batInner, batNub, batFill, ledMesh;
    { Builder b; b.rect(BAT_X0 - 0.008f, BAT_Y0 - 0.012f, BAT_X1 + 0.008f, BAT_Y1 + 0.012f, { 1,1,1,1 }); batFrame = makeMesh(b.v); }
    { Builder b; b.rect(BAT_X0, BAT_Y0, BAT_X1, BAT_Y1, { 0.07f, 0.07f, 0.08f, 1 }); batInner = makeMesh(b.v); }
    {
        float my = (BAT_Y0 + BAT_Y1) * 0.5f;
        Builder b; b.rect(BAT_X1 + 0.008f, my - 0.02f, BAT_X1 + 0.025f, my + 0.02f, { 1,1,1,1 }); batNub = makeMesh(b.v);
    }
    {
        // Ispuna je uvek puna; suzava se preko uScale/uOffset pri crtanju
        Builder b; b.rect(BAT_X0 + BAT_PAD_X, BAT_Y0 + BAT_PAD_Y, BAT_X1 - BAT_PAD_X, BAT_Y1 - BAT_PAD_Y, { 0.10f, 0.85f, 0.20f, 1 });
        batFill = makeMesh(b.v);
    }
    { Builder b; b.circle(LED_X, LED_Y, LED_R, { 1,1,1,1 }); ledMesh = makeMesh(b.v); }

    // Natpis sa imenom
    int texW, texH;
    unsigned int nameTex = createTextTexture(name, texW, texH);
    Mesh nameMesh;
    {
        float hPx = fbH * 0.05f;                    // 5% visine ekrana
        float wPx = hPx * texW / (float)texH;       // cuva odnos stranica teksture
        float ndcW = 2.0f * wPx / fbW, ndcH = 2.0f * hPx / fbH;
        float m = 0.02f;
        Builder b; b.rect(1.0f - m - ndcW, -1.0f + m, 1.0f - m, -1.0f + m + ndcH, { 1,1,1,1 });
        nameMesh = makeMesh(b.v);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Boje neba i mora: noc (N) i svitanje (D), mesaju se po g.dawn
    const float skyTopN[3] = { 0.06f, 0.09f, 0.28f }, skyBotN[3] = { 0.14f, 0.20f, 0.45f };
    const float skyTopD[3] = { 0.35f, 0.50f, 0.80f }, skyBotD[3] = { 1.00f, 0.68f, 0.40f };
    const float seaTopN[3] = { 0.08f, 0.17f, 0.35f }, seaBotN[3] = { 0.04f, 0.09f, 0.22f };
    const float seaTopD[3] = { 0.40f, 0.55f, 0.65f }, seaBotD[3] = { 0.12f, 0.35f, 0.50f };
    auto mix3 = [](const float* a, const float* b, float t, float* o) { for (int i = 0; i < 3; ++i) o[i] = a[i] + (b[i] - a[i]) * t; };

    double lastTime = glfwGetTime();

    // --- Glavna petlja update -> crtanje -> frame limiter
    while (!glfwWindowShouldClose(window))
    {
        double frameStart = glfwGetTime();
        float dt = (float)std::min(0.1, frameStart - lastTime);
        lastTime = frameStart;

        bool sHeld = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;   // provera dok se taster drzi
        update(dt, sHeld);
        float time = (float)frameStart;
        float flash = lightningAlpha();

        glClear(GL_COLOR_BUFFER_BIT);

        // 1. Nebo
        float top[3], bot[3];
        glUseProgram(skyShader);
        mix3(skyTopN, skyTopD, g.dawn, top); mix3(skyBotN, skyBotD, g.dawn, bot);
        set3(skyShader, "uTop", top[0], top[1], top[2]);
        set3(skyShader, "uBottom", bot[0], bot[1], bot[2]);
        setF(skyShader, "uWave", 0.0f);
        setF(skyShader, "uTime", time);
        drawMesh(skyShader, skyQuad);

        // 2. Zvezde i mesec (nestaju u svitanje)
        { DrawOpts o; o.a = 1.0f - g.dawn; drawMesh(basicShader, starsMesh, o); }

        // 3. More (talasi jaci u oluji)
        float stormLevel = (g.overlay - OVERLAY_NORMAL) / (OVERLAY_STORM - OVERLAY_NORMAL);
        stormLevel = std::max(0.0f, std::min(1.0f, stormLevel));
        mix3(seaTopN, seaTopD, g.dawn, top); mix3(seaBotN, seaBotD, g.dawn, bot);
        glUseProgram(skyShader);
        set3(skyShader, "uTop", top[0], top[1], top[2]);
        set3(skyShader, "uBottom", bot[0], bot[1], bot[2]);
        setF(skyShader, "uWave", 0.05f + 0.10f * stormLevel);
        drawMesh(skyShader, seaQuad);

        // 4. Stena, luka, svetionik
        drawMesh(basicShader, sceneMesh);

        // 5. Tamni providni sloj
        { DrawOpts o; o.a = g.overlay; drawMesh(basicShader, overlayQuad, o); }

        // 6. Snop (aditivno mesanje, racuna se u beam.frag za svaki piksel)
        if (g.beamIntensity > 0.001f) {
            setBeamUniforms(beamShader, 0.0f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            drawMesh(beamShader, fullQuad);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // 7. Lampa (svetlija sa jacim snopom)
        {
            float br = 0.15f + 0.85f * std::min(1.0f, g.beamIntensity);
            DrawOpts o; o.r = br; o.g = br; o.b = br * 0.8f;
            drawMesh(basicShader, lampMesh, o);
        }

        // 8. Brod: vidljiv samo u snopu ili uz munju i svitanje
        {
            float ambient = 0.04f + 0.96f * std::max(0.0f, std::min(1.0f, 1.0f - g.overlay / OVERLAY_NORMAL));
            ambient = std::max(ambient, flash);
            setBeamUniforms(basicShader, ambient);
            DrawOpts o;
            float bob = std::sin(time * 1.5f) * (0.006f + 0.014f * stormLevel);   // njihanje
            o.ox = g.shipX; o.oy = SHIP_Y + bob; o.lit = true;
            drawMesh(basicShader, shipMesh, o);
        }

        // 9. Munja
        if (flash > 0.001f) { DrawOpts o; o.a = flash; drawMesh(basicShader, flashQuad, o); }

        // 10. Baterija i lampica stanja
        {
            bool clickable = g.phase == STORM && g.powerFailed && !g.backup && g.battery > 0.0f;
            float pulse = clickable ? (0.5f + 0.5f * std::sin(time * 6.0f)) : 1.0f;   // okvir pulsira kad je klik moguc
            DrawOpts f; f.r = 1.0f; f.g = 1.0f; f.b = clickable ? pulse : 1.0f;
            drawMesh(basicShader, batFrame, f);
            drawMesh(basicShader, batNub, f);
            drawMesh(basicShader, batInner);
            if (g.battery > 0.001f) {
                // Scale smanjuje ispunu, offset vraca levu ivicu na mesto (skaliranje je oko (0,0))
                float fx0 = BAT_X0 + BAT_PAD_X;
                DrawOpts o; o.sx = g.battery; o.ox = fx0 * (1.0f - g.battery);
                drawMesh(basicShader, batFill, o);
            }
            DrawOpts led;   // zelena: normalno, zuta: rezerva, crvena (trepti): ispad
            if (!g.powerFailed) { led.r = 0.1f; led.g = 0.9f; led.b = 0.2f; }
            else if (g.backup) { led.r = 1.0f; led.g = 0.85f; led.b = 0.1f; }
            else {
                bool on = std::fmod(time, 0.8f) < 0.4f;
                led.r = on ? 1.0f : 0.25f; led.g = 0.05f; led.b = 0.05f;
            }
            drawMesh(basicShader, ledMesh, led);
        }

        // 11. Natpis sa imenom
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, nameTex);
            DrawOpts o; o.tex = true; o.a = 0.7f;
            drawMesh(basicShader, nameMesh, o);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        // Frame limiter, sacekaj do kraja trajanja frejma
        double target = frameStart + FRAME_TIME;
        for (;;) {
            double rem = target - glfwGetTime();
            if (rem <= 0.0) break;
            if (rem > 0.002) std::this_thread::sleep_for(std::chrono::microseconds((long long)((rem - 0.001) * 1e6)));
        }
    }

    glfwDestroyCursor(cursor);
    glfwTerminate();
    return 0;
}