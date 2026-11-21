#ifndef HMS_HOTEL_SYSTEM_H
#define HMS_HOTEL_SYSTEM_H

#include "Algorithms.h"
#include "FileManager.h"
#include "Models.h"

#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <stack>
#include <string>
#include <vector>

namespace hms {

// =============================================================================
// 1. RBAC ACTIONS & NOTIFICATIONS (Section 7 & Section 8)
// =============================================================================
enum class Action : int32_t {
    ViewDashboard = 0,
    ViewRoomMatrix,
    UpdateRoomStatus,
    SmartCheckIn,
    CreateBooking,
    EditBooking,
    CancelBooking,
    CheckInGuest,
    CheckOutGuest,
    RoomSwap,
    AddServiceCharge,
    ManageGuests,
    ManageBilling,
    ExportInvoice,
    ManageWaitlist,
    ManageHousekeeping,
    ManageServiceOrders,
    ViewYieldManagement,
    OverridePricing,
    ViewAnalytics,
    ManageStaff,
    ViewAuditLog,
    ManageBackups,
    ManageSettings,
    RunNightAudit,
    UseVivaTools
};

enum class NoticeType : int32_t {
    Success = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3
};

struct SystemNotice {
    NoticeType  type    = NoticeType::Info;
    std::string title   = "";
    std::string message = "";
    int         relatedWaitlistId = -1; // >0 when waitlist auto-match offers [Book Now]
    int         relatedRoomId     = -1;
};

// =============================================================================
// 2. BILLING FOLIO BREAKDOWN & PROMO CODES (Section 9.5)
// =============================================================================
struct PromoCodeRule {
    std::string code        = "";
    std::string description = "";
    bool        isFlat      = false;
    double      value       = 0.0; // fraction (e.g. 0.10) if !isFlat, or PKR amount if isFlat
    int         minNights   = 1;
};

struct FolioBreakdown {
    int         nights            = 1;
    double      nightRate         = 0.0;
    double      roomSubtotal      = 0.0;
    double      addOnSubtotal     = 0.0;
    double      lateCheckoutFee   = 0.0;
    double      loyaltyDiscount   = 0.0;
    double      promoDiscount     = 0.0;
    double      taxableAmount     = 0.0;
    double      taxRate           = Cfg::TAX_RATE;
    double      taxAmount         = 0.0;
    double      grandTotal        = 0.0;
    bool        freeBreakfastPerk = false;
    bool        lateCheckoutWaived = false;
    std::string appliedPromoCode  = "";
};

// =============================================================================
// 3. UNDO/REDO COMMAND PATTERN (Section 9.10 #2)
// =============================================================================
struct UndoCommand {
    std::string           name   = "";
    std::function<void()> apply  = nullptr;
    std::function<void()> revert = nullptr;
};

// =============================================================================
// 4. LOGIN & CHECKOUT RESULT STRUCTS
// =============================================================================
struct LoginResult {
    bool        success          = false;
    bool        lockedOut        = false;
    double      lockoutRemaining = 0.0;
    std::string message          = "";
};

struct CheckOutResult {
    bool        success         = false;
    int         invoiceId       = -1;
    bool        loyaltyPromoted = false;
    LoyaltyTier oldTier         = LoyaltyTier::Standard;
    LoyaltyTier newTier         = LoyaltyTier::Standard;
    std::string txtExportPath   = "";
    std::string htmlExportPath  = "";
    std::string message         = "";
};

// =============================================================================
// 5. HOTEL SYSTEM CORE STATE MANAGER (Section 3, 7, 9.3–9.10)
// =============================================================================
class HotelSystem {
public:
    explicit HotelSystem(
        const std::string& dataDir = "data",
        const std::string& backupDir = "backups",
        const std::string& exportDir = "exports"
    );

    // Startup initialization: loads data/, runs crash recovery, rebuilds CNIC hash table, min-heap, waitlist queue
    bool initialize();

    // Saves all modified .dat tables atomically and updates last-saved timestamp
    bool saveAll();

    // Periodic tick (call once per frame or second): updates lockout timers, recomputes heap priorities every 30s,
    // and triggers automatic 5-minute snapshot backup
    void tick(double deltaTimeSeconds);

    // -------------------------------------------------------------------------
    // Authentication & RBAC (Section 7)
    // -------------------------------------------------------------------------
    static bool can(Role role, Action action);
    bool checkPermission(Action action, const std::string& attemptedActionName);

    LoginResult login(const std::string& username, const std::string& password);
    void logout();
    bool isLoggedIn() const { return m_currentUserId != -1; }
    const User* currentUser() const;
    Role currentRole() const;
    double getLoginLockoutRemaining() const { return m_lockoutRemainingSeconds; }
    int getFailedLoginAttempts() const { return m_failedLoginAttempts; }

