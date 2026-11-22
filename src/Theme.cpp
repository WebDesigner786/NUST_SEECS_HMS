#include "Theme.h"
#include <algorithm>
#include <filesystem>

namespace hms {

/*
 * Purpose: Constructs the ThemeEngine with precomputed Midnight Obsidian and Executive Pearl palettes.
 * Inputs: None.
 * Outputs: Initialized ThemeEngine instance.
 * Big-O: O(1) time, O(1) space.
 */
ThemeEngine::ThemeEngine()
    : m_darkPalette(makeMidnightObsidian()),
      m_lightPalette(makeExecutivePearl()),
      m_startPalette(m_darkPalette),
      m_currentPalette(m_darkPalette),
      m_targetMode(ThemeMode::MidnightObsidian),
      m_transitionElapsed(kTransitionDuration) {}

/*
 * Purpose: Converts a 24-bit hexadecimal RGB value (e.g. 0x0B0E14) to an ImVec4 normalized color.
 * Inputs: rgbHex (24-bit RGB integer), alpha (opacity in [0.0, 1.0]).
 * Outputs: Normalized ImVec4(r, g, b, a).
 * Big-O: O(1) time, O(1) space.
 */
ImVec4 ThemeEngine::hexToVec4(uint32_t rgbHex, float alpha) {
    const float r = static_cast<float>((rgbHex >> 16) & 0xFFU) / 255.0f;
    const float g = static_cast<float>((rgbHex >> 8) & 0xFFU) / 255.0f;
    const float b = static_cast<float>(rgbHex & 0xFFU) / 255.0f;
    return ImVec4(r, g, b, alpha);
}

/*
 * Purpose: Linearly interpolates between two ImVec4 colors by factor t in [0.0, 1.0].
 * Inputs: Color a, Color b, interpolation factor t.
 * Outputs: Blended ImVec4 color.
 * Big-O: O(1) time, O(1) space.
 */
ImVec4 ThemeEngine::lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        a.x + (b.x - a.x) * clampedT,
        a.y + (b.y - a.y) * clampedT,
        a.z + (b.z - a.z) * clampedT,
        a.w + (b.w - a.w) * clampedT
    );
}

/*
 * Purpose: Converts an ImVec4 color to a packed 32-bit ImU32 color for ImDrawList primitives.
 * Inputs: Normalized ImVec4 color c.
 * Outputs: Packed ImU32 ABGR value.
 * Big-O: O(1) time, O(1) space.
 */
ImU32 ThemeEngine::vec4ToU32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

/*
 * Purpose: Builds the exact Midnight Obsidian (dark) palette required by Section 8.
 * Inputs: None.
 * Outputs: Populated ThemePalette struct.
 * Big-O: O(1) time, O(1) space.
 */
ThemePalette ThemeEngine::makeMidnightObsidian() {
    ThemePalette p{};
    p.background        = hexToVec4(0x0B0E14);
    p.panel             = hexToVec4(0x121722);
    p.surface           = hexToVec4(0x1A2030);
    p.hover             = hexToVec4(0x232B40);
    p.border            = hexToVec4(0x2A3248);
    p.text              = hexToVec4(0xE6EAF2);
    p.dimText           = hexToVec4(0x8B93A7);
    p.accent            = hexToVec4(0x6C8CFF);
    p.accentHover       = hexToVec4(0x8AA4FF);
    p.accentActive      = hexToVec4(0x4F6FE8);
    p.secondaryAccent   = hexToVec4(0x22D3EE);
    p.goldHighlight     = hexToVec4(0xF5B942);

    p.statusAvailable   = hexToVec4(0x10B981);
    p.statusOccupied    = hexToVec4(0xEF4444);
    p.statusReserved    = hexToVec4(0xF59E0B);
    p.statusCleaning    = hexToVec4(0x8B5CF6);
    p.statusMaintenance = hexToVec4(0x64748B);

    p.tierStandard      = hexToVec4(0x94A3B8);
    p.tierSilver        = hexToVec4(0xCBD5E1);
    p.tierGold          = hexToVec4(0xFBBF24);
    p.tierPlatinum      = hexToVec4(0xA78BFA);
    return p;
}

