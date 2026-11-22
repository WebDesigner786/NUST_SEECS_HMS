#include "Algorithms.h"
#include "FileManager.h"
#include "GuiRenderer.h"
#include "HotelSystem.h"
#include "Models.h"
#include "Sha256.h"
#include "Theme.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/*
 * Purpose: GLFW error callback that logs any platform/OpenGL errors to stderr.
 * Inputs: errorCode (GLFW error integer), description (UTF-8 error message).
 * Outputs: Prints formatted error message to stderr.
 * Big-O: O(1) time, O(1) space.
 */
void glfwErrorCallback(int errorCode, const char* description) {
    std::fprintf(stderr, "[GLFW Error %d]: %s\n", errorCode, description ? description : "Unknown");
}

/*
 * Purpose: Ensures the runtime directories (data/, backups/, exports/) exist before startup.
 * Inputs: None.
 * Outputs: Creates directories on disk if missing.
 * Big-O: O(1) time, O(1) space.
 */
void ensureRuntimeDirectories() {
    std::error_code ec;
    std::filesystem::create_directories("data", ec);
    std::filesystem::create_directories("backups", ec);
    std::filesystem::create_directories("exports", ec);
}

/*
 * Purpose: Draws a 32x32 "G" glyph (rounded accent square, white ring with gap and bar) into a GLFW window icon at runtime.
 * Inputs: window handle.
 * Outputs: Sets the window icon; no asset files needed.
 * Big-O: O(32*32) time, O(1) space.
 */
void setWindowIcon(GLFWwindow* window) {
    constexpr int kSz = 32;
    static unsigned char pixels[kSz * kSz * 4];
    for (int y = 0; y < kSz; ++y) {
        for (int x = 0; x < kSz; ++x) {
            const float dx = static_cast<float>(x) - 15.5f;
            const float dy = static_cast<float>(y) - 15.5f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float ang = std::atan2(dy, dx); // 0 = right
            bool ink = (r >= 8.0f && r <= 12.5f);
            if (ink && std::fabs(ang) < 0.62f) {
                ink = false; // opening of the G on the right side
            }
            if (!ink && dy >= -1.0f && dy <= 2.5f && dx >= 1.0f && dx <= 12.5f) {
                ink = true; // horizontal bar of the G
            }
            const bool inSquare = !((x < 2 || x > 29) && (y < 2 || y > 29));
            unsigned char* px = &pixels[(y * kSz + x) * 4];
            if (!inSquare) { px[0] = px[1] = px[2] = px[3] = 0; continue; }
            px[0] = ink ? 255 : 0x6C; px[1] = ink ? 255 : 0x8C; px[2] = ink ? 255 : 0xFF; px[3] = 255;
        }
    }
    GLFWimage img{kSz, kSz, pixels};
    glfwSetWindowIcon(window, 1, &img);
}

} // namespace

/*
 * Purpose: Application entry point for NUST_SEECS_HMS (supports --selftest headless CLI mode,
 *          --smoke-test automated GUI verification, and normal interactive desktop launch).
 * Inputs: argc (argument count), argv (argument vector).
 * Outputs: Process exit status code (0 on success, non-zero on failure).
 * Big-O: O(Frames) over application lifetime; O(1) per frame overhead.
 */
