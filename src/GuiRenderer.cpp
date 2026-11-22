#include "GuiRenderer.h"
#include "Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>

namespace hms {

namespace {

constexpr float kPi = 3.1415926535f;

/*
 * Purpose: Cubic ease-out interpolation function f(t) = 1 - (1 - t)^3 for smooth UI animations.
 * Inputs: t in [0.0, 1.0].
 * Outputs: Eased value in [0.0, 1.0].
 * Big-O: O(1) time, O(1) space.
 */
float easeOutCubic(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - c;
    return 1.0f - (inv * inv * inv);
}

/*
 * Purpose: Returns current local wall-clock time formatted as "HH:MM:SS".
 * Inputs: None.
 * Outputs: Formatted std::string.
 * Big-O: O(1) time, O(1) space.
 */
std::string currentClockString() {
    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    return std::string(buf);
}

/*
 * Purpose: Extracts up to 2 uppercase initials from a user's display name for the avatar circle.
 * Inputs: displayName string.
 * Outputs: 1-2 character uppercase initials string.
 * Big-O: O(N) where N = displayName.size().
 */
std::string extractInitials(const std::string& displayName) {
    std::string out;
    bool takeNext = true;
    for (char ch : displayName) {
        if (ch == ' ' || ch == '-') {
            takeNext = true;
        } else if (takeNext) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            takeNext = false;
            if (out.size() == 2U) {
                break;
            }
        }
    }
    return out.empty() ? "U" : out;
}

} // namespace

// =============================================================================
// 1. TOAST NOTIFICATION MANAGER (Section 8)
// =============================================================================
/*
 * Purpose: Returns the static toast queue vector.
 * Inputs: None.
 * Outputs: Reference to std::vector<ToastItem>.
 * Big-O: O(1) time, O(1) space.
 */
std::vector<ToastItem>& Toast::items() {
    static std::vector<ToastItem> s_toasts;
    return s_toasts;
}

/*
 * Purpose: Returns the monotonically increasing toast ID counter.
 * Inputs: None.
 * Outputs: Reference to int counter.
 * Big-O: O(1) time, O(1) space.
 */
int& Toast::nextId() {
    static int s_nextId = 1;
    return s_nextId;
}

/*
 * Purpose: Pushes a new toast notification to the top-right stack (max 5 visible per Section 8).
 * Inputs: type (Success/Info/Warning/Error), title, message, optional waitlist/room IDs.
 * Outputs: Appends ToastItem to static stack.
 * Big-O: O(1) time, O(1) space.
 */
void Toast::push(
    NoticeType type,
    const std::string& title,
    const std::string& message,
    int relatedWaitlistId,
    int relatedRoomId
) {
    auto& q = items();
    if (q.size() >= 5U) {
        q.erase(q.begin());
    }
    ToastItem item{};
    item.id                = nextId()++;
    item.type              = type;
    item.title             = title;
    item.message           = message;
    item.elapsed           = 0.0f;
    item.duration          = 4.0f;
    item.dismissed         = false;
    item.relatedWaitlistId = relatedWaitlistId;
    item.relatedRoomId     = relatedRoomId;
    q.push_back(std::move(item));
}

/*
 * Purpose: Clears all active toasts.
 * Inputs: None.
 * Outputs: Empties static toast vector.
 * Big-O: O(1) time, O(1) space.
 */
void Toast::clear() {
    items().clear();
}

/*
 * Purpose: Updates timers and renders up to 5 stacked 340x72 toast cards at top-right:
 *          slide-in from right over 250ms (ease-out), colored left accent bar, 4s auto-dismiss with
 *          shrinking progress line, click to dismiss, fade-out last 300ms (Section 8).
 * Inputs: deltaTime, palette, fonts.
 * Outputs: Renders overlay toast windows and prunes expired/dismissed items.
 * Big-O: O(T) where T <= 5.
 */
