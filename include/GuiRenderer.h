#ifndef HMS_GUI_RENDERER_H
#define HMS_GUI_RENDERER_H

#include "HotelSystem.h"
#include "Models.h"
#include "Theme.h"

#include "imgui.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace hms {

/* Returns the end of the visible part of an ImGui label (before any "##id" suffix). O(N) time, O(1) space. */
inline const char* visibleLabelEnd(const char* label) {
    if (label == nullptr) {
        return nullptr;
    }
    const char* p = label;
    while (*p != '\0') {
        if (p[0] == '#' && p[1] == '#') {
            break;
        }
        ++p;
    }
    return p;
}

/* Measures only the visible portion of a label (ignores "##id"). O(N) time, O(1) space. */
inline ImVec2 visibleTextSize(const char* label) { return ImGui::CalcTextSize(label, nullptr, true); }

// =============================================================================
// 1. SCREEN ROUTING & NAVIGATION DEFINITIONS (Section 7)
// =============================================================================
enum class ScreenId : int32_t {
    Dashboard = 0,
    RoomMatrix,
    SmartCheckIn,
    Timeline,
    Guests,
    Bookings,
    Billing,
    Waitlist,
    YieldManagement,
    Analytics,
    HousekeepingBoard,
    ServiceOrdersKanban,
    Staff,
    AuditLog,
    Backups,
    Settings,
    VivaTools
};

// =============================================================================
// 2. TOAST NOTIFICATION SYSTEM (Section 8)
// =============================================================================
struct ToastItem {
    int         id        = 0;
    NoticeType  type      = NoticeType::Info;
    std::string title     = "";
    std::string message   = "";
    float       elapsed   = 0.0f;
    float       duration  = 4.0f;
    bool        dismissed = false;
    int         relatedWaitlistId = -1;
    int         relatedRoomId     = -1;
};

class Toast {
public:
    static void push(NoticeType type, const std::string& title, const std::string& message,
                     int relatedWaitlistId = -1, int relatedRoomId = -1);
    static void updateAndRender(float deltaTime, const ThemePalette& palette, const FontBank& fonts);
    static void clear();

private:
    static std::vector<ToastItem>& items();
    static int& nextId();
};

// =============================================================================
// 3. SHARED REUSABLE UI WIDGETS (Section 8)
// =============================================================================
enum class NavGlyph : int32_t {
    Dashboard = 0,
    Rooms,
    CheckIn,
    Timeline,
    Guests,
    Bookings,
    Billing,
    Waitlist,
    Yield,
    Analytics,
    Housekeeping,
    Kanban,
    Staff,
    Audit,
    Backups,
    Settings,
    Viva,
    Logout
};

struct NavItemSpec {
    ScreenId    screen = ScreenId::Dashboard;
    const char* label  = "";
    NavGlyph    glyph  = NavGlyph::Dashboard;
};

struct LoginParticle {
    float x      = 0.0f;
    float y      = 0.0f;
    float vx     = 0.0f;
    float vy     = 0.0f;
    float radius = 3.0f;
    float alpha  = 0.25f;
};

struct KpiAnimState {
    double startVal = 0.0;
    double targetVal = 0.0;
    float  elapsed  = 0.6f;
};

struct ConfettiParticle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float size = 5.0f;
    ImVec4 color{1.0f, 0.8f, 0.2f, 1.0f};
};

// State for Interactive Floor-Plan Room Matrix & Room NNN Modal (Section 9.2)
struct RoomMatrixState {
    bool statusFilter[5]    = {true, true, true, true, true};
    int  typeFilterIndex    = 0; // 0=All, 1..4=RoomType
    int  floorFilterIndex   = 0; // 0=All, 1..5=Floor
    char searchBuf[64]{};

    int  selectedRoomId     = -1;
    bool openRoomModalReq   = false;
    bool showSwapPicker     = false;
    bool showMaintForm      = false;
    bool showChargeForm     = false;
    bool showCheckoutForm   = false;

    int  maintSeverity      = 3;
    char maintNoteBuf[128]{};

    int   chargeTypeIdx     = 0;
    char  chargeDescBuf[96]{};
    float chargeUnitPrice   = 1500.0f;
    int   chargeQty         = 1;

    int   checkoutPayMethodIdx = 0; // 0 Cash, 1 Card, 2 Bank Transfer
};

