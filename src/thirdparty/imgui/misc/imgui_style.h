#ifndef IMGUI_STYLE_H
#define IMGUI_STYLE_H

enum class ImGuiStyle_t
{
    NONE = -1,
    DEFAULT,
    LEGACY,
    MODERN,
    OG
};

void ImGui_SetStyle(const ImGuiStyle_t style);

#endif // IMGUI_STYLE_H
