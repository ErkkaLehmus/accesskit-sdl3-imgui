# accesskit-sdl3-imgui
Minimal test to utilize both ImGui and AccessKit on top of SDL3

AccessKit makes it easier to implement accessibility, for screen readers and other assistive technologies, in toolkits that render their own user interface elements. It provides a cross-platform, cross-language abstraction over accessibility APIs, so toolkit developers only have to implement accessibility once. For more information see https://github.com/AccessKit/accesskit

AccessKit has bindings for C language, with an example using SDL2, see https://github.com/AccessKit/accesskit-c/tree/main/examples/sdl

This project is based on the above mentioned example, migrated to using SDL3. I have also added a few simple visual GUI widgets using Dear ImGui, just to test how to make the visual widgets accessible. For Dear ImGui see https://github.com/ocornut/imgui

Feel free to copy, improve, modify and utilize the example code.