// State for 4-Step Smart Check-In Wizard (Section 9.3)
struct SmartCheckInState {
    int  step                = 0; // 0..3
    int  preBookedBookingId  = -1;
    int  selectedGuestId     = -1;
    int  selectedRoomId      = -1;

    char guestSearchBuf[64]{};
    char cnicExactBuf[32]{};
    bool showNewGuestForm    = false;

    // New Guest inline registration form
    char newNameBuf[64]{};
    char newCnicBuf[32]{};
    char newPassportBuf[32]{};
    char newPhoneBuf[32]{};
    char newEmailBuf[64]{};
    char newNationalityBuf[48] = "Pakistan";
    char newAddressBuf[96]{};
    bool newIsForeign        = false;

    // Step 2: Stay Details
    int  checkInOffsetDays   = 0; // 0 = today
    int  nights              = 2;
    int  adults              = 2;
    int  children            = 0;
    int  promoIndex          = 0; // 0 None, 1 WELCOME10, 2 SAVE500, 3 STAY15
    bool immediateCheckIn    = true;

    // Step 3: Preferences & Cached Recommendations
    GuestPreferences                  prefs{};
    std::vector<RoomFeatureBreakdown> rankedBreakdowns{};
    bool                              stlCrossCheckOk = true;
};

// State for Gantt Timeline (Section 9.6)
struct TimelineState {
    int   startDayOffset   = -3; // Relative to today
    float dayColumnWidth   = 38.0f;
    bool  floorExpanded[5] = {true, true, true, true, true};
    int   resizingBookingId = -1;
    int   conflictFlashBookingId = -1;
    float conflictFlashTimer = 0.0f;
};

// State for Guests CRM Table & Drawer (Section 9.4)
struct GuestsCrmState {
    char searchBuf[64]{};
    int  sortColumn      = 0; // 0 ID, 1 Name, 2 Tier, 3 Stays, 4 Spend
    bool sortAscending   = true;
    int  currentPage     = 0;
    int  selectedGuestId = -1;
    bool openAddModal    = false;

    char formName[64]{};
    char formCnic[32]{};
    char formPassport[32]{};
    char formPhone[32]{};
    char formEmail[64]{};
    char formNationality[48] = "Pakistan";
    char formAddress[96]{};
    char formPrefs[128]{};
    bool formIsForeign   = false;
};

// State for Bookings, Billing & Invoice Preview Modal (Section 9.5, 9.6)
struct OperationsState {
    int  bookingStatusFilter = 0; // 0 All, 1 Reserved, 2 CheckedIn, 3 CheckedOut, 4 Cancelled, 5 NoShow
    char bookingSearchBuf[64]{};

    int  billingPaidFilter   = 0; // 0 All, 1 Paid, 2 Unpaid
    char billingSearchBuf[64]{};

    int         previewInvoiceId   = -1;
    bool        openPreviewModal   = false;
    std::string previewTxtPath     = "";
    std::string previewHtmlPath    = "";

    // Waitlist quick-add state
    int   wlGuestId  = -1;
    int   wlTypeIdx  = 0;
    int   wlInOff    = 1;
    int   wlNights   = 2;
    float wlBudget   = 15000.0f;
};

// State for Yield Management Simulator, Housekeeping Board, Kanban, X-Ray & Overlays (Phase 7)
struct YieldSimState {
    float occSliderPct     = 76.0f;
    bool  peakSeason       = false;
    bool  weekendNight     = true;
    float elasticityEps    = -1.2f;
    float overrideMult     = 1.15f;
    char  overrideReason[96]{};
};

struct KanbanState {
    int   newOrderBookingId = -1;
    int   newOrderTypeIdx   = 2; // Dining
    char  newOrderItem[64]{};
    int   newOrderQty       = 1;
    float newOrderPrice     = 1800.0f;
};

struct XRayVisualiserState {
    std::vector<SortBenchmarkRow> benchmarks{};
    int                           visMode       = 0; // 0 MergeSort, 1 Min-Heap
    std::vector<SortStepSnapshot> visSteps{};
    int                           visStepIdx    = 0;
    bool                          visPlaying    = false;
    float                         visStepTimer  = 0.0f;
};

struct CommandPaletteState {
    bool  isOpen        = false;
    float openAnimT     = 0.0f;
    char  queryBuf[96]{};
    int   selectedIndex = 0;
    bool  focusInput    = false;
};