/*
 * Purpose: Builds the exact Executive Pearl (light) palette required by Section 8.
 * Inputs: None.
 * Outputs: Populated ThemePalette struct.
 * Big-O: O(1) time, O(1) space.
 */
ThemePalette ThemeEngine::makeExecutivePearl() {
    ThemePalette p{};
    p.background        = hexToVec4(0xF4F6FA);
    p.panel             = hexToVec4(0xFFFFFF);
    p.surface           = hexToVec4(0xEDF0F6);
    p.hover             = hexToVec4(0xE2E7F2);
    p.border            = hexToVec4(0xD5DAE6);
    p.text              = hexToVec4(0x1B2233);
    p.dimText           = hexToVec4(0x66708A);
    p.accent            = hexToVec4(0x3B5BDB);
    p.accentHover       = hexToVec4(0x4C6EF5);
    p.accentActive      = hexToVec4(0x2F4AB8);
    p.secondaryAccent   = hexToVec4(0x0891B2);
    p.goldHighlight     = hexToVec4(0xD97706);

    p.statusAvailable   = hexToVec4(0x10B981);
    p.statusOccupied    = hexToVec4(0xEF4444);
    p.statusReserved    = hexToVec4(0xF59E0B);
    p.statusCleaning    = hexToVec4(0x8B5CF6);
    p.statusMaintenance = hexToVec4(0x64748B);

    p.tierStandard      = hexToVec4(0x94A3B8);
    p.tierSilver        = hexToVec4(0xCBD5E1);
    p.tierGold          = hexToVec4(0xFBBF24);
    p.tierPlatinum      = hexToVec4(0xA78BFA);
    return p;
}

/*
 * Purpose: Interpolates every color field between two ThemePalette structs.
 * Inputs: Palette a, Palette b, factor t in [0.0, 1.0].
 * Outputs: Interpolated ThemePalette.
 * Big-O: O(1) time, O(1) space.
 */
ThemePalette ThemeEngine::lerpPalette(const ThemePalette& a, const ThemePalette& b, float t) {
    ThemePalette out{};
    out.background        = lerpColor(a.background, b.background, t);
    out.panel             = lerpColor(a.panel, b.panel, t);
    out.surface           = lerpColor(a.surface, b.surface, t);
    out.hover             = lerpColor(a.hover, b.hover, t);
    out.border            = lerpColor(a.border, b.border, t);
    out.text              = lerpColor(a.text, b.text, t);
    out.dimText           = lerpColor(a.dimText, b.dimText, t);
    out.accent            = lerpColor(a.accent, b.accent, t);
    out.accentHover       = lerpColor(a.accentHover, b.accentHover, t);
    out.accentActive      = lerpColor(a.accentActive, b.accentActive, t);
    out.secondaryAccent   = lerpColor(a.secondaryAccent, b.secondaryAccent, t);
    out.goldHighlight     = lerpColor(a.goldHighlight, b.goldHighlight, t);

    out.statusAvailable   = lerpColor(a.statusAvailable, b.statusAvailable, t);
    out.statusOccupied    = lerpColor(a.statusOccupied, b.statusOccupied, t);
    out.statusReserved    = lerpColor(a.statusReserved, b.statusReserved, t);
    out.statusCleaning    = lerpColor(a.statusCleaning, b.statusCleaning, t);
    out.statusMaintenance = lerpColor(a.statusMaintenance, b.statusMaintenance, t);

    out.tierStandard      = lerpColor(a.tierStandard, b.tierStandard, t);
    out.tierSilver        = lerpColor(a.tierSilver, b.tierSilver, t);
    out.tierGold          = lerpColor(a.tierGold, b.tierGold, t);
    out.tierPlatinum      = lerpColor(a.tierPlatinum, b.tierPlatinum, t);
    return out;
}