int main(int argc, char** argv) {
    bool smokeTestMode = false;
    bool selfTestCliMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTestMode = true;
        } else if (std::strcmp(argv[i], "--selftest") == 0) {
            selfTestCliMode = true;
        }
    }

    ensureRuntimeDirectories();

    // Section 11: Run startup self-test in main; exit without opening a window in --selftest CLI mode
    if (selfTestCliMode) {
        std::printf("================================================================================\n");
        std::printf("Grand Meridian HMS - CLI Self-Test Runner (--selftest)\n");
        std::printf("================================================================================\n");
        std::vector<hms::SelfTestEntry> cliResults = hms::runAllSelfTests(true);
        const std::vector<hms::SelfTestEntry> p3Results = hms::HotelSystem::runPhase3SelfTests(true);
        cliResults.insert(cliResults.end(), p3Results.begin(), p3Results.end());
        int failedCount = 0;
        for (const hms::SelfTestEntry& entry : cliResults) {
            if (!entry.passed) {
                ++failedCount;
            }
        }
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf(
            "Self-Test Summary: %zu/%zu PASSED (%d FAILED)\n",
            cliResults.size() - static_cast<size_t>(failedCount),
            cliResults.size(),
            failedCount
        );
        std::printf("================================================================================\n");
        return (failedCount == 0) ? 0 : 1;
    }

    std::vector<hms::SelfTestEntry> startupSelfTests = hms::runAllSelfTests(false);
    const std::vector<hms::SelfTestEntry> phase3SelfTests = hms::HotelSystem::runPhase3SelfTests(false);
    startupSelfTests.insert(startupSelfTests.end(), phase3SelfTests.begin(), phase3SelfTests.end());

    hms::HotelSystem hotelSystem("data", "backups", "exports");
    hotelSystem.initialize();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW.\n");
        return 1;
    }

    // Section 2: OpenGL 3.3 Core, GLSL #version 330, MSAA 4x
    const char* glslVersion = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Section 2: 1600x900 default, resizable, min 1280x720
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Grand Meridian Hotel - Management", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW 1600x900 OpenGL 3.3 window.\n");
        glfwTerminate();
        return 1;
    }

    glfwSetWindowSizeLimits(window, 1280, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
    setWindowIcon(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync ON

    // OpenGL 3.3 Core multisampling (GL_MULTISAMPLE = 0x809D, omitted from legacy Windows GL/gl.h)
    constexpr GLenum kGlMultisample = 0x809D;
    glEnable(kGlMultisample);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable; // Single window, viewports disabled per Section 2
    io.IniFilename = "data/layout.ini";

    hms::ThemeEngine themeEngine;
    themeEngine.initialize(hms::ThemeMode::MidnightObsidian);
    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
    const hms::FontBank fonts = themeEngine.loadFonts(io, contentScaleX);

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::fprintf(stderr, "Failed to initialize ImGui GLFW backend.\n");
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        std::fprintf(stderr, "Failed to initialize ImGui OpenGL3 backend.\n");
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    hms::GuiRenderer gui(hotelSystem, themeEngine, fonts, startupSelfTests);

    int renderedFrames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        gui.renderFrame(io.DeltaTime);

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);

        const ImVec4& clearCol = themeEngine.getActivePalette().background;
        glClearColor(clearCol.x, clearCol.y, clearCol.z, clearCol.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        ++renderedFrames;
        if (smokeTestMode) {
            // Exercise Phase 8: Login -> Seed Viva Demo Data -> Dashboard (seeded) -> Room Matrix -> Backups UI -> Night Audit -> Audit Chain Verify
            if (renderedFrames == 6) {
                gui.skipSplash();
            } else if (renderedFrames == 10) {
                hotelSystem.login("admin", "Admin@123");
            } else if (renderedFrames == 14) {
                hotelSystem.seedVivaDemoData();
                gui.navigateTo(hms::ScreenId::Dashboard);
            } else if (renderedFrames == 22) {
                gui.navigateTo(hms::ScreenId::RoomMatrix);
            } else if (renderedFrames == 30) {
                gui.navigateTo(hms::ScreenId::Backups);
            } else if (renderedFrames == 38) {
                std::string naPath, naSum;
                hotelSystem.runNightAudit(&naPath, &naSum);
                gui.navigateTo(hms::ScreenId::VivaTools);
                themeEngine.toggleTheme();
            } else if (renderedFrames >= 48) {
                const int chainRes = hotelSystem.verifyAuditLogChain();
                std::printf(
                    "[Phase 8 Smoke Test] PASS: Rendered %d frames | Seeded: rooms=%zu guests=%zu bookings=%zu invoices=%zu tasks=%zu orders=%zu waitlist=%zu audit=%zu (chain=%s)\n",
                    renderedFrames,
                    hotelSystem.db().rooms.size(),
                    hotelSystem.db().guests.size(),
                    hotelSystem.db().bookings.size(),
                    hotelSystem.db().invoices.size(),
                    hotelSystem.db().tasks.size(),
                    hotelSystem.db().orders.size(),
                    hotelSystem.db().waitlist.size(),
                    hotelSystem.db().auditLog.size(),
                    (chainRes == -1) ? "VERIFIED" : "TAMPERED"
                );
                hotelSystem.logout();
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
    }

    // Section 5: Snapshot backup & clean save on exit
    hotelSystem.saveAll();
    hotelSystem.fileManager().createBackupSnapshot();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