void Toast::updateAndRender(float deltaTime, const ThemePalette& palette, const FontBank& fonts) {
    auto& q = items();
    if (q.empty()) {
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kCardW   = 340.0f;
    constexpr float kCardH   = 72.0f;
    constexpr float kMargin  = 16.0f;
    constexpr float kGap     = 10.0f;
    constexpr float kTopOff  = 66.0f;

    for (std::size_t i = 0U; i < q.size(); ++i) {
        ToastItem& t = q[i];
        t.elapsed += std::max(0.0f, deltaTime);

        const float slideT = easeOutCubic(t.elapsed / 0.25f);
        float alpha = 1.0f;
        const float timeRemaining = t.duration - t.elapsed;
        if (timeRemaining < 0.30f) {
            alpha = std::clamp(timeRemaining / 0.30f, 0.0f, 1.0f);
        }

        const float xTarget = vp->Pos.x + vp->Size.x - kCardW - kMargin;
        const float xStart  = vp->Pos.x + vp->Size.x + 24.0f;
        const float curX    = xStart + (xTarget - xStart) * slideT;
        const float curY    = vp->Pos.y + kTopOff + static_cast<float>(i) * (kCardH + kGap);

        ImVec4 accentCol = ThemeEngine::hexToVec4(0x6C8CFF); // Info
        if (t.type == NoticeType::Success) {
            accentCol = ThemeEngine::hexToVec4(0x10B981);
        } else if (t.type == NoticeType::Warning) {
            accentCol = ThemeEngine::hexToVec4(0xF59E0B);
        } else if (t.type == NoticeType::Error) {
            accentCol = ThemeEngine::hexToVec4(0xEF4444);
        }

        ImGui::SetNextWindowPos(ImVec2(curX, curY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kCardW, kCardH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
        ImGui::PushStyleColor(ImGuiCol_Border, palette.border);

        const std::string winName = "##ToastCard_" + std::to_string(t.id);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoScrollbar;

        if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pMin = ImGui::GetWindowPos();
            const ImVec2 pMax(pMin.x + kCardW, pMin.y + kCardH);

            // Soft drop shadow (2 offset translucent rects)
            dl->AddRectFilled(
                ImVec2(pMin.x + 2.0f, pMin.y + 4.0f),
                ImVec2(pMax.x + 2.0f, pMax.y + 4.0f),
                IM_COL32(0, 0, 0, static_cast<int>(38.0f * alpha)),
                10.0f
            );

            // Colored left accent bar
            dl->AddRectFilled(
                pMin,
                ImVec2(pMin.x + 5.0f, pMax.y),
                ThemeEngine::vec4ToU32(ImVec4(accentCol.x, accentCol.y, accentCol.z, alpha)),
                10.0f,
                ImDrawFlags_RoundCornersLeft
            );

            // Shrinking bottom progress line
            const float prog = std::clamp(1.0f - (t.elapsed / t.duration), 0.0f, 1.0f);
            dl->AddRectFilled(
                ImVec2(pMin.x + 5.0f, pMax.y - 3.0f),
                ImVec2(pMin.x + 5.0f + (kCardW - 5.0f) * prog, pMax.y),
                ThemeEngine::vec4ToU32(ImVec4(accentCol.x, accentCol.y, accentCol.z, 0.85f * alpha)),
                0.0f
            );

            ImGui::SetCursorPosX(16.0f);
            ImGui::TextColored(accentCol, "%s", t.title.c_str());

            if (fonts.captionFont != nullptr) {
                ImGui::PushFont(fonts.captionFont);
            }
            ImGui::SetCursorPosX(16.0f);
            ImGui::TextColored(palette.text, "%s", t.message.c_str());
            if (fonts.captionFont != nullptr) {
                ImGui::PopFont();
            }

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                t.dismissed = true;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    q.erase(
        std::remove_if(
            q.begin(),
            q.end(),
            [](const ToastItem& item) { return item.dismissed || item.elapsed >= item.duration; }
        ),
        q.end()
    );
}

// =============================================================================
// 2. GUI RENDERER CONSTRUCTION & FRAME LIFECYCLE
// =============================================================================
/*
 * Purpose: Constructs GuiRenderer, initializes login background particles and applies saved theme.
 * Inputs: system, themeEngine, fonts, startupSelfTests.
 * Outputs: Initialized GuiRenderer ready for renderFrame().
 * Big-O: O(P) where P = 36 drifting particles.
 */
GuiRenderer::GuiRenderer(
    HotelSystem& system,
    ThemeEngine& themeEngine,
    const FontBank& fonts,
    const std::vector<SelfTestEntry>& startupSelfTests
)
    : m_system(system),
      m_theme(themeEngine),
      m_fonts(fonts),
      m_startupSelfTests(startupSelfTests) {
    std::mt19937 rng(20261119U);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    m_particles.reserve(36U);
    for (int i = 0; i < 36; ++i) {
        LoginParticle p{};
        p.x      = dist01(rng);
        p.y      = dist01(rng);
        p.vx     = (dist01(rng) - 0.5f) * 0.018f;
        p.vy     = -0.008f - dist01(rng) * 0.014f;
        p.radius = 2.0f + dist01(rng) * 4.5f;
        p.alpha  = 0.10f + dist01(rng) * 0.22f;
        m_particles.push_back(p);
    }

    // Apply persisted theme choice from settings.dat
    const ThemeMode savedMode = (m_system.db().settings.themeChoice == 1)
                                    ? ThemeMode::ExecutivePearl
                                    : ThemeMode::MidnightObsidian;
    m_theme.initialize(savedMode);
}

/*
 * Purpose: Navigates to a target screen and resets the 180ms fade + 12px slide-in transition.
 * Inputs: screen (ScreenId).
 * Outputs: Updates m_activeScreen and resets m_pageAnimElapsed to 0.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::navigateTo(ScreenId screen) {
    if (m_activeScreen != screen) {
        m_activeScreen = screen;
        m_pageAnimElapsed = 0.0f;
    }
}

/*
 * Purpose: Drains pending notices from HotelSystem into the Toast overlay stack.
 * Inputs: None.
 * Outputs: Pushes SystemNotice items to Toast.
 * Big-O: O(K) where K is number of new notices this frame.
 */
void GuiRenderer::syncSystemNoticesToToasts() {
    const std::vector<SystemNotice> notices = m_system.drainNotices();
    for (const SystemNotice& n : notices) {
        Toast::push(n.type, n.title, n.message, n.relatedWaitlistId, n.relatedRoomId);
    }
}

/*
 * Purpose: Processes global keyboard shortcuts (Ctrl+T theme toggle, Ctrl+Z undo, Ctrl+Y redo).
 * Inputs: None.
 * Outputs: Executes matching shortcut actions.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::handleGlobalShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        m_theme.toggleTheme();
        m_system.db().settings.themeChoice = (m_theme.getCurrentMode() == ThemeMode::ExecutivePearl) ? 1 : 0;
        m_system.fileManager().saveSettings(m_system.db().settings);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        m_showShortcutsOverlay = !m_showShortcutsOverlay;
    }
    if (m_system.isLoggedIn() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        m_cmdPalette.isOpen = !m_cmdPalette.isOpen;
        if (m_cmdPalette.isOpen) {
            m_cmdPalette.queryBuf[0] = '\0';
            m_cmdPalette.selectedIndex = 0;
            m_cmdPalette.focusInput = true;
        }
    }
    if (m_system.isLoggedIn() && !io.WantTextInput && !m_cmdPalette.isOpen) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            m_system.undo();
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            m_system.redo();
        }
    }
}

/*
 * Purpose: Master per-frame UI entry point: ticks HotelSystem, handles shortcuts, routes Splash -> Login -> Shell,
 *          and renders the top-right Toast stack.
 * Inputs: deltaTime (frame delta seconds).
 * Outputs: Submits complete frame to ImGui draw lists.
 * Big-O: O(Active Screen Complexity).
 */
void GuiRenderer::renderFrame(float deltaTime) {
    const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
    m_system.tick(static_cast<double>(dt));
    m_theme.update(dt);
    handleGlobalShortcuts();
    syncSystemNoticesToToasts();

    if (m_splashElapsed < kSplashDuration) {
        renderSplashScreen(dt);
    } else if (!m_system.isLoggedIn()) {
        renderLoginScreen(dt);
    } else {
        renderAuthenticatedShell(dt);
        renderCommandPaletteOverlay(dt);
        updateAndRenderGuidedDemo(dt);
    }

    renderShortcutsOverlay();
    updateAndRenderConfetti(dt);
    Toast::updateAndRender(dt, m_theme.getActivePalette(), m_fonts);
}

// =============================================================================
// 3. ANIMATED 1.8s SPLASH SCREEN (Section 8)
// =============================================================================
/*
 * Purpose: Renders the 1.8-second animated splash screen with logo scale-in, progress bar,
 *          and "NUST SEECS · Section D · PF Project" branding before transitioning to Login.
 * Inputs: deltaTime.
 * Outputs: Renders full-viewport splash screen.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::renderSplashScreen(float deltaTime) {
    m_splashElapsed = std::min(kSplashDuration, m_splashElapsed + deltaTime);
    const float progress = std::clamp(m_splashElapsed / kSplashDuration, 0.0f, 1.0f);
    const float scaleT   = easeOutCubic(std::min(1.0f, m_splashElapsed / 0.65f));
    const float fadeOut  = (progress > 0.85f) ? (1.0f - (progress - 0.85f) / 0.15f) : 1.0f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ThemePalette& pal = m_theme.getActivePalette();

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fadeOut);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##SplashScreen", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = vp->Pos;
        const ImVec2 p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);

        // Multi-color atmospheric gradient background
        const ImU32 cTopLeft  = ThemeEngine::vec4ToU32(pal.background);
        const ImU32 cTopRight = ThemeEngine::vec4ToU32(ThemeEngine::lerpColor(pal.background, pal.accent, 0.14f));
        const ImU32 cBotLeft  = ThemeEngine::vec4ToU32(ThemeEngine::lerpColor(pal.background, pal.secondaryAccent, 0.08f));
        const ImU32 cBotRight = ThemeEngine::vec4ToU32(pal.panel);
        dl->AddRectFilledMultiColor(p0, p1, cTopLeft, cTopRight, cBotRight, cBotLeft);

        const ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.44f);
        const float logoSide = 86.0f * (0.65f + 0.35f * scaleT);
        const ImVec2 logoMin(center.x - logoSide * 0.5f, center.y - logoSide * 0.5f);
        const ImVec2 logoMax(center.x + logoSide * 0.5f, center.y + logoSide * 0.5f);

        // Soft ambient glow behind logo
        dl->AddRectFilled(
            ImVec2(logoMin.x - 10.0f, logoMin.y - 10.0f),
            ImVec2(logoMax.x + 10.0f, logoMax.y + 10.0f),
            ThemeEngine::vec4ToU32(ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.16f * scaleT)),
            24.0f
        );
        dl->AddRectFilled(logoMin, logoMax, ThemeEngine::vec4ToU32(pal.accent), 18.0f);
        dl->AddRect(logoMin, logoMax, ThemeEngine::vec4ToU32(pal.secondaryAccent), 18.0f, 0, 2.0f);

        // Vector "H" glyph inside the rounded square
        const float hw = logoSide * 0.24f;
        const float hh = logoSide * 0.28f;
        const float stroke = logoSide * 0.09f;
        const ImU32 whiteCol = IM_COL32(255, 255, 255, static_cast<int>(255.0f * scaleT));
        dl->AddLine(ImVec2(center.x - hw, center.y - hh), ImVec2(center.x - hw, center.y + hh), whiteCol, stroke);
        dl->AddLine(ImVec2(center.x + hw, center.y - hh), ImVec2(center.x + hw, center.y + hh), whiteCol, stroke);
        dl->AddLine(ImVec2(center.x - hw, center.y), ImVec2(center.x + hw, center.y), whiteCol, stroke);

        // Title & Subtitle
        if (m_fonts.headingFont != nullptr) {
            ImGui::PushFont(m_fonts.headingFont);
        }
        const char* title = "NUST SEECS Hotel Management System";
        const ImVec2 titleSz = ImGui::CalcTextSize(title);
        ImGui::SetCursorScreenPos(ImVec2(center.x - titleSz.x * 0.5f, logoMax.y + 24.0f));
        ImGui::TextColored(pal.text, "%s", title);
        if (m_fonts.headingFont != nullptr) {
            ImGui::PopFont();
        }

        const char* subtitle = "NUST SEECS  ·  Section D  ·  PF Project";
        const ImVec2 subSz = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorScreenPos(ImVec2(center.x - subSz.x * 0.5f, logoMax.y + 62.0f));
        ImGui::TextColored(pal.dimText, "%s", subtitle);

        // Animated progress bar
        const float barW = 340.0f;
        const float barH = 8.0f;
        const ImVec2 barMin(center.x - barW * 0.5f, logoMax.y + 104.0f);
        const ImVec2 barMax(center.x + barW * 0.5f, barMin.y + barH);
        dl->AddRectFilled(barMin, barMax, ThemeEngine::vec4ToU32(pal.surface), 4.0f);
        dl->AddRectFilled(
            barMin,
            ImVec2(barMin.x + barW * easeOutCubic(progress), barMax.y),
            ThemeEngine::vec4ToU32(pal.accent),
            4.0f
        );

        if (m_fonts.captionFont != nullptr) {
            ImGui::PushFont(m_fonts.captionFont);
        }
        const char* loadMsg = (progress < 0.4f)
                                  ? "Verifying SHA-256 audit chain & FNV-1a binary tables..."
                                  : ((progress < 0.8f) ? "Building CNIC hash index & housekeeping min-heap..."
                                                       : "Launching Executive Interface...");
        const ImVec2 lmSz = ImGui::CalcTextSize(loadMsg);
        ImGui::SetCursorScreenPos(ImVec2(center.x - lmSz.x * 0.5f, barMax.y + 12.0f));
        ImGui::TextColored(pal.dimText, "%s", loadMsg);
        if (m_fonts.captionFont != nullptr) {
            ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
}

// =============================================================================
// 4. ANIMATED GLASS-STYLE LOGIN SCREEN (Section 7)
// =============================================================================
/*
 * Purpose: Renders the centered 460x560 glass-style login card over an animated gradient background
 *          with slow drifting particles, logo glyph "H", username/password fields, show-password toggle,
 *          and 3-failed-attempt 30s lockout countdown (Section 7).
 * Inputs: deltaTime.
 * Outputs: Authenticates user into HotelSystem on valid submit.
 * Big-O: O(P) where P = 36 particles.
 */
void GuiRenderer::renderLoginScreen(float deltaTime) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ThemePalette& pal = m_theme.getActivePalette();

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##LoginBackground", nullptr, bgFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = vp->Pos;
        const ImVec2 p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
        const float timeSec = static_cast<float>(ImGui::GetTime());
        const float wave = 0.5f + 0.5f * std::sin(timeSec * 0.45f);

        const ImU32 cTL = ThemeEngine::vec4ToU32(ThemeEngine::lerpColor(pal.background, pal.accent, 0.08f + 0.08f * wave));
        const ImU32 cTR = ThemeEngine::vec4ToU32(ThemeEngine::lerpColor(pal.background, pal.secondaryAccent, 0.10f * (1.0f - wave)));
        const ImU32 cBR = ThemeEngine::vec4ToU32(ThemeEngine::lerpColor(pal.panel, pal.accent, 0.07f * wave));
        const ImU32 cBL = ThemeEngine::vec4ToU32(pal.background);
        dl->AddRectFilledMultiColor(p0, p1, cTL, cTR, cBR, cBL);

        // Animate drifting ambient particles
        for (LoginParticle& pt : m_particles) {
            pt.x += pt.vx * deltaTime;
            pt.y += pt.vy * deltaTime;
            if (pt.y < -0.05f) { pt.y = 1.05f; }
            if (pt.x < -0.05f) { pt.x = 1.05f; }
            if (pt.x > 1.05f)  { pt.x = -0.05f; }

            const ImVec2 screenPt(vp->Pos.x + pt.x * vp->Size.x, vp->Pos.y + pt.y * vp->Size.y);
            const ImVec4 pCol(pal.accent.x, pal.accent.y, pal.accent.z, pt.alpha);
            dl->AddCircleFilled(screenPt, pt.radius, ThemeEngine::vec4ToU32(pCol), 16);
        }

        // Top-right quick theme toggle on login screen
        ImGui::SetCursorScreenPos(ImVec2(vp->Pos.x + vp->Size.x - 165.0f, vp->Pos.y + 20.0f));
        if (ghostButton(m_theme.getCurrentMode() == ThemeMode::MidnightObsidian ? "Light Theme" : "Dark Theme", ImVec2(140.0f, 36.0f))) {
            m_theme.toggleTheme();
        }

        // Centered 460x560 glass-style card
        constexpr float kCardW = 460.0f;
        constexpr float kCardH = 560.0f;
        const ImVec2 cardMin(
            vp->Pos.x + (vp->Size.x - kCardW) * 0.5f,
            vp->Pos.y + (vp->Size.y - kCardH) * 0.5f
        );
        const ImVec2 cardMax(cardMin.x + kCardW, cardMin.y + kCardH);

        // Drop shadow + translucent glass surface
        dl->AddRectFilled(
            ImVec2(cardMin.x + 4.0f, cardMin.y + 10.0f),
            ImVec2(cardMax.x + 4.0f, cardMax.y + 10.0f),
            IM_COL32(0, 0, 0, 55),
            16.0f
        );
        dl->AddRectFilled(
            cardMin,
            cardMax,
            ThemeEngine::vec4ToU32(ImVec4(pal.panel.x, pal.panel.y, pal.panel.z, 0.92f)),
            16.0f
        );
        dl->AddRect(cardMin, cardMax, ThemeEngine::vec4ToU32(pal.border), 16.0f, 0, 1.5f);

        // Logo glyph "H" in a rounded square
        const ImVec2 logoCenter(cardMin.x + kCardW * 0.5f, cardMin.y + 68.0f);
        const ImVec2 lMin(logoCenter.x - 30.0f, logoCenter.y - 30.0f);
        const ImVec2 lMax(logoCenter.x + 30.0f, logoCenter.y + 30.0f);
        dl->AddRectFilled(lMin, lMax, ThemeEngine::vec4ToU32(pal.accent), 14.0f);
        dl->AddLine(ImVec2(logoCenter.x - 11.0f, logoCenter.y - 13.0f), ImVec2(logoCenter.x - 11.0f, logoCenter.y + 13.0f), IM_COL32(255, 255, 255, 255), 4.5f);
        dl->AddLine(ImVec2(logoCenter.x + 11.0f, logoCenter.y - 13.0f), ImVec2(logoCenter.x + 11.0f, logoCenter.y + 13.0f), IM_COL32(255, 255, 255, 255), 4.5f);
        dl->AddLine(ImVec2(logoCenter.x - 11.0f, logoCenter.y), ImVec2(logoCenter.x + 11.0f, logoCenter.y), IM_COL32(255, 255, 255, 255), 4.5f);

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 36.0f, cardMin.y + 112.0f));
        ImGui::BeginChild("##LoginCardBody", ImVec2(kCardW - 72.0f, kCardH - 132.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

        if (m_fonts.headingFont != nullptr) {
            ImGui::PushFont(m_fonts.headingFont);
        }
        const char* heading = "Sign In to HMS";
        const float hW = ImGui::CalcTextSize(heading).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - hW) * 0.5f);
        ImGui::TextColored(pal.text, "%s", heading);
        if (m_fonts.headingFont != nullptr) {
            ImGui::PopFont();
        }

        const char* sub = "NUST SEECS Grand Hotel Operations";
        const float sW = ImGui::CalcTextSize(sub).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - sW) * 0.5f);
        ImGui::TextColored(pal.dimText, "%s", sub);

        ImGui::Spacing();
        ImGui::Spacing();

        const double lockoutRem = m_system.getLoginLockoutRemaining();
        const bool isLocked = (lockoutRem > 0.0);

        ImGui::TextColored(pal.dimText, "Username");
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputTextWithHint(
            "##LoginUser",
            "admin / reception / housekeeping",
            m_loginUserBuf,
            sizeof(m_loginUserBuf),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        ImGui::Spacing();
        ImGui::TextColored(pal.dimText, "Password");
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(availW - 84.0f);
        const ImGuiInputTextFlags passFlags =
            ImGuiInputTextFlags_EnterReturnsTrue | (m_showPassword ? 0 : ImGuiInputTextFlags_Password);
        enterPressed |= ImGui::InputTextWithHint("##LoginPass", "Enter password", m_loginPassBuf, sizeof(m_loginPassBuf), passFlags);
        ImGui::SameLine();
        if (ghostButton(m_showPassword ? "Hide" : "Show", ImVec2(74.0f, 34.0f))) {
            m_showPassword = !m_showPassword;
        }

        ImGui::Spacing();
        if (isLocked) {
            ImGui::TextColored(
                pal.statusOccupied,
                "Locked out after 3 failed attempts. Retry in %d s...",
                static_cast<int>(std::ceil(lockoutRem))
            );
        } else if (!m_loginErrorMsg.empty()) {
            ImGui::TextWrapped("%s", m_loginErrorMsg.c_str());
        } else {
            ImGui::Dummy(ImVec2(0.0f, 18.0f));
        }

        ImGui::Spacing();
        const bool submitClick = primaryButton(isLocked ? "Account Locked" : "Sign In", ImVec2(-1.0f, 42.0f), !isLocked);
        if ((submitClick || enterPressed) && !isLocked) {
            const LoginResult res = m_system.login(m_loginUserBuf, m_loginPassBuf);
            if (res.success) {
                m_loginErrorMsg.clear();
                std::memset(m_loginPassBuf, 0, sizeof(m_loginPassBuf));
                navigateTo(ScreenId::Dashboard);
            } else {
                m_loginErrorMsg = res.message;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_fonts.captionFont != nullptr) {
            ImGui::PushFont(m_fonts.captionFont);
        }
        ImGui::TextColored(pal.dimText, "Quick Viva Demo Accounts (Click to autofill):");
        if (m_fonts.captionFont != nullptr) {
            ImGui::PopFont();
        }
        ImGui::Spacing();

        const float chipW = (ImGui::GetContentRegionAvail().x - 20.0f) / 3.0f;
        if (ghostButton("Admin", ImVec2(chipW, 32.0f))) {
            std::snprintf(m_loginUserBuf, sizeof(m_loginUserBuf), "admin");
            std::snprintf(m_loginPassBuf, sizeof(m_loginPassBuf), "Admin@123");
            m_loginErrorMsg.clear();
        }
        ImGui::SameLine();
        if (ghostButton("Reception", ImVec2(chipW, 32.0f))) {
            std::snprintf(m_loginUserBuf, sizeof(m_loginUserBuf), "reception");
            std::snprintf(m_loginPassBuf, sizeof(m_loginPassBuf), "Desk@123");
            m_loginErrorMsg.clear();
        }
        ImGui::SameLine();
        if (ghostButton("Housekeep", ImVec2(chipW, 32.0f))) {
            std::snprintf(m_loginUserBuf, sizeof(m_loginUserBuf), "housekeeping");
            std::snprintf(m_loginPassBuf, sizeof(m_loginPassBuf), "Clean@123");
            m_loginErrorMsg.clear();
        }

        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// =============================================================================
// 5. AUTHENTICATED SHELL: SIDEBAR (230px / 64px), TOP BAR (56px), STATUS BAR (26px)
// =============================================================================
/*
 * Purpose: Builds the list of navigation items visible for the current user's Role per Section 7.
 * Inputs: role, outItems vector.
 * Outputs: Populates outItems with only role-permitted screens.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::renderRoleAllowedNavItems(Role role, std::vector<NavItemSpec>& outItems) const {
    outItems.clear();
    if (role == Role::Admin) {
        outItems = {
            {ScreenId::Dashboard,           "Dashboard",        NavGlyph::Dashboard},
            {ScreenId::RoomMatrix,          "Room Matrix",      NavGlyph::Rooms},
            {ScreenId::SmartCheckIn,        "Smart Check-In",   NavGlyph::CheckIn},
            {ScreenId::Timeline,            "Timeline",         NavGlyph::Timeline},
            {ScreenId::Guests,              "Guests CRM",       NavGlyph::Guests},
            {ScreenId::Bookings,            "Bookings",         NavGlyph::Bookings},
            {ScreenId::Billing,             "Billing",          NavGlyph::Billing},
            {ScreenId::YieldManagement,     "Yield Mgmt",       NavGlyph::Yield},
            {ScreenId::Analytics,           "Analytics",        NavGlyph::Analytics},
            {ScreenId::HousekeepingBoard,   "Housekeeping",     NavGlyph::Housekeeping},
            {ScreenId::ServiceOrdersKanban, "Service Orders",   NavGlyph::Kanban},
            {ScreenId::Staff,               "Staff",            NavGlyph::Staff},
            {ScreenId::AuditLog,            "Audit Log",        NavGlyph::Audit},
            {ScreenId::Backups,             "Backups",          NavGlyph::Backups},
            {ScreenId::Settings,            "Settings",         NavGlyph::Settings},
            {ScreenId::VivaTools,           "Viva Tools",       NavGlyph::Viva}
        };
    } else if (role == Role::Receptionist) {
        outItems = {
            {ScreenId::Dashboard,           "Dashboard",        NavGlyph::Dashboard},
            {ScreenId::RoomMatrix,          "Room Matrix",      NavGlyph::Rooms},
            {ScreenId::SmartCheckIn,        "Smart Check-In",   NavGlyph::CheckIn},
            {ScreenId::Timeline,            "Timeline",         NavGlyph::Timeline},
            {ScreenId::Guests,              "Guests CRM",       NavGlyph::Guests},
            {ScreenId::Bookings,            "Bookings",         NavGlyph::Bookings},
            {ScreenId::Billing,             "Billing",          NavGlyph::Billing},
            {ScreenId::Waitlist,            "Waitlist",         NavGlyph::Waitlist}
        };
    } else {
        outItems = {
            {ScreenId::Dashboard,           "Dashboard",        NavGlyph::Dashboard},
            {ScreenId::HousekeepingBoard,   "Dispatch Board",   NavGlyph::Housekeeping},
            {ScreenId::RoomMatrix,          "Room Matrix",      NavGlyph::Rooms},
            {ScreenId::ServiceOrdersKanban, "Service Kanban",   NavGlyph::Kanban}
        };
    }
}

/*
 * Purpose: Renders the full authenticated application shell: 56px Top Bar, collapsible 230px/64px Left Sidebar,
 *          26px Status Bar, and central animated content pane with DockSpaceOverViewport.
 * Inputs: deltaTime.
 * Outputs: Renders shell chrome and active screen.
 * Big-O: O(Active Screen).
 */
void GuiRenderer::renderAuthenticatedShell(float deltaTime) {
    constexpr float kTopBarH    = 56.0f;
    constexpr float kStatusBarH = 26.0f;

    // Smoothly animate sidebar width between 230px and 64px
    const float targetSidebarW = m_sidebarCollapsed ? 64.0f : 230.0f;
    m_sidebarWidth += (targetSidebarW - m_sidebarWidth) * std::min(1.0f, deltaTime * 14.0f);

    // Advance 180ms page transition animation
    m_pageAnimElapsed = std::min(kPageAnimDuration, m_pageAnimElapsed + deltaTime);

    renderTopBar(kTopBarH);
    renderSidebar(kTopBarH, kStatusBarH);
    renderStatusBar(kStatusBarH);

    // Main central content viewport
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float contentX = vp->Pos.x + m_sidebarWidth;
    const float contentY = vp->Pos.y + kTopBarH;
    const float contentW = std::max(300.0f, vp->Size.x - m_sidebarWidth);
    const float contentH = std::max(200.0f, vp->Size.y - kTopBarH - kStatusBarH);

    const float animT = easeOutCubic(m_pageAnimElapsed / kPageAnimDuration);
    const float slideOffsetY = (1.0f - animT) * 12.0f; // 12px slide-in over 180ms (Section 8)

    ImGui::SetNextWindowPos(ImVec2(contentX, contentY + slideOffsetY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(contentW, contentH - slideOffsetY), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.05f, animT));

    const ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##MainContentPane", nullptr, mainFlags)) {
        renderActiveScreenContent();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

/*
 * Purpose: Renders the 56px Top Bar with sidebar toggle, breadcrumb, global search field (Ctrl+K hint),
 *          Undo/Redo indicator, live clock, Sun/Moon theme toggle, notification bell with badge, and user avatar circle.
 * Inputs: height (56.0f).
 * Outputs: Renders Top Bar window.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::renderTopBar(float height) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ThemePalette& pal = m_theme.getActivePalette();

    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panel);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##TopBar", nullptr, flags)) {
        // Sidebar collapse button
        if (ghostButton(m_sidebarCollapsed ? ">>" : "<<", ImVec2(38.0f, 34.0f))) {
            m_sidebarCollapsed = !m_sidebarCollapsed;
        }
        drawTooltip(m_sidebarCollapsed ? "Expand Navigation Sidebar" : "Collapse to Icon Rail");

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal.dimText, "HMS  /");
        ImGui::SameLine();
        ImGui::TextColored(pal.text, "%s", screenDisplayName(m_activeScreen));

        // Undo / Redo indicator in toolbar (Section 9.10 #2)
        if (m_system.canUndo()) {
            ImGui::SameLine(0.0f, 18.0f);
            const std::string undoLabel = "Undo: " + m_system.lastUndoActionName();
            if (ghostButton(undoLabel.c_str(), ImVec2(0.0f, 32.0f))) {
                m_system.undo();
            }
            drawTooltip("Ctrl+Z to Undo last action");
        }

        // Right-aligned toolbar cluster
        const float rightStart = vp->Size.x - 585.0f;
        if (rightStart > ImGui::GetCursorPosX() + 10.0f) {
            ImGui::SameLine(rightStart);
        } else {
            ImGui::SameLine();
        }

        ImGui::SetNextItemWidth(230.0f);
        ImGui::InputTextWithHint("##GlobalSearch", "Search guests, rooms... (Ctrl+K)", m_globalSearchBuf, sizeof(m_globalSearchBuf));
        if (ImGui::IsItemActivated()) {
            m_cmdPalette.isOpen = true;
            m_cmdPalette.focusInput = true;
            std::snprintf(m_cmdPalette.queryBuf, sizeof(m_cmdPalette.queryBuf), "%s", m_globalSearchBuf);
        }

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal.dimText, "%s", currentClockString().c_str());

        // Sun/Moon theme toggle button
        ImGui::SameLine(0.0f, 12.0f);
        const char* themeBtnTxt = (m_theme.getCurrentMode() == ThemeMode::MidnightObsidian) ? "Light" : "Dark";
        if (ghostButton(themeBtnTxt, ImVec2(62.0f, 34.0f))) {
            m_theme.toggleTheme();
            m_system.db().settings.themeChoice = (m_theme.getCurrentMode() == ThemeMode::ExecutivePearl) ? 1 : 0;
            m_system.fileManager().saveSettings(m_system.db().settings);
        }
        drawTooltip("Toggle Midnight Obsidian / Executive Pearl (Ctrl+T)");

        // Notification Bell with unread badge
        ImGui::SameLine(0.0f, 10.0f);
        const int unread = m_system.unreadNotificationCount();
        const std::string bellLabel = (unread > 0) ? ("Alerts (" + std::to_string(unread) + ")") : "Alerts";
        if (ghostButton(bellLabel.c_str(), ImVec2(84.0f, 34.0f))) {
            ImGui::OpenPopup("##NotificationsPopup");
            m_system.markNotificationsRead();
        }
        if (ImGui::BeginPopup("##NotificationsPopup")) {
            ImGui::TextColored(pal.accent, "Recent Notifications");
            ImGui::Separator();
            const auto& hist = m_system.notificationHistory();
            if (hist.empty()) {
                ImGui::TextColored(pal.dimText, "No notifications yet.");
            } else {
                const int startIdx = std::max(0, static_cast<int>(hist.size()) - 8);
                for (int i = static_cast<int>(hist.size()) - 1; i >= startIdx; --i) {
                    const SystemNotice& n = hist[static_cast<std::size_t>(i)];
                    ImGui::TextColored(pal.text, "%s", n.title.c_str());
                    ImGui::TextColored(pal.dimText, "  %s", n.message.c_str());
                    ImGui::Separator();
                }
            }
            ImGui::EndPopup();
        }

        // User avatar circle with initials
        ImGui::SameLine(0.0f, 12.0f);
        const User* u = m_system.currentUser();
        const std::string initials = extractInitials(u ? u->displayName : "User");
        const ImVec2 avPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 avCenter(avPos.x + 17.0f, avPos.y + 17.0f);
        dl->AddCircleFilled(avCenter, 17.0f, ThemeEngine::vec4ToU32(pal.accent), 24);
        const ImVec2 initSz = ImGui::CalcTextSize(initials.c_str());
        dl->AddText(
            ImVec2(avCenter.x - initSz.x * 0.5f, avCenter.y - initSz.y * 0.5f),
            IM_COL32(255, 255, 255, 255),
            initials.c_str()
        );
        ImGui::Dummy(ImVec2(34.0f, 34.0f));
        if (u != nullptr) {
            drawTooltip((u->displayName + " (" + toString(u->role) + ")").c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

/*
 * Purpose: Renders the collapsible Left Sidebar (230px expanded / 64px icon rail) with custom ImDrawList
 *          vector glyphs (no icon fonts), role-filtered navigation items, role badge, and Logout button.
 * Inputs: topBarHeight, statusBarHeight.
 * Outputs: Renders Sidebar window.
 * Big-O: O(M) where M <= 16 menu items.
 */
void GuiRenderer::renderSidebar(float topBarHeight, float statusBarHeight) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ThemePalette& pal = m_theme.getActivePalette();
    const float sideH = std::max(200.0f, vp->Size.y - topBarHeight - statusBarHeight);

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_sidebarWidth, sideH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panel);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##LeftSidebar", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Brand Logo Header
        const ImVec2 logoPos = ImGui::GetCursorScreenPos();
        const ImVec2 sqMin(logoPos.x + 4.0f, logoPos.y + 2.0f);
        const ImVec2 sqMax(sqMin.x + 36.0f, sqMin.y + 36.0f);
        dl->AddRectFilled(sqMin, sqMax, ThemeEngine::vec4ToU32(pal.accent), 9.0f);
        const ImVec2 lc((sqMin.x + sqMax.x) * 0.5f, (sqMin.y + sqMax.y) * 0.5f);
        dl->AddLine(ImVec2(lc.x - 6.0f, lc.y - 8.0f), ImVec2(lc.x - 6.0f, lc.y + 8.0f), IM_COL32(255, 255, 255, 255), 2.8f);
        dl->AddLine(ImVec2(lc.x + 6.0f, lc.y - 8.0f), ImVec2(lc.x + 6.0f, lc.y + 8.0f), IM_COL32(255, 255, 255, 255), 2.8f);
        dl->AddLine(ImVec2(lc.x - 6.0f, lc.y), ImVec2(lc.x + 6.0f, lc.y), IM_COL32(255, 255, 255, 255), 2.8f);

        if (!m_sidebarCollapsed && m_sidebarWidth > 140.0f) {
            dl->AddText(ImVec2(sqMax.x + 10.0f, sqMin.y + 2.0f), ThemeEngine::vec4ToU32(pal.text), "NUST SEECS");
            if (m_fonts.captionFont != nullptr) {
                dl->AddText(m_fonts.captionFont, 12.0f, ImVec2(sqMax.x + 10.0f, sqMin.y + 20.0f), ThemeEngine::vec4ToU32(pal.dimText), "Hotel Mgmt Suite");
            }
        }
        ImGui::Dummy(ImVec2(0.0f, 44.0f));
        ImGui::Separator();
        ImGui::Spacing();

        // Scrollable nav items area
        std::vector<NavItemSpec> navItems;
        renderRoleAllowedNavItems(m_system.currentRole(), navItems);

        const float footerReserve = 96.0f;
        const float listH = std::max(80.0f, ImGui::GetContentRegionAvail().y - footerReserve);
        ImGui::BeginChild("##NavScrollArea", ImVec2(0.0f, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        for (const NavItemSpec& item : navItems) {
            const bool isSelected = (m_activeScreen == item.screen);
            const ImVec2 btnSize(ImGui::GetContentRegionAvail().x, 38.0f);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const std::string btnId = std::string("##NavBtn_") + item.label;
            const bool clicked = ImGui::InvisibleButton(btnId.c_str(), btnSize);
            const bool hovered = ImGui::IsItemHovered();

            ImVec4 bgCol = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            if (isSelected) {
                bgCol = ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.18f);
            } else if (hovered) {
                bgCol = pal.hover;
            }

            if (bgCol.w > 0.01f) {
                dl->AddRectFilled(pos, ImVec2(pos.x + btnSize.x, pos.y + btnSize.y), ThemeEngine::vec4ToU32(bgCol), 8.0f);
            }
            if (isSelected) {
                dl->AddRectFilled(
                    ImVec2(pos.x, pos.y + 6.0f),
                    ImVec2(pos.x + 4.0f, pos.y + btnSize.y - 6.0f),
                    ThemeEngine::vec4ToU32(pal.accent),
                    2.0f
                );
            }

            const ImVec4 fgCol = isSelected ? pal.accent : (hovered ? pal.text : pal.dimText);
            drawVectorGlyph(dl, item.glyph, ImVec2(pos.x + 22.0f, pos.y + btnSize.y * 0.5f), 8.0f, ThemeEngine::vec4ToU32(fgCol));

            if (!m_sidebarCollapsed && m_sidebarWidth > 140.0f) {
                const ImVec2 tSz = ImGui::CalcTextSize(item.label);
                dl->AddText(
                    ImVec2(pos.x + 42.0f, pos.y + (btnSize.y - tSz.y) * 0.5f),
                    ThemeEngine::vec4ToU32(isSelected ? pal.text : fgCol),
                    item.label
                );
            } else if (hovered) {
                ImGui::SetTooltip("%s", item.label);
            }

            if (clicked) {
                navigateTo(item.screen);
            }
        }
        ImGui::EndChild();

        // Bottom role badge & Logout button
        ImGui::Separator();
        ImGui::Spacing();
        if (!m_sidebarCollapsed && m_sidebarWidth > 140.0f) {
            const Role r = m_system.currentRole();
            const ImVec4 roleCol = (r == Role::Admin)
                                       ? pal.accent
                                       : ((r == Role::Receptionist) ? pal.secondaryAccent : pal.statusCleaning);
            drawBadge(toString(r), roleCol);
            ImGui::Spacing();
        }
        if (dangerButton(m_sidebarCollapsed ? "Out" : "Sign Out", ImVec2(-1.0f, 34.0f))) {
            m_system.logout();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

/*
 * Purpose: Renders the 26px bottom Status Bar: "Occupancy XX% | Surge x1.XX | Chain OK | Saved HH:MM:SS | FPS".
 * Inputs: height (26.0f).
 * Outputs: Renders Status Bar window.
 * Big-O: O(R) to compute live occupancy.
 */
void GuiRenderer::renderStatusBar(float height) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ThemePalette& pal = m_theme.getActivePalette();
    const YieldPricingResult yield = m_system.getTodayYieldPricing();
    const bool chainOk = (m_system.lastAuditTamperIndex() == -1);

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - height), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panel);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        if (m_fonts.captionFont != nullptr) {
            ImGui::PushFont(m_fonts.captionFont);
        }
        ImGui::TextColored(pal.text, "Occupancy %.0f%%", yield.occupancyRatio * 100.0);
        ImGui::SameLine(); ImGui::TextColored(pal.dimText, " | ");
        ImGui::SameLine(); ImGui::TextColored(pal.goldHighlight, "Surge x%.2f", yield.surge);
        ImGui::SameLine(); ImGui::TextColored(pal.dimText, " | ");
        ImGui::SameLine();
        ImGui::TextColored(
            chainOk ? pal.statusAvailable : pal.statusOccupied,
            "%s",
            chainOk ? "Chain VERIFIED" : "Chain TAMPERED"
        );
        ImGui::SameLine(); ImGui::TextColored(pal.dimText, " | ");
        ImGui::SameLine(); ImGui::TextColored(pal.dimText, "Saved %s", m_system.lastSavedTimeStr().c_str());
        ImGui::SameLine(); ImGui::TextColored(pal.dimText, " | ");
        ImGui::SameLine(); ImGui::TextColored(pal.secondaryAccent, "%.0f FPS", ImGui::GetIO().Framerate);

        if (m_fonts.captionFont != nullptr) {
            ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

/*
 * Purpose: Dispatches the active screen view inside the central shell pane.
 * Inputs: None.
 * Outputs: Renders the active screen.
 * Big-O: O(Active Screen).
 */
void GuiRenderer::renderActiveScreenContent() {
    switch (m_activeScreen) {
        case ScreenId::Dashboard:
            renderDashboardScreen();
            break;
        case ScreenId::RoomMatrix:
            renderRoomMatrixScreen();
            break;
        case ScreenId::SmartCheckIn:
            renderSmartCheckInScreen();
            break;
        case ScreenId::Timeline:
            renderTimelineScreen();
            break;
        case ScreenId::Guests:
            renderGuestsCrmScreen();
            break;
        case ScreenId::Bookings:
            renderBookingsScreen();
            break;
        case ScreenId::Billing:
            renderBillingScreen();
            break;
        case ScreenId::Waitlist:
            renderWaitlistScreen();
            break;
        case ScreenId::YieldManagement:
            renderYieldManagementScreen();
            break;
        case ScreenId::Analytics:
            renderAnalyticsScreen();
            break;
        case ScreenId::HousekeepingBoard:
            renderHousekeepingBoardScreen();
            break;
        case ScreenId::ServiceOrdersKanban:
            renderServiceOrdersKanbanScreen();
            break;
        case ScreenId::Staff:
            renderStaffScreen();
            break;
        case ScreenId::AuditLog:
            renderAuditLogScreen();
            break;
        case ScreenId::Backups:
            renderBackupsScreen();
            break;
        case ScreenId::Settings:
            renderSettingsScreen();
            break;
        case ScreenId::VivaTools:
            renderVivaToolsScreen();
            break;
        default:
            renderUpcomingModulePreview(m_activeScreen);
            break;
    }
}

/*
 * Purpose: Displays a clean, informative module status view for screens scheduled in Phases 5–8.
 * Inputs: screen (ScreenId).
 * Outputs: Renders themed card with quick return to Dashboard.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::renderUpcomingModulePreview(ScreenId screen) {
    drawSectionHeader(screenDisplayName(screen), "Scheduled for interactive implementation in Phases 5–8");
    if (beginCard("##ModulePreviewCard", "Module Ready for Next Phase", ImVec2(0.0f, 260.0f))) {
        drawEmptyState(
            screenDisplayName(screen),
            "Core backend state in HotelSystem is active. Interactive GUI screen will be wired in its designated phase."
        );
        ImGui::Spacing();
        if (primaryButton("Return to Dashboard", ImVec2(200.0f, 38.0f))) {
            navigateTo(ScreenId::Dashboard);
        }
    }
    endCard();
}

// =============================================================================
// 6. SHARED REUSABLE WIDGETS (Section 8 — Write Once, Reuse Everywhere)
// =============================================================================
/*
 * Purpose: Opens a rounded Card container with a soft 2-layer drop-shadow and optional title header.
 * Inputs: id (ImGui ID), title (nullable header), size.
 * Outputs: Returns true if child region is visible (always pair with endCard()).
 * Big-O: O(1) time, O(1) space.
 */
bool GuiRenderer::beginCard(const char* id, const char* title, const ImVec2& size) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 actualSize(
        (size.x <= 0.0f) ? avail.x : size.x,
        (size.y <= 0.0f) ? avail.y : size.y
    );

    ImDrawList* parentDl = ImGui::GetWindowDrawList();
    // 2 offset translucent drop-shadow rects
    parentDl->AddRectFilled(
        ImVec2(pos.x + 2.0f, pos.y + 4.0f),
        ImVec2(pos.x + actualSize.x + 2.0f, pos.y + actualSize.y + 4.0f),
        IM_COL32(0, 0, 0, 26),
        10.0f
    );
    parentDl->AddRectFilled(
        ImVec2(pos.x + 4.0f, pos.y + 8.0f),
        ImVec2(pos.x + actualSize.x + 4.0f, pos.y + actualSize.y + 8.0f),
        IM_COL32(0, 0, 0, 14),
        12.0f
    );

    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
    if (open && title != nullptr && title[0] != '\0') {
        ImGui::TextColored(pal.text, "%s", title);
        ImGui::Separator();
        ImGui::Spacing();
    }
    return open;
}

/*
 * Purpose: Closes a Card container opened by beginCard().
 * Inputs: None.
 * Outputs: Pops child window and styles.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/*
 * Purpose: Renders an animated KPI Tile with 600ms cubic ease-out count-up animation, delta pill,
 *          and mini vector sparkline drawn via ImDrawList (Section 8).
 * Inputs: id, label, numericValue, prefix, suffix, formatAsPkr, deltaText, deltaPositive, sparkline, accentColor.
 * Outputs: Renders KPI tile card.
 * Big-O: O(S) where S is sparkline point count.
 */
void GuiRenderer::drawKpiTile(
    const char* id,
    const char* label,
    double numericValue,
    const std::string& formattedPrefix,
    const std::string& formattedSuffix,
    bool formatAsPkr,
    const char* deltaText,
    bool deltaPositive,
    const std::vector<float>& sparkline,
    const ImVec4& accentColor
) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const float dt = ImGui::GetIO().DeltaTime;

    KpiAnimState& anim = m_kpiAnims[id];
    if (std::fabs(anim.targetVal - numericValue) > 1e-4) {
        anim.startVal = anim.targetVal;
        anim.targetVal = numericValue;
        anim.elapsed = 0.0f;
    }
    anim.elapsed = std::min(0.6f, anim.elapsed + dt);
    const double displayVal = anim.startVal + (anim.targetVal - anim.startVal) * easeOutCubic(anim.elapsed / 0.6f);

    if (beginCard(id, nullptr, ImVec2(0.0f, 128.0f))) {
        if (m_fonts.captionFont != nullptr) {
            ImGui::PushFont(m_fonts.captionFont);
        }
        ImGui::TextColored(pal.dimText, "%s", label);
        if (m_fonts.captionFont != nullptr) {
            ImGui::PopFont();
        }

        if (m_fonts.headingFont != nullptr) {
            ImGui::PushFont(m_fonts.headingFont);
        }
        std::string valStr;
        if (formatAsPkr) {
            valStr = formatPKR(displayVal);
        } else {
            char buf[64]{};
            std::snprintf(buf, sizeof(buf), "%s%.1f%s", formattedPrefix.c_str(), displayVal, formattedSuffix.c_str());
            valStr = buf;
        }
        ImGui::TextColored(pal.text, "%s", valStr.c_str());
        if (m_fonts.headingFont != nullptr) {
            ImGui::PopFont();
        }

        if (deltaText != nullptr && deltaText[0] != '\0') {
            drawBadge(deltaText, deltaPositive ? pal.statusAvailable : pal.statusOccupied);
        }

        // Draw sparkline in bottom-right of tile
        if (sparkline.size() >= 2U) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 winPos = ImGui::GetWindowPos();
            const ImVec2 winSz  = ImGui::GetWindowSize();
            const float spW = 104.0f;
            const float spH = 30.0f;
            const ImVec2 spMin(winPos.x + winSz.x - spW - 14.0f, winPos.y + winSz.y - spH - 12.0f);

            float minV = sparkline[0];
            float maxV = sparkline[0];
            for (float v : sparkline) {
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
            }
            const float range = std::max(0.001f, maxV - minV);
            const ImU32 lineCol = ThemeEngine::vec4ToU32(accentColor);

            for (std::size_t i = 0U; i + 1U < sparkline.size(); ++i) {
                const float x0 = spMin.x + (static_cast<float>(i) / static_cast<float>(sparkline.size() - 1U)) * spW;
                const float x1 = spMin.x + (static_cast<float>(i + 1U) / static_cast<float>(sparkline.size() - 1U)) * spW;
                const float y0 = spMin.y + spH - ((sparkline[i] - minV) / range) * spH;
                const float y1 = spMin.y + spH - ((sparkline[i + 1U] - minV) / range) * spH;
                dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, 2.0f);
            }
        }
    }
    endCard();
}