/*
 * Purpose: Applies the exact rounding, padding, spacing, border and AA metrics from Section 8.
 * Inputs: Reference to ImGuiStyle.
 * Outputs: Modifies style metrics in-place.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::applyMetricsToImGuiStyle(ImGuiStyle& style) const {
    style.WindowRounding    = 12.0f;
    style.FrameRounding     = 8.0f;
    style.ChildRounding     = 10.0f;
    style.PopupRounding     = 10.0f;
    style.GrabRounding      = 8.0f;
    style.TabRounding       = 8.0f;
    style.ScrollbarRounding = 12.0f;

    style.FramePadding      = ImVec2(12.0f, 8.0f);
    style.ItemSpacing       = ImVec2(10.0f, 10.0f);
    style.WindowPadding     = ImVec2(18.0f, 18.0f);

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    style.AntiAliasedLines       = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill        = true;
}

/*
 * Purpose: Maps all ThemePalette colors onto ImGuiStyle::Colors array.
 * Inputs: ThemePalette p, reference to ImGuiStyle.
 * Outputs: Updates ImGui color table in-place.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::applyPaletteToImGuiStyle(const ThemePalette& p, ImGuiStyle& style) const {
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = p.text;
    colors[ImGuiCol_TextDisabled]          = p.dimText;
    colors[ImGuiCol_WindowBg]              = p.background;
    colors[ImGuiCol_ChildBg]               = p.panel;
    colors[ImGuiCol_PopupBg]               = p.panel;
    colors[ImGuiCol_Border]                = p.border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = p.surface;
    colors[ImGuiCol_FrameBgHovered]        = p.hover;
    colors[ImGuiCol_FrameBgActive]         = p.accentActive;
    colors[ImGuiCol_TitleBg]               = p.panel;
    colors[ImGuiCol_TitleBgActive]         = p.surface;
    colors[ImGuiCol_TitleBgCollapsed]      = p.panel;
    colors[ImGuiCol_MenuBarBg]             = p.panel;
    colors[ImGuiCol_ScrollbarBg]           = p.background;
    colors[ImGuiCol_ScrollbarGrab]         = p.surface;
    colors[ImGuiCol_ScrollbarGrabHovered]  = p.hover;
    colors[ImGuiCol_ScrollbarGrabActive]   = p.accent;
    colors[ImGuiCol_CheckMark]             = p.accent;
    colors[ImGuiCol_SliderGrab]            = p.accent;
    colors[ImGuiCol_SliderGrabActive]      = p.accentActive;
    colors[ImGuiCol_Button]                = p.accent;
    colors[ImGuiCol_ButtonHovered]         = p.accentHover;
    colors[ImGuiCol_ButtonActive]          = p.accentActive;
    colors[ImGuiCol_Header]                = p.surface;
    colors[ImGuiCol_HeaderHovered]         = p.hover;
    colors[ImGuiCol_HeaderActive]          = p.accentActive;
    colors[ImGuiCol_Separator]             = p.border;
    colors[ImGuiCol_SeparatorHovered]      = p.accentHover;
    colors[ImGuiCol_SeparatorActive]       = p.accentActive;
    colors[ImGuiCol_ResizeGrip]            = p.surface;
    colors[ImGuiCol_ResizeGripHovered]     = p.accentHover;
    colors[ImGuiCol_ResizeGripActive]      = p.accentActive;
    colors[ImGuiCol_Tab]                   = p.surface;
    colors[ImGuiCol_TabHovered]            = p.hover;
    colors[ImGuiCol_TabSelected]           = p.accent;
    colors[ImGuiCol_TabDimmed]             = p.panel;
    colors[ImGuiCol_TabDimmedSelected]     = p.surface;
    colors[ImGuiCol_DockingPreview]        = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.55f);
    colors[ImGuiCol_DockingEmptyBg]        = p.background;
    colors[ImGuiCol_PlotLines]             = p.accent;
    colors[ImGuiCol_PlotLinesHovered]      = p.secondaryAccent;
    colors[ImGuiCol_PlotHistogram]         = p.goldHighlight;
    colors[ImGuiCol_PlotHistogramHovered]  = p.accentHover;
    colors[ImGuiCol_TableHeaderBg]         = p.surface;
    colors[ImGuiCol_TableBorderStrong]     = p.border;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(p.border.x, p.border.y, p.border.z, 0.55f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(p.surface.x, p.surface.y, p.surface.z, 0.35f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = p.secondaryAccent;
    colors[ImGuiCol_NavCursor]             = p.accent;
    colors[ImGuiCol_NavWindowingHighlight] = p.accent;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
}

/*
 * Purpose: Initializes ImGui style metrics and immediately applies the requested initial theme.
 * Inputs: initialMode (MidnightObsidian or ExecutivePearl).
 * Outputs: Configures ImGui::GetStyle() in-place.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::initialize(ThemeMode initialMode) {
    m_targetMode = initialMode;
    m_currentPalette = (initialMode == ThemeMode::MidnightObsidian) ? m_darkPalette : m_lightPalette;
    m_startPalette = m_currentPalette;
    m_transitionElapsed = kTransitionDuration;

    ImGuiStyle& style = ImGui::GetStyle();
    applyMetricsToImGuiStyle(style);
    applyPaletteToImGuiStyle(m_currentPalette, style);
}

/*
 * Purpose: Starts a 250ms cross-fade transition to the opposite theme.
 * Inputs: None.
 * Outputs: Flips m_targetMode and resets transition timer.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::toggleTheme() {
    setTheme(m_targetMode == ThemeMode::MidnightObsidian
                 ? ThemeMode::ExecutivePearl
                 : ThemeMode::MidnightObsidian);
}

/*
 * Purpose: Sets the target theme and begins a 250ms cross-fade if different from current target.
 * Inputs: target ThemeMode.
 * Outputs: Snapshots current palette as start palette and resets transition timer.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::setTheme(ThemeMode target) {
    if (m_targetMode == target) {
        return;
    }
    m_startPalette = m_currentPalette;
    m_targetMode = target;
    m_transitionElapsed = 0.0f;
}

/*
 * Purpose: Advances the 250ms theme cross-fade animation each frame using smooth cubic ease-out.
 * Inputs: deltaTimeSeconds (frame delta time from ImGuiIO).
 * Outputs: Updates m_currentPalette and ImGui::GetStyle() colors.
 * Big-O: O(1) time, O(1) space.
 */