    // Staff management (Admin only)
    bool createUser(
        const std::string& username,
        const std::string& password,
        Role role,
        const std::string& displayName,
        std::string* outError = nullptr
    );
    bool setUserActive(int userId, bool active, std::string* outError = nullptr);

    // -------------------------------------------------------------------------
    // Guest CRM & Loyalty State Machine (Section 9.4)
    // -------------------------------------------------------------------------
    int registerGuest(const Guest& inputGuest, std::string* outError = nullptr);
    bool updateGuest(const Guest& updatedGuest, std::string* outError = nullptr);
    Guest* findGuestById(int guestId);
    const Guest* findGuestById(int guestId) const;
    int findGuestIdByCnicHash(const std::string& cnic) const;
    const Guest* findGuestByCnicBinarySearch(const std::string& cnic) const;
    bool isDuplicateCnic(const std::string& cnic, int ignoreGuestId = -1) const;

    static LoyaltyTier evaluateLoyaltyTier(LoyaltyTier currentTier, int stayCount, double lifetimeSpend);
    static double loyaltyDiscountRate(LoyaltyTier tier);

    // -------------------------------------------------------------------------
    // Room Operations, Booking Engine & Timeline Actions (Section 9.2, 9.6)
    // -------------------------------------------------------------------------
    Room* findRoomById(int roomId);
    const Room* findRoomById(int roomId) const;
    Room* findRoomByNumber(int roomNumber);
    const Room* findRoomByNumber(int roomNumber) const;
    Booking* findBookingById(int bookingId);
    const Booking* findBookingById(int bookingId) const;
    Invoice* findInvoiceById(int invoiceId);
    const Invoice* findInvoiceById(int invoiceId) const;

    bool setRoomStatus(int roomId, RoomStatus newStatus, bool recordUndo = true, std::string* outError = nullptr);

    int createBooking(
        int guestId,
        int roomId,
        int32_t checkInDay,
        int32_t checkOutDay,
        int adults,
        int children,
        const std::string& promoCode,
        bool immediateCheckIn,
        std::string* outError = nullptr
    );

    bool checkInBooking(int bookingId, std::string* outError = nullptr);
    CheckOutResult checkOutBooking(
        int bookingId,
        int32_t checkoutMinuteOfDay,
        const std::string& paymentMethod,
        std::string* outError = nullptr
    );

    bool cancelBooking(int bookingId, bool isNoShow, std::string* outError = nullptr);
    bool swapBookingRoom(int bookingId, int newRoomId, bool recordUndo = true, std::string* outError = nullptr);
    bool editBookingDates(
        int bookingId,
        int32_t newCheckInDay,
        int32_t newCheckOutDay,
        bool recordUndo = true,
        std::string* outError = nullptr
    );
    bool addChargeToBooking(int bookingId, const AddOn& addOn, bool recordUndo = true, std::string* outError = nullptr);

    // -------------------------------------------------------------------------
    // Waitlist FIFO Queue & Auto-Matching (Section 9.6)
    // -------------------------------------------------------------------------
    int addToWaitlist(
        int guestId,
        RoomType wantedType,
        int32_t fromDay,
        int32_t toDay,
        double maxBudget,
        std::string* outError = nullptr
    );
    bool removeWaitlistEntry(int waitlistId);
    int checkWaitlistMatchForRoom(int roomId);
    const std::queue<WaitlistEntry>& waitlistQueue() const { return m_waitlistQueue; }

    // -------------------------------------------------------------------------
    // Billing, Folio Calculation & TXT/HTML Invoice Export (Section 9.5)
    // -------------------------------------------------------------------------
    static const std::vector<PromoCodeRule>& promoCodeTable();
    FolioBreakdown calculateFolio(
        const Booking& booking,
        const Guest& guest,
        int32_t actualCheckoutDay,
        int32_t actualCheckoutMinute
    ) const;

    bool markInvoicePaid(int invoiceId, const std::string& paymentMethod, std::string* outError = nullptr);
    bool exportInvoiceFiles(
        const Invoice& invoice,
        std::string* outTxtPath = nullptr,
        std::string* outHtmlPath = nullptr
    ) const;

    // -------------------------------------------------------------------------
    // Dynamic Yield Pricing, Housekeeping Heap & Service Orders (Section 9.8, 9.9, 9.10)
    // -------------------------------------------------------------------------
    YieldPricingResult getTodayYieldPricing() const;
    YieldPricingResult getYieldPricingForDay(int32_t dayNumber) const;
    bool setManualSurgeOverride(double multiplier, const std::string& reason, std::string* outError = nullptr);
    bool setSurgeEnabled(bool enabled);
    bool setTaxRate(double newRate);

