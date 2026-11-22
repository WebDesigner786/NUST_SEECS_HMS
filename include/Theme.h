#ifndef HMS_THEME_H
#define HMS_THEME_H

#include "imgui.h"
#include <cstdint>
#include <string>

namespace hms {

enum class ThemeMode {
    MidnightObsidian = 0,
    ExecutivePearl = 1
};

struct ThemePalette {
    ImVec4 background{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 panel{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 surface{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 hover{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 border{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 text{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 dimText{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 accent{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 accentHover{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 accentActive{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 secondaryAccent{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 goldHighlight{0.0f, 0.0f, 0.0f, 1.0f};

    // Status colors (shared across both themes)
    ImVec4 statusAvailable{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 statusOccupied{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 statusReserved{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 statusCleaning{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 statusMaintenance{0.0f, 0.0f, 0.0f, 1.0f};

    // Tier colors (shared across both themes)
    ImVec4 tierStandard{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 tierSilver{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 tierGold{0.0f, 0.0f, 0.0f, 1.0f};
    ImVec4 tierPlatinum{0.0f, 0.0f, 0.0f, 1.0f};
};

struct FontBank {
    ImFont* bodyFont = nullptr;
    ImFont* headingFont = nullptr;
    ImFont* captionFont = nullptr;
    std::string loadedFontPath = "ImGui Default (ProggyClean)";
    bool loadedSystemFont = false;
};

class ThemeEngine {
public:
    ThemeEngine();

    // Initializes layout metrics and sets initial theme immediately without transition
    void initialize(ThemeMode initialMode = ThemeMode::MidnightObsidian);

    // Toggles between Midnight Obsidian and Executive Pearl with a 250ms cross-fade
    void toggleTheme();

    // Sets target theme explicitly (with 250ms transition if different)
    void setTheme(ThemeMode target);

    // Advances the 250ms color lerp each frame and writes to ImGui::GetStyle()
    void update(float deltaTimeSeconds);

    // Loads 19px body, 30px heading, and 15px caption (times content scale) fonts from system font paths with fallback
    FontBank loadFonts(ImGuiIO& io, float contentScale = 1.0f);

    ThemeMode getCurrentMode() const;
    const char* getCurrentModeName() const;
    const ThemePalette& getActivePalette() const;
    float getTransitionProgress() const;

    // Utility color conversion helpers
    static ImVec4 hexToVec4(uint32_t rgbHex, float alpha = 1.0f);
    static ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t);
    static ImU32 vec4ToU32(const ImVec4& c);

private:
    static ThemePalette makeMidnightObsidian();
    static ThemePalette makeExecutivePearl();
    static ThemePalette lerpPalette(const ThemePalette& a, const ThemePalette& b, float t);
    void applyMetricsToImGuiStyle(ImGuiStyle& style) const;
    void applyPaletteToImGuiStyle(const ThemePalette& p, ImGuiStyle& style) const;

    ThemePalette m_darkPalette{};
    ThemePalette m_lightPalette{};
    ThemePalette m_startPalette{};
    ThemePalette m_currentPalette{};
    ThemeMode m_targetMode = ThemeMode::MidnightObsidian;
    float m_transitionElapsed = 0.250f;
    static constexpr float kTransitionDuration = 0.250f;
};

} // namespace hms

#endif // HMS_THEME_H
