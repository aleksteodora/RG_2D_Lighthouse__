![C++](https://img.shields.io/badge/C++-00599C?logo=cplusplus&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3-5586A4)
![GLSL](https://img.shields.io/badge/Shaders-GLSL-5586A4)
![GLFW](https://img.shields.io/badge/GLFW-3-orange)
![GLEW](https://img.shields.io/badge/GLEW-2-blue)

# Lighthouse 2D OpenGL Scene

A 2D interactive scene of a lighthouse during a storm, written in C++ with
OpenGL 3.3 (core profile), GLFW and GLEW.

A rotating light beam guides a ship towards the harbour. When the storm hits,
the main power flickers and fails, and the player has to switch on the backup
battery and ration its charge until dawn.

## Screenshots

<table>
  <tr>
    <td align="center"><b>Calm night</b><br><img src="screenshots/calm.png" width="400"></td>
    <td align="center"><b>Rotating beam</b><br><img src="screenshots/beam.png" width="400"></td>
  </tr>
  <tr>
    <td align="center"><b>Power outage</b><br><img src="screenshots/outage.png" width="400"></td>
    <td align="center"><b>Backup power</b><br><img src="screenshots/backup.png" width="400"></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><b>Dawn</b><br><img src="screenshots/dawn.png" width="400"></td>
  </tr>
</table>

## Features

- Rotating lighthouse beam computed per pixel in a fragment shader
- Ship visible only while lit by the beam (or by lightning and dawn light)
- Storm: darkening sky, animated waves, random lightning, flickering power
- Backup battery with a live charge indicator and a status LED
  (green: normal, red blinking: outage, yellow: backup)
- Smooth night-to-dawn transition
- Custom lighthouse mouse cursor, drawn in code
- Name label rendered from a texture generated at runtime (5x7 bitmap font)
- Frame limiter at 75 FPS, with time-based (delta time) movement

All geometry is built from triangles in code; no external models or images
are used.

## Project structure

```
Main.cpp      application, scene logic and rendering
Util.h/.cpp   shader loading helpers
basic.vert    vertex shader shared by all programs
basic.frag    general shader (colour, texture, beam lighting for the ship)
sky.frag      sky and sea gradient with waves
beam.frag     lighthouse beam
```

## Controls

| Input | Action |
|---|---|
| `O` | Start the storm (from the calm state or after dawn) |
| `Enter` | Bring the dawn (during the storm) |
| Left click on the battery | Switch on backup power (only after the outage) |
| `S` (hold) | Power saving: dimmer beam, battery drains 3x slower |
| `Esc` | Exit |

## Build and run (Windows, Visual Studio)

1. Install Visual Studio with the *Desktop development with C++* workload.
2. Create an empty C++ project and add the files from this repository
   (`Util.h` under Header Files, the rest under Source Files).
3. Project → Properties → Linker → Input → Additional Dependencies: add
   `opengl32.lib;` at the beginning.
4. Project → Manage NuGet Packages: install **GLFW** and **GLEW**.
5. Make sure `basic.vert`, `basic.frag`, `sky.frag` and `beam.frag` are in the
   project folder (the program loads them from the working directory).
6. Build and run (x64). The app starts in fullscreen; press `Esc` to quit.

Requires a GPU with OpenGL 3.3 support.