/*
 * Purpose: Draws a rounded pill badge with colored background and crisp caption text.
 * Inputs: text, bgColor, textColor.
 * Outputs: Renders inline badge pill.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawBadge(const char* text, const ImVec4& bgColor, const ImVec4& textColor) {
    if (m_fonts.captionFont != nullptr) {
        ImGui::PushFont(m_fonts.captionFont);
    }
    const ImVec2 tSz = visibleTextSize(text);
    const ImVec2 pad(9.0f, 3.0f);
    const ImVec2 pillSz(tSz.x + pad.x * 2.0f, tSz.y + pad.y * 2.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        pos,
        ImVec2(pos.x + pillSz.x, pos.y + pillSz.y),
        ThemeEngine::vec4ToU32(ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.22f)),
        pillSz.y * 0.5f
    );
    dl->AddRect(
        pos,
        ImVec2(pos.x + pillSz.x, pos.y + pillSz.y),
        ThemeEngine::vec4ToU32(ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.65f)),
        pillSz.y * 0.5f,
        0,
        1.0f
    );
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ThemeEngine::vec4ToU32(textColor), text, visibleLabelEnd(text));
    ImGui::Dummy(pillSz);

    if (m_fonts.captionFont != nullptr) {
        ImGui::PopFont();
    }
}

/*
 * Purpose: Draws a filled circular status indicator dot inline.
 * Inputs: color, radius.
 * Outputs: Renders dot and reserves layout space.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawStatusDot(const ImVec4& color, float radius) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius + 3.0f), radius, ThemeEngine::vec4ToU32(color), 16);
    ImGui::Dummy(ImVec2(radius * 2.0f + 4.0f, radius * 2.0f + 4.0f));
}

/*
 * Purpose: Draws a circular progress ring with centered percentage text via ImDrawList arcs.
 * Inputs: id, fraction01, radius, color, centerText.
 * Outputs: Renders progress ring widget.
 * Big-O: O(1) time (fixed 48 arc segments), O(1) space.
 */