    int dispatchHousekeepingTask(
        int roomId,
        TaskType type,
        int severity,
        const std::string& assignedTo,
        const std::string& note,
        std::string* outError = nullptr
    );
    bool startHousekeepingTask(int taskId);
    bool completeHousekeepingTask(int taskId);
    bool reassignHousekeepingTask(int taskId, const std::string& newAssignee);
    void recomputeAllTaskPriorities();
    std::vector<HousekeepingTask> getHousekeepingDispatchOrder() const;

    int createServiceOrder(int bookingId, AddOnType type, const std::string& item, int qty, double price);
    bool moveServiceOrderStage(int orderId, int newStage);

    // -------------------------------------------------------------------------
    // Undo / Redo Stacks (Section 9.10 #2)
    // -------------------------------------------------------------------------
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    std::string lastUndoActionName() const { return m_undoStack.empty() ? "" : m_undoStack.top().name; }
    std::string lastRedoActionName() const { return m_redoStack.empty() ? "" : m_redoStack.top().name; }
    bool undo();
    bool redo();
    void clearUndoHistory();

    // -------------------------------------------------------------------------
    // Night Audit, Audit Chain Verification & Backups (Section 5, 6, 9.10 #5)
    // -------------------------------------------------------------------------
    int verifyAuditLogChain() const;
    bool runNightAudit(std::string* outReportPath = nullptr, std::string* outSummary = nullptr);
    bool createBackupNow(std::string* outSnapshotName = nullptr);
    std::vector<std::string> listBackups() const;
    bool restoreBackup(const std::string& snapshotName, std::string* outError = nullptr);
    bool resetCleanState(std::string* outError = nullptr);
    bool seedVivaDemoData(std::string* outError = nullptr);

    // -------------------------------------------------------------------------
    // Notices / Toast Queue & Accessors
    // -------------------------------------------------------------------------
    void pushNotice(NoticeType type, const std::string& title, const std::string& message,
                    int relatedWaitlistId = -1, int relatedRoomId = -1);
    std::vector<SystemNotice> drainNotices();
    const std::vector<SystemNotice>& notificationHistory() const { return m_notificationHistory; }
    int unreadNotificationCount() const { return m_unreadNotifications; }
    void markNotificationsRead() { m_unreadNotifications = 0; }

    HotelDatabase& db() { return m_db; }
    const HotelDatabase& db() const { return m_db; }
    FileManager& fileManager() { return m_fileManager; }
    const FileManager& fileManager() const { return m_fileManager; }
    const GuestCnicHashTable& cnicHashTable() const { return m_cnicHashTable; }
    const TaskMinHeap& taskHeap() const { return m_taskHeap; }
    RecommenderWeights& recommenderWeights() { return m_recommenderWeights; }
    const RecommenderWeights& recommenderWeights() const { return m_recommenderWeights; }
    const std::string& lastSavedTimeStr() const { return m_lastSavedTimeStr; }
    int lastAuditTamperIndex() const { return m_lastAuditTamperIndex; }

    // Phase 3 end-to-end self-test verifying RBAC, CRM/Loyalty, Booking/Conflict, Folio, Exports, Undo/Redo, Waitlist
    static std::vector<SelfTestEntry> runPhase3SelfTests(bool verboseConsole = true);

private:
    void rebuildRuntimeIndexes();
    void syncWaitlistQueueFromDb();
    void logAudit(const std::string& action, const std::string& details);
    void pushUndoCommand(UndoCommand cmd);
    void updateLastSavedClock();
    int countOccupiedOrReservedForDay(int32_t dayNumber) const;

    FileManager               m_fileManager;
    HotelDatabase             m_db{};
    GuestCnicHashTable        m_cnicHashTable{};
    TaskMinHeap               m_taskHeap{};
    std::queue<WaitlistEntry> m_waitlistQueue{};
    RecommenderWeights        m_recommenderWeights{};

    std::stack<UndoCommand>   m_undoStack{};
    std::stack<UndoCommand>   m_redoStack{};

    int                       m_currentUserId           = -1;
    int                       m_failedLoginAttempts     = 0;
    double                    m_lockoutRemainingSeconds = 0.0;
    double                    m_heapRecomputeTimer      = 0.0;
    double                    m_autoBackupTimer         = 0.0;
    int                       m_lastAuditTamperIndex    = -1;
    std::string               m_lastSavedTimeStr        = "00:00:00";

    std::vector<SystemNotice> m_pendingNotices{};
    std::vector<SystemNotice> m_notificationHistory{};
    int                       m_unreadNotifications     = 0;
};

} // namespace hms

#endif // HMS_HOTEL_SYSTEM_H