void ThemeEngine::update(float deltaTimeSeconds) {
    if (m_transitionElapsed < kTransitionDuration) {
        m_transitionElapsed = std::min(kTransitionDuration, m_transitionElapsed + std::max(0.0f, deltaTimeSeconds));
        const float rawT = m_transitionElapsed / kTransitionDuration;
        const float inv = 1.0f - rawT;
        const float smoothT = 1.0f - (inv * inv * inv);

        const ThemePalette& targetPalette =
            (m_targetMode == ThemeMode::MidnightObsidian) ? m_darkPalette : m_lightPalette;
        m_currentPalette = lerpPalette(m_startPalette, targetPalette, smoothT);
        applyPaletteToImGuiStyle(m_currentPalette, ImGui::GetStyle());
    }
}

/*
 * Purpose: Attempts to load system TTF fonts (19px body, 30px heading, 15px caption, times the display content scale) in Section 2 order,
 *          falling back safely to ImGui's built-in default font if none exist.
 * Inputs: Reference to ImGuiIO.
 * Outputs: FontBank containing pointers to bodyFont, headingFont, captionFont and status metadata.
 * Big-O: O(F) where F is the number of candidate font paths (3 candidates).
 */
FontBank ThemeEngine::loadFonts(ImGuiIO& io, float contentScale) {
    FontBank bank{};
    const float sc = std::clamp(contentScale, 1.0f, 3.0f);
    // Persistent glyph ranges: Latin-1, general punctuation (dashes, quotes, bullets), arrows, minus sign.
    static const ImWchar kGlyphRanges[] = {
        0x0020, 0x00FF,
        0x2010, 0x2027,
        0x2030, 0x203A,
        0x2190, 0x2193,
        0x2212, 0x2212,
        0
    };
    const char* candidatePaths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };

    std::string chosenPath;
    for (const char* path : candidatePaths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            chosenPath = path;
            break;
        }
    }

    if (!chosenPath.empty()) {
        ImFontConfig cfgBody{};
        cfgBody.OversampleH = 3;
        cfgBody.OversampleV = 2;
        cfgBody.PixelSnapH = true;
        cfgBody.GlyphRanges = kGlyphRanges;

        ImFontConfig cfgHeading{};
        cfgHeading.OversampleH = 3;
        cfgHeading.OversampleV = 2;
        cfgHeading.PixelSnapH = true;
        cfgHeading.GlyphRanges = kGlyphRanges;

        ImFontConfig cfgCaption{};
        cfgCaption.OversampleH = 3;
        cfgCaption.OversampleV = 2;
        cfgCaption.PixelSnapH = true;
        cfgCaption.GlyphRanges = kGlyphRanges;

        bank.bodyFont    = io.Fonts->AddFontFromFileTTF(chosenPath.c_str(), 19.0f * sc, &cfgBody);
        bank.headingFont = io.Fonts->AddFontFromFileTTF(chosenPath.c_str(), 30.0f * sc, &cfgHeading);
        bank.captionFont = io.Fonts->AddFontFromFileTTF(chosenPath.c_str(), 15.0f * sc, &cfgCaption);

        if (bank.bodyFont != nullptr && bank.headingFont != nullptr && bank.captionFont != nullptr) {
            io.FontDefault = bank.bodyFont;
            bank.loadedFontPath = chosenPath;
            bank.loadedSystemFont = true;
            return bank;
        }
        io.Fonts->Clear();
    }

    ImFont* fallback = io.Fonts->AddFontDefault();
    bank.bodyFont = fallback;
    bank.headingFont = fallback;
    bank.captionFont = fallback;
    io.FontDefault = fallback;
    bank.loadedFontPath = "ImGui Default (Fallback)";
    bank.loadedSystemFont = false;
    return bank;
}