void GuiRenderer::drawProgressRing(
    const char* /*id*/,
    float fraction01,
    float radius,
    const ImVec4& color,
    const char* centerText
) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + radius + 4.0f, pos.y + radius + 4.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddCircle(center, radius, ThemeEngine::vec4ToU32(pal.surface), 48, 6.0f);
    const float clamped = std::clamp(fraction01, 0.0f, 1.0f);
    if (clamped > 0.005f) {
        const float startA = -kPi * 0.5f;
        const float endA = startA + clamped * 2.0f * kPi;
        dl->PathArcTo(center, radius, startA, endA, 48);
        dl->PathStroke(ThemeEngine::vec4ToU32(color), 0, 6.0f);
    }

    if (centerText != nullptr) {
        const ImVec2 tSz = visibleTextSize(centerText);
        dl->AddText(ImVec2(center.x - tSz.x * 0.5f, center.y - tSz.y * 0.5f), ThemeEngine::vec4ToU32(pal.text), centerText, visibleLabelEnd(centerText));
    }
    ImGui::Dummy(ImVec2(radius * 2.0f + 8.0f, radius * 2.0f + 8.0f));
}

/*
 * Purpose: Renders a Primary CTA button with 120ms hover color lerp and 1.5px hover lift (Section 8).
 * Inputs: label, size, enabled.
 * Outputs: true if clicked while enabled.
 * Big-O: O(1) time, O(1) space.
 */