struct StaffAdminState {
    char newUserBuf[48]{};
    char newPassBuf[48]{};
    char newNameBuf[64]{};
    int  newRoleIdx = 1; // Receptionist
};

// State for Guided 90-Second Demo Mode, Backups UI & Seeder/Reset Confirm Modals (Phase 8)
struct GuidedDemoState {
    bool        active      = false;
    float       elapsed     = 0.0f;
    int         lastStage   = -1;
    std::string captionText = "";
};

struct BackupsAndSeederState {
    std::string pendingRestoreSnap   = "";
    bool        openRestoreModal     = false;
    bool        openSeedConfirmModal = false;
    bool        openResetStep1Modal  = false;
    bool        openResetStep2Modal  = false;
    std::string lastNightAuditPath   = "";
    std::string lastNightAuditSummary = "";
};

struct OverflowRecord {
    std::string screen;
    std::string cardId;
    float       width      = 0.0f;
    float       height     = 0.0f;
    float       scrollMaxY = 0.0f;
};

class GuiRenderer {
public:
    GuiRenderer(
        HotelSystem& system,
        ThemeEngine& themeEngine,
        const FontBank& fonts,
        const std::vector<SelfTestEntry>& startupSelfTests
    );

    // Renders one complete frame (Splash -> Login -> Main Authenticated Shell + Toasts)
    void renderFrame(float deltaTime);

    // Navigation & screen transition (180ms fade + 12px slide-in)
    void navigateTo(ScreenId screen);
    ScreenId activeScreen() const { return m_activeScreen; }
    bool isSplashFinished() const { return m_splashElapsed >= kSplashDuration; }

    // For automated smoke testing: skip splash, force Viva tab, and detect child overflow
    void skipSplash() { m_splashElapsed = kSplashDuration; }
    void setForcedVivaTab(int tabIdx) { m_forcedVivaTab = tabIdx; }
    void setOverflowDetectionEnabled(bool enabled) { m_overflowDetectEnabled = enabled; }
    void clearOverflowRecords() { m_overflowRecords.clear(); }
    const std::vector<OverflowRecord>& overflowRecords() const { return m_overflowRecords; }
    void checkChildOverflow(const char* cardId);

    // -------------------------------------------------------------------------
    // Shared Widgets (Section 8 — write once, reuse everywhere)
    // -------------------------------------------------------------------------
    bool beginCard(const char* id, const char* title, const ImVec2& size = ImVec2(0.0f, 0.0f), bool autoHeight = false);
    void endCard();
    void drawKpiTile(
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
    );
    void drawBadge(const char* text, const ImVec4& bgColor, const ImVec4& textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    void drawStatusDot(const ImVec4& color, float radius = 5.0f);
    void drawProgressRing(const char* id, float fraction01, float radius, const ImVec4& color, const char* centerText);
    bool primaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool enabled = true);
    bool ghostButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool enabled = true);
    bool dangerButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool enabled = true);
    void drawTooltip(const char* text);
    void drawSectionHeader(const char* title, const char* subtitle = nullptr);
    void drawEmptyState(const char* title, const char* subtitle);
    void drawVectorGlyph(ImDrawList* dl, NavGlyph glyph, const ImVec2& center, float size, ImU32 color) const;

    // Color lookup helpers using ThemePalette
    ImVec4 statusColor(RoomStatus status) const;
    ImVec4 tierColor(LoyaltyTier tier) const;
    static const char* screenDisplayName(ScreenId screen);