/*
 * Purpose: Returns the currently targeted ThemeMode enum.
 * Inputs: None.
 * Outputs: ThemeMode::MidnightObsidian or ThemeMode::ExecutivePearl.
 * Big-O: O(1) time, O(1) space.
 */
ThemeMode ThemeEngine::getCurrentMode() const {
    return m_targetMode;
}

/*
 * Purpose: Returns the human-readable display name of the active theme.
 * Inputs: None.
 * Outputs: C-string theme name.
 * Big-O: O(1) time, O(1) space.
 */
const char* ThemeEngine::getCurrentModeName() const {
    return (m_targetMode == ThemeMode::MidnightObsidian)
               ? "Midnight Obsidian (Dark)"
               : "Executive Pearl (Light)";
}

/*
 * Purpose: Returns a const reference to the current interpolated ThemePalette.
 * Inputs: None.
 * Outputs: Const reference to m_currentPalette.
 * Big-O: O(1) time, O(1) space.
 */
const ThemePalette& ThemeEngine::getActivePalette() const {
    return m_currentPalette;
}

/*
 * Purpose: Returns the normalized [0.0, 1.0] progress of the 250ms theme cross-fade.
 * Inputs: None.
 * Outputs: Float in [0.0, 1.0].
 * Big-O: O(1) time, O(1) space.
 */
float ThemeEngine::getTransitionProgress() const {
    return std::clamp(m_transitionElapsed / kTransitionDuration, 0.0f, 1.0f);
}

} // namespace hms