bool GuiRenderer::primaryButton(const char* label, const ImVec2& size, bool enabled) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec2 textSz = visibleTextSize(label);
    const ImVec2 btnSz(
        (size.x < 0.0f) ? ImGui::GetContentRegionAvail().x : ((size.x == 0.0f) ? (textSz.x + 28.0f) : size.x),
        (size.y <= 0.0f) ? 36.0f : size.y
    );

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, btnSz) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active  = enabled && ImGui::IsItemActive();

    float& hoverT = m_btnHoverT[label];
    const float step = ImGui::GetIO().DeltaTime / 0.12f; // 120ms lerp
    hoverT = std::clamp(hoverT + (hovered ? step : -step), 0.0f, 1.0f);

    ImVec4 bg = ThemeEngine::lerpColor(pal.accent, pal.accentHover, hoverT);
    if (active) {
        bg = pal.accentActive;
    } else if (!enabled) {
        bg = pal.surface;
    }

    const float liftY = hovered ? -1.5f : 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + liftY),
        ImVec2(pos.x + btnSz.x, pos.y + btnSz.y + liftY),
        ThemeEngine::vec4ToU32(bg),
        8.0f
    );
    const ImVec4 txtCol = enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : pal.dimText;
    dl->AddText(
        ImVec2(pos.x + (btnSz.x - textSz.x) * 0.5f, pos.y + liftY + (btnSz.y - textSz.y) * 0.5f),
        ThemeEngine::vec4ToU32(txtCol),
        label,
        visibleLabelEnd(label)
    );
    return clicked;
}

