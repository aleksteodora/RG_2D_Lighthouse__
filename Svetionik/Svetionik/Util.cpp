#include "Util.h";
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define _CRT_SECURE_NO_WARNINGS
#include <fstream>
#include <sstream>
#include <iostream>

// Ucitava izvorni kod sejdera iz fajla na putanji "source", kompajlira ga
// i vraca ID sejdera zadatog tipa (GL_VERTEX_SHADER ili GL_FRAGMENT_SHADER).
unsigned int compileShader(GLenum type, const char* source)
{
    // --- Citanje izvornog koda iz fajla ---
    std::string content = "";
    std::ifstream file(source);
    std::stringstream ss;
    if (file.is_open())
    {
        ss << file.rdbuf();
        file.close();
        std::cout << "Uspesno procitao fajl sa putanje \"" << source << "\"!" << std::endl;
    }
    else {
        ss << "";
        std::cout << "Greska pri citanju fajla sa putanje \"" << source << "\"!" << std::endl;
    }
    std::string temp = ss.str();
    const char* sourceCode = temp.c_str();   // izvorni kod sejdera

    // --- Kreiranje i kompajliranje sejdera ---
    int shader = glCreateShader(type);       // prazan sejder zadatog tipa

    int success;                             // GL_TRUE ako je kompajliranje uspelo
    char infoLog[512];                       // poruka o gresci
    glShaderSource(shader, 1, &sourceCode, NULL);
    glCompileShader(shader);

    // --- Provera greske pri kompajliranju ---
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        if (type == GL_VERTEX_SHADER)
            printf("VERTEX");
        else if (type == GL_FRAGMENT_SHADER)
            printf("FRAGMENT");
        printf(" sejder ima gresku! Greska: \n");
        printf(infoLog);
    }
    return shader;
}

// Pravi objedinjeni sejder program od vertex sejdera (putanja vsSource)
// i fragment sejdera (putanja fsSource); vraca ID programa
unsigned int createShader(const char* vsSource, const char* fsSource)
{
    unsigned int program;          // objedinjeni sejder program
    unsigned int vertexShader;     // vertex sejder (pozicije temena)
    unsigned int fragmentShader;   // fragment sejder (boje, teksture...)

    program = glCreateProgram();

    // Kompajliranje oba sejdera
    vertexShader = compileShader(GL_VERTEX_SHADER, vsSource);
    fragmentShader = compileShader(GL_FRAGMENT_SHADER, fsSource);

    // Povezivanje sejdera u jedan program
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);
    glValidateProgram(program);

    // Provera programa
    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_VALIDATE_STATUS, &success);
    if (success == GL_FALSE)
    {
        glGetShaderInfoLog(program, 512, NULL, infoLog);
        std::cout << "Objedinjeni sejder ima gresku! Greska: \n";
        std::cout << infoLog << std::endl;
    }

    // Pojedinacni sejderi vise nisu potrebni jer su ugradjeni u program
    glDetachShader(program, vertexShader);
    glDeleteShader(vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(fragmentShader);

    return program;
}