private:
    // Core shell screens (src/GuiRenderer.cpp)
    void renderSplashScreen(float deltaTime);
    void renderLoginScreen(float deltaTime);
    void renderAuthenticatedShell(float deltaTime);
    void renderSidebar(float topBarHeight, float statusBarHeight);
    void renderTopBar(float height);
    void renderStatusBar(float height);
    void renderActiveScreenContent();
    void renderRoleAllowedNavItems(Role role, std::vector<NavItemSpec>& outItems) const;
    void handleGlobalShortcuts();
    void syncSystemNoticesToToasts();

    // Role-specific dashboards (src/Gui_Dashboard.cpp)
    void renderDashboardScreen();
    void renderAdminDashboard();
    void renderReceptionistDashboard();
    void renderHousekeepingDashboard();
    void drawRoomStatusDonutChart(const int statusCounts[5], int totalRooms);

    // Room Matrix, Room NNN Modal & Smart Check-In Wizard (src/Gui_Rooms.cpp)
    void renderRoomMatrixScreen();
    void renderRoomDetailModal();
    void renderSmartCheckInScreen();
    void launchSmartCheckInForRoom(int roomId, int existingBookingId = -1);
    void triggerLoyaltyConfetti();
    void updateAndRenderConfetti(float deltaTime);

    // Timeline (Gantt), Guests CRM, Bookings, Billing, Invoice Preview & Waitlist (src/Gui_Operations.cpp)
    void renderTimelineScreen();
    void renderGuestsCrmScreen();
    void renderBookingsScreen();
    void renderBillingScreen();
    void renderWaitlistScreen();
    void renderInvoicePreviewModal();
    void openInvoicePreview(int invoiceId);

    // Yield, Analytics, Housekeeping Dispatch, Kanban, Staff, Audit, Backups, Settings, X-Ray, Guided Demo & Overlays (src/Gui_Admin.cpp)
    void renderYieldManagementScreen();
    void renderAnalyticsScreen();
    void renderHousekeepingBoardScreen();
    void renderServiceOrdersKanbanScreen();
    void renderStaffScreen();
    void renderAuditLogScreen();
    void renderBackupsScreen();
    void renderSettingsScreen();
    void renderVivaToolsScreen();
    void renderCommandPaletteOverlay(float deltaTime);
    void renderShortcutsOverlay();
    void updateAndRenderGuidedDemo(float deltaTime);
    void startGuidedDemo();
    void draw90DayOccupancyHeatmap();

    // Placeholder-free screen overview for screens scheduled in Phase 8 (Backups)
    void renderUpcomingModulePreview(ScreenId screen);

    HotelSystem&               m_system;
    ThemeEngine&               m_theme;
    FontBank                   m_fonts;
    std::vector<SelfTestEntry> m_startupSelfTests;

    // Splash & Login state
    static constexpr float     kSplashDuration = 1.8f;
    float                      m_splashElapsed = 0.0f;
    std::vector<LoginParticle> m_particles{};
    char                       m_loginUserBuf[64]{};
    char                       m_loginPassBuf[64]{};
    bool                       m_showPassword = false;
    std::string                m_loginErrorMsg = "";

    // Shell layout & animation state
    ScreenId                   m_activeScreen      = ScreenId::Dashboard;
    float                      m_pageAnimElapsed   = 0.18f;
    static constexpr float     kPageAnimDuration   = 0.18f;
    bool                       m_sidebarCollapsed  = false;
    float                      m_sidebarWidth      = 230.0f;
    char                       m_globalSearchBuf[128]{};
    bool                       m_notifPopupOpen    = false;

    // Widget hover & KPI count-up animation maps
    std::unordered_map<std::string, float>        m_btnHoverT{};
    std::unordered_map<std::string, KpiAnimState> m_kpiAnims{};

    // Phase 5 screens state & loyalty confetti burst (1.5s)
    RoomMatrixState               m_matrixState{};
    SmartCheckInState             m_wizardState{};
    std::vector<ConfettiParticle> m_confetti{};
    float                         m_confettiRemaining = 0.0f;

    // Phase 6 screens state
    TimelineState                 m_timelineState{};
    GuestsCrmState                m_crmState{};
    OperationsState               m_opsState{};

    // Phase 7 screens & overlays state
    YieldSimState                 m_yieldState{};
    KanbanState                   m_kanbanState{};
    XRayVisualiserState           m_xrayState{};
    CommandPaletteState           m_cmdPalette{};
    StaffAdminState               m_staffState{};
    bool                          m_showShortcutsOverlay = false;
    char                          m_hkReassignBuf[48]    = "Housekeeping Team";

    // Phase 8 state (Guided Demo, Backups, Seeder & Night Audit)
    GuidedDemoState               m_demoState{};
    BackupsAndSeederState         m_adminOpsState{};

    // Debug-only overflow detector state (inactive in normal use)
    bool                          m_overflowDetectEnabled = false;
    int                           m_forcedVivaTab         = -1;
    std::vector<std::string>      m_cardIdStack{};
    std::vector<OverflowRecord>   m_overflowRecords{};
};

} // namespace hms

#endif // HMS_GUI_RENDERER_H