/*
 * Purpose: Renders a Ghost/Secondary button with border, 120ms hover color lerp, and subtle hover lift.
 * Inputs: label, size, enabled.
 * Outputs: true if clicked while enabled.
 * Big-O: O(1) time, O(1) space.
 */
bool GuiRenderer::ghostButton(const char* label, const ImVec2& size, bool enabled) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec2 textSz = visibleTextSize(label);
    const ImVec2 btnSz(
        (size.x < 0.0f) ? ImGui::GetContentRegionAvail().x : ((size.x == 0.0f) ? (textSz.x + 24.0f) : size.x),
        (size.y <= 0.0f) ? 34.0f : size.y
    );

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, btnSz) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();

    float& hoverT = m_btnHoverT[std::string("g_") + label];
    const float step = ImGui::GetIO().DeltaTime / 0.12f;
    hoverT = std::clamp(hoverT + (hovered ? step : -step), 0.0f, 1.0f);

    const ImVec4 bg = ThemeEngine::lerpColor(pal.surface, pal.hover, hoverT);
    const float liftY = hovered ? -1.0f : 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + liftY),
        ImVec2(pos.x + btnSz.x, pos.y + btnSz.y + liftY),
        ThemeEngine::vec4ToU32(bg),
        8.0f
    );
    dl->AddRect(
        ImVec2(pos.x, pos.y + liftY),
        ImVec2(pos.x + btnSz.x, pos.y + btnSz.y + liftY),
        ThemeEngine::vec4ToU32(hovered ? pal.accent : pal.border),
        8.0f,
        0,
        1.0f
    );
    dl->AddText(
        ImVec2(pos.x + (btnSz.x - textSz.x) * 0.5f, pos.y + liftY + (btnSz.y - textSz.y) * 0.5f),
        ThemeEngine::vec4ToU32(enabled ? pal.text : pal.dimText),
        label,
        visibleLabelEnd(label)
    );
    return clicked;
}

/*
 * Purpose: Renders a Danger button (red accent) with 120ms hover color lerp and hover lift.
 * Inputs: label, size, enabled.
 * Outputs: true if clicked while enabled.
 * Big-O: O(1) time, O(1) space.
 */
bool GuiRenderer::dangerButton(const char* label, const ImVec2& size, bool enabled) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec2 textSz = visibleTextSize(label);
    const ImVec2 btnSz(
        (size.x < 0.0f) ? ImGui::GetContentRegionAvail().x : ((size.x == 0.0f) ? (textSz.x + 24.0f) : size.x),
        (size.y <= 0.0f) ? 34.0f : size.y
    );

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, btnSz) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();

    float& hoverT = m_btnHoverT[std::string("d_") + label];
    const float step = ImGui::GetIO().DeltaTime / 0.12f;
    hoverT = std::clamp(hoverT + (hovered ? step : -step), 0.0f, 1.0f);

    const ImVec4 dangerBase = pal.statusOccupied;
    const ImVec4 dangerBright(
        std::min(1.0f, dangerBase.x + 0.08f),
        std::min(1.0f, dangerBase.y + 0.08f),
        std::min(1.0f, dangerBase.z + 0.08f),
        1.0f
    );
    const ImVec4 bg = enabled ? ThemeEngine::lerpColor(dangerBase, dangerBright, hoverT) : pal.surface;
    const float liftY = hovered ? -1.5f : 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + liftY),
        ImVec2(pos.x + btnSz.x, pos.y + btnSz.y + liftY),
        ThemeEngine::vec4ToU32(bg),
        8.0f
    );
    dl->AddText(
        ImVec2(pos.x + (btnSz.x - textSz.x) * 0.5f, pos.y + liftY + (btnSz.y - textSz.y) * 0.5f),
        ThemeEngine::vec4ToU32(enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : pal.dimText),
        label,
        visibleLabelEnd(label)
    );
    return clicked;
}

/*
 * Purpose: Displays a styled tooltip when the preceding ImGui item is hovered.
 * Inputs: text C-string.
 * Outputs: Renders tooltip popup if hovered.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawTooltip(const char* text) {
    if (text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text);
    }
}

/*
 * Purpose: Draws a consistent page/section heading with 28px title and optional dimmed subtitle.
 * Inputs: title, optional subtitle.
 * Outputs: Renders section header block.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawSectionHeader(const char* title, const char* subtitle) {
    const ThemePalette& pal = m_theme.getActivePalette();
    if (m_fonts.headingFont != nullptr) {
        ImGui::PushFont(m_fonts.headingFont);
    }
    ImGui::TextColored(pal.text, "%s", title);
    if (m_fonts.headingFont != nullptr) {
        ImGui::PopFont();
    }
    if (subtitle != nullptr && subtitle[0] != '\0') {
        if (m_fonts.captionFont != nullptr) {
            ImGui::PushFont(m_fonts.captionFont);
        }
        ImGui::TextColored(pal.dimText, "%s", subtitle);
        if (m_fonts.captionFont != nullptr) {
            ImGui::PopFont();
        }
    }
    ImGui::Spacing();
}

/*
 * Purpose: Draws a geometric vector EmptyState illustration with title and subtitle (Section 8).
 * Inputs: title, subtitle.
 * Outputs: Renders centered empty-state block.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawEmptyState(const char* title, const char* subtitle) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const float availW = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + availW * 0.5f, origin.y + 42.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, 28.0f, ThemeEngine::vec4ToU32(ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.14f)), 32);
    dl->AddRect(
        ImVec2(center.x - 14.0f, center.y - 10.0f),
        ImVec2(center.x + 14.0f, center.y + 10.0f),
        ThemeEngine::vec4ToU32(pal.accent),
        4.0f,
        0,
        2.0f
    );
    dl->AddLine(ImVec2(center.x - 10.0f, center.y - 3.0f), ImVec2(center.x + 10.0f, center.y - 3.0f), ThemeEngine::vec4ToU32(pal.secondaryAccent), 1.8f);
    dl->AddLine(ImVec2(center.x - 10.0f, center.y + 4.0f), ImVec2(center.x + 4.0f, center.y + 4.0f), ThemeEngine::vec4ToU32(pal.dimText), 1.8f);

    ImGui::Dummy(ImVec2(0.0f, 82.0f));
    const ImVec2 tSz = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX((availW - tSz.x) * 0.5f);
    ImGui::TextColored(pal.text, "%s", title);

    if (subtitle != nullptr) {
        const ImVec2 sSz = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorPosX(std::max(0.0f, (availW - sSz.x) * 0.5f));
        ImGui::TextColored(pal.dimText, "%s", subtitle);
    }
}

/*
 * Purpose: Draws crisp vector navigation icons using ImDrawList primitives (zero external icon fonts, Section 8).
 * Inputs: dl (ImDrawList*), glyph (NavGlyph), center, size (half-extent), color.
 * Outputs: Appends vector primitives to draw list.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::drawVectorGlyph(
    ImDrawList* dl,
    NavGlyph glyph,
    const ImVec2& c,
    float s,
    ImU32 col
) const {
    switch (glyph) {
        case NavGlyph::Dashboard:
            dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x - 1.5f, c.y - 1.5f), col, 2.0f, 0, 1.6f);
            dl->AddRect(ImVec2(c.x + 1.5f, c.y - s), ImVec2(c.x + s, c.y - 1.5f), col, 2.0f, 0, 1.6f);
            dl->AddRect(ImVec2(c.x - s, c.y + 1.5f), ImVec2(c.x - 1.5f, c.y + s), col, 2.0f, 0, 1.6f);
            dl->AddRect(ImVec2(c.x + 1.5f, c.y + 1.5f), ImVec2(c.x + s, c.y + s), col, 2.0f, 0, 1.6f);
            break;
        case NavGlyph::Rooms:
            dl->AddRect(ImVec2(c.x - s, c.y - s * 0.7f), ImVec2(c.x + s, c.y + s * 0.8f), col, 2.0f, 0, 1.6f);
            dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, 1.4f);
            dl->AddLine(ImVec2(c.x, c.y - s * 0.7f), ImVec2(c.x, c.y + s * 0.8f), col, 1.4f);
            break;
        case NavGlyph::CheckIn:
            dl->AddCircle(c, s, col, 16, 1.6f);
            dl->AddLine(ImVec2(c.x - 3.5f, c.y), ImVec2(c.x - 0.5f, c.y + 3.5f), col, 1.8f);
            dl->AddLine(ImVec2(c.x - 0.5f, c.y + 3.5f), ImVec2(c.x + 4.5f, c.y - 3.5f), col, 1.8f);
            break;
        case NavGlyph::Timeline:
            dl->AddLine(ImVec2(c.x - s, c.y - 4.5f), ImVec2(c.x + 2.0f, c.y - 4.5f), col, 2.2f);
            dl->AddLine(ImVec2(c.x - 3.0f, c.y), ImVec2(c.x + s, c.y), col, 2.2f);
            dl->AddLine(ImVec2(c.x - s, c.y + 4.5f), ImVec2(c.x + 4.0f, c.y + 4.5f), col, 2.2f);
            break;
        case NavGlyph::Guests:
            dl->AddCircle(ImVec2(c.x, c.y - 3.0f), 3.5f, col, 14, 1.6f);
            dl->PathArcTo(ImVec2(c.x, c.y + 8.5f), 7.0f, kPi * 1.15f, kPi * 1.85f, 12);
            dl->PathStroke(col, 0, 1.6f);
            break;
        case NavGlyph::Bookings:
        case NavGlyph::Waitlist:
            dl->AddRect(ImVec2(c.x - s, c.y - s * 0.8f), ImVec2(c.x + s, c.y + s * 0.8f), col, 2.0f, 0, 1.6f);
            dl->AddLine(ImVec2(c.x - s, c.y - 2.5f), ImVec2(c.x + s, c.y - 2.5f), col, 1.4f);
            break;
        case NavGlyph::Billing:
            dl->AddRect(ImVec2(c.x - s * 0.8f, c.y - s), ImVec2(c.x + s * 0.8f, c.y + s), col, 2.0f, 0, 1.6f);
            dl->AddLine(ImVec2(c.x - 3.5f, c.y - 2.5f), ImVec2(c.x + 3.5f, c.y - 2.5f), col, 1.4f);
            dl->AddLine(ImVec2(c.x - 3.5f, c.y + 2.0f), ImVec2(c.x + 2.0f, c.y + 2.0f), col, 1.4f);
            break;
        case NavGlyph::Yield:
        case NavGlyph::Analytics:
            dl->AddLine(ImVec2(c.x - s, c.y + s * 0.7f), ImVec2(c.x - 2.0f, c.y), col, 1.8f);
            dl->AddLine(ImVec2(c.x - 2.0f, c.y), ImVec2(c.x + 2.0f, c.y + 2.5f), col, 1.8f);
            dl->AddLine(ImVec2(c.x + 2.0f, c.y + 2.5f), ImVec2(c.x + s, c.y - s * 0.7f), col, 1.8f);
            break;
        case NavGlyph::Housekeeping:
        case NavGlyph::Kanban:
            dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x - 3.0f, c.y + s), col, 1.5f, 0, 1.5f);
            dl->AddRect(ImVec2(c.x - 1.5f, c.y - s), ImVec2(c.x + 3.0f, c.y + 3.0f), col, 1.5f, 0, 1.5f);
            dl->AddRect(ImVec2(c.x + 4.5f, c.y - s), ImVec2(c.x + s, c.y + s * 0.6f), col, 1.5f, 0, 1.5f);
            break;
        case NavGlyph::Audit:
            dl->AddQuad(
                ImVec2(c.x, c.y - s),
                ImVec2(c.x + s * 0.85f, c.y - s * 0.3f),
                ImVec2(c.x, c.y + s),
                ImVec2(c.x - s * 0.85f, c.y - s * 0.3f),
                col,
                1.6f
            );
            break;
        default:
            dl->AddCircle(c, s * 0.75f, col, 16, 1.6f);
            dl->AddCircleFilled(c, 2.2f, col, 10);
            break;
    }
}

/*
 * Purpose: Maps RoomStatus to its exact Section 8 status color from ThemePalette.
 * Inputs: status (RoomStatus).
 * Outputs: ImVec4 color.
 * Big-O: O(1) time, O(1) space.
 */
ImVec4 GuiRenderer::statusColor(RoomStatus status) const {
    const ThemePalette& pal = m_theme.getActivePalette();
    switch (status) {
        case RoomStatus::Available:        return pal.statusAvailable;
        case RoomStatus::Occupied:         return pal.statusOccupied;
        case RoomStatus::Reserved:         return pal.statusReserved;
        case RoomStatus::CleaningRequired: return pal.statusCleaning;
        case RoomStatus::Maintenance:      return pal.statusMaintenance;
    }
    return pal.statusAvailable;
}

/*
 * Purpose: Maps LoyaltyTier to its exact Section 8 tier color from ThemePalette.
 * Inputs: tier (LoyaltyTier).
 * Outputs: ImVec4 color.
 * Big-O: O(1) time, O(1) space.
 */
ImVec4 GuiRenderer::tierColor(LoyaltyTier tier) const {
    const ThemePalette& pal = m_theme.getActivePalette();
    switch (tier) {
        case LoyaltyTier::Standard: return pal.tierStandard;
        case LoyaltyTier::Silver:   return pal.tierSilver;
        case LoyaltyTier::Gold:     return pal.tierGold;
        case LoyaltyTier::Platinum: return pal.tierPlatinum;
    }
    return pal.tierStandard;
}

/*
 * Purpose: Returns the display breadcrumb title for each ScreenId.
 * Inputs: screen (ScreenId).
 * Outputs: Constant C-string name.
 * Big-O: O(1) time, O(1) space.
 */
const char* GuiRenderer::screenDisplayName(ScreenId screen) {
    switch (screen) {
        case ScreenId::Dashboard:           return "Dashboard";
        case ScreenId::RoomMatrix:          return "Interactive Room Matrix";
        case ScreenId::SmartCheckIn:        return "Smart Check-In Wizard";
        case ScreenId::Timeline:            return "Booking Timeline (Gantt)";
        case ScreenId::Guests:              return "Guest CRM Registry";
        case ScreenId::Bookings:            return "Bookings Management";
        case ScreenId::Billing:             return "Billing & Invoices";
        case ScreenId::Waitlist:            return "FIFO Waitlist Queue";
        case ScreenId::YieldManagement:     return "Dynamic Yield Management";
        case ScreenId::Analytics:           return "Executive Analytics";
        case ScreenId::HousekeepingBoard:   return "Housekeeping Dispatch Board";
        case ScreenId::ServiceOrdersKanban: return "Service Orders Kanban";
        case ScreenId::Staff:               return "Staff & RBAC Accounts";
        case ScreenId::AuditLog:            return "Tamper-Evident Audit Log";
        case ScreenId::Backups:             return "Snapshot Backups";
        case ScreenId::Settings:            return "System Settings";
        case ScreenId::VivaTools:           return "Viva Tools & Algorithm X-Ray";
    }
    return "Dashboard";
}

} // namespace hms
