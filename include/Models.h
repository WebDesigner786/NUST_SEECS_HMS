#ifndef HMS_MODELS_H
#define HMS_MODELS_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace hms {

// =============================================================================
// 1. CONFIGURATION CONSTANTS (Section 4 & Section 9.4/9.5)
// =============================================================================
namespace Cfg {
    constexpr const char* HOTEL_NAME    = "Grand Meridian Hotel";
    constexpr const char* HOTEL_TAGLINE = "Islamabad";
    constexpr const char* APP_VERSION   = "1.0.0";
    constexpr double TAX_RATE          = 0.16;
    constexpr double SURGE_T1          = 0.70;
    constexpr double SURGE_T2          = 0.85;
    constexpr double SURGE_M1          = 1.10;
    constexpr double SURGE_M2          = 1.25;
    constexpr double SURGE_CAP         = 1.60;
    constexpr int    FLOORS            = 5;
    constexpr int    ROOMS_PER_FLOOR   = 10;
    constexpr int    TOTAL_ROOMS       = FLOORS * ROOMS_PER_FLOOR;

    // Base room rates in PKR (Section 10)
    constexpr double RATE_STANDARD     = 8000.0;
    constexpr double RATE_DELUXE       = 12500.0;
    constexpr double RATE_SUITE        = 22000.0;
    constexpr double RATE_PRESIDENTIAL = 45000.0;

    // Loyalty promotion thresholds (Section 9.4)
    constexpr int    SILVER_STAYS      = 5;
    constexpr double SILVER_SPEND      = 150000.0;
    constexpr int    GOLD_STAYS        = 15;
    constexpr double GOLD_SPEND        = 500000.0;
    constexpr int    PLATINUM_STAYS    = 30;
    constexpr double PLATINUM_SPEND    = 1500000.0;

    // Loyalty discount rates (Section 9.4)
    constexpr double DISCOUNT_STANDARD = 0.00;
    constexpr double DISCOUNT_SILVER   = 0.05;
    constexpr double DISCOUNT_GOLD     = 0.10;
    constexpr double DISCOUNT_PLATINUM = 0.15;
} // namespace Cfg

// =============================================================================
// 2. ENUMERATIONS & toString() HELPERS (Section 4)
// =============================================================================
enum class RoomStatus : int32_t {
    Available = 0,
    Occupied = 1,
    Reserved = 2,
    CleaningRequired = 3,
    Maintenance = 4
};

enum class RoomType : int32_t {
    Standard = 0,
    Deluxe = 1,
    Suite = 2,
    Presidential = 3
};

enum class ViewType : int32_t {
    City = 0,
    Garden = 1,
    Pool = 2
};

enum class BedConfig : int32_t {
    Single = 0,
    Double = 1,
    Twin = 2,
    King = 3
};

enum class Role : int32_t {
    Admin = 0,
    Receptionist = 1,
    Housekeeping = 2
};

enum class LoyaltyTier : int32_t {
    Standard = 0,
    Silver = 1,
    Gold = 2,
    Platinum = 3
};

enum class BookingStatus : int32_t {
    Reserved = 0,
    CheckedIn = 1,
    CheckedOut = 2,
    Cancelled = 3,
    NoShow = 4
};

enum class TaskType : int32_t {
    Cleaning = 0,
    Maintenance = 1
};

enum class TaskStatus : int32_t {
    Queued = 0,
    InProgress = 1,
    Done = 2
};

enum class AddOnType : int32_t {
    Minibar = 0,
    Laundry = 1,
    Dining = 2,
    Spa = 3,
    Transport = 4
};

/*
 * Purpose: Converts RoomStatus enum value to human-readable string.
 * Inputs: s (RoomStatus).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(RoomStatus s) {
    switch (s) {
        case RoomStatus::Available:        return "Available";
        case RoomStatus::Occupied:         return "Occupied";
        case RoomStatus::Reserved:         return "Reserved";
        case RoomStatus::CleaningRequired: return "Cleaning Required";
        case RoomStatus::Maintenance:      return "Maintenance";
    }
    return "Available";
}

/*
 * Purpose: Converts RoomType enum value to human-readable string.
 * Inputs: t (RoomType).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(RoomType t) {
    switch (t) {
        case RoomType::Standard:     return "Standard";
        case RoomType::Deluxe:       return "Deluxe";
        case RoomType::Suite:        return "Suite";
        case RoomType::Presidential: return "Presidential";
    }
    return "Standard";
}

/*
 * Purpose: Converts ViewType enum value to human-readable string.
 * Inputs: v (ViewType).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(ViewType v) {
    switch (v) {
        case ViewType::City:   return "City";
        case ViewType::Garden: return "Garden";
        case ViewType::Pool:   return "Pool";
    }
    return "City";
}

/*
 * Purpose: Converts BedConfig enum value to human-readable string.
 * Inputs: b (BedConfig).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(BedConfig b) {
    switch (b) {
        case BedConfig::Single: return "Single";
        case BedConfig::Double: return "Double";
        case BedConfig::Twin:   return "Twin";
        case BedConfig::King:   return "King";
    }
    return "Single";
}

/*
 * Purpose: Converts Role enum value to human-readable string.
 * Inputs: r (Role).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(Role r) {
    switch (r) {
        case Role::Admin:        return "Admin";
        case Role::Receptionist: return "Receptionist";
        case Role::Housekeeping: return "Housekeeping";
    }
    return "Receptionist";
}

/*
 * Purpose: Converts LoyaltyTier enum value to human-readable string.
 * Inputs: t (LoyaltyTier).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(LoyaltyTier t) {
    switch (t) {
        case LoyaltyTier::Standard: return "Standard";
        case LoyaltyTier::Silver:   return "Silver";
        case LoyaltyTier::Gold:     return "Gold";
        case LoyaltyTier::Platinum: return "Platinum";
    }
    return "Standard";
}

/*
 * Purpose: Converts BookingStatus enum value to human-readable string.
 * Inputs: s (BookingStatus).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(BookingStatus s) {
    switch (s) {
        case BookingStatus::Reserved:   return "Reserved";
        case BookingStatus::CheckedIn:  return "Checked In";
        case BookingStatus::CheckedOut: return "Checked Out";
        case BookingStatus::Cancelled:  return "Cancelled";
        case BookingStatus::NoShow:     return "No-Show";
    }
    return "Reserved";
}

/*
 * Purpose: Converts TaskType enum value to human-readable string.
 * Inputs: t (TaskType).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(TaskType t) {
    switch (t) {
        case TaskType::Cleaning:    return "Cleaning";
        case TaskType::Maintenance: return "Maintenance";
    }
    return "Cleaning";
}

/*
 * Purpose: Converts TaskStatus enum value to human-readable string.
 * Inputs: s (TaskStatus).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(TaskStatus s) {
    switch (s) {
        case TaskStatus::Queued:     return "Queued";
        case TaskStatus::InProgress: return "In Progress";
        case TaskStatus::Done:       return "Done";
    }
    return "Queued";
}

/*
 * Purpose: Converts AddOnType enum value to human-readable string.
 * Inputs: a (AddOnType).
 * Outputs: Constant C-string representation.
 * Big-O: O(1) time, O(1) space.
 */
inline const char* toString(AddOnType a) {
    switch (a) {
        case AddOnType::Minibar:   return "Minibar";
        case AddOnType::Laundry:   return "Laundry";
        case AddOnType::Dining:    return "Dining";
        case AddOnType::Spa:       return "Spa";
        case AddOnType::Transport: return "Transport";
    }
    return "Minibar";
}

// =============================================================================
// 3. DATA STRUCTURES (Section 4 — Exact Members & Default Initializers)
// =============================================================================

struct AddOn {
    AddOnType   type        = AddOnType::Minibar;
    std::string description = "";
    double      unitPrice   = 0.0;
    int         quantity    = 1;
    int         dayAdded    = 0;

    bool operator==(const AddOn& o) const {
        return type == o.type &&
               description == o.description &&
               std::abs(unitPrice - o.unitPrice) < 1e-6 &&
               quantity == o.quantity &&
               dayAdded == o.dayAdded;
    }
};

struct Room {
    int         id                 = 0;
    int         number             = 101;
    int         floor              = 1;
    RoomType    type               = RoomType::Standard;
    ViewType    view               = ViewType::City;
    BedConfig   bed                = BedConfig::Single;
    double      baseRate           = Cfg::RATE_STANDARD;
    int         noiseLevel         = 1; // 1 quiet .. 5 loud
    int         elevatorDistance   = 0; // rooms from elevator, 0..5
    RoomStatus  status             = RoomStatus::Available;
    int         currentBookingId   = -1; // -1 none
    int         lastCheckoutDay    = 0;
    int         lastCheckoutMinute = 720;
    std::string notes              = "";

    bool operator==(const Room& o) const {
        return id == o.id &&
               number == o.number &&
               floor == o.floor &&
               type == o.type &&
               view == o.view &&
               bed == o.bed &&
               std::abs(baseRate - o.baseRate) < 1e-6 &&
               noiseLevel == o.noiseLevel &&
               elevatorDistance == o.elevatorDistance &&
               status == o.status &&
               currentBookingId == o.currentBookingId &&
               lastCheckoutDay == o.lastCheckoutDay &&
               lastCheckoutMinute == o.lastCheckoutMinute &&
               notes == o.notes;
    }
};

struct Guest {
    int         id              = 0;
    std::string fullName        = "";
    std::string cnic            = "";
    std::string passport        = "";
    std::string phone           = "";
    std::string email           = "";
    std::string nationality     = "Pakistan";
    std::string address         = "";
    bool        isForeign       = false;
    LoyaltyTier tier            = LoyaltyTier::Standard;
    double      lifetimeSpend   = 0.0;
    int         stayCount       = 0;
    int         registeredDay   = 0;
    std::string preferencesNote = "";

    bool operator==(const Guest& o) const {
        return id == o.id &&
               fullName == o.fullName &&
               cnic == o.cnic &&
               passport == o.passport &&
               phone == o.phone &&
               email == o.email &&
               nationality == o.nationality &&
               address == o.address &&
               isForeign == o.isForeign &&
               tier == o.tier &&
               std::abs(lifetimeSpend - o.lifetimeSpend) < 1e-6 &&
               stayCount == o.stayCount &&
               registeredDay == o.registeredDay &&
               preferencesNote == o.preferencesNote;
    }
};

struct Booking {
    int                id              = 0;
    int                guestId         = 0;
    int                roomId          = 0;
    int                checkInDay      = 0;
    int                checkOutDay     = 1;
    int                checkInMinute   = 840; // 14:00 default
    int                checkOutMinute  = 720; // 12:00 default
    int                adults          = 1;
    int                children        = 0;
    BookingStatus      status          = BookingStatus::Reserved;
    double             quotedRate      = Cfg::RATE_STANDARD;
    double             surgeMultiplier = 1.0;
    std::string        promoCode       = "";
    std::vector<AddOn> addOns          = {};
    int                invoiceId       = -1; // -1 none
    int                createdDay      = 0;

    bool operator==(const Booking& o) const {
        return id == o.id &&
               guestId == o.guestId &&
               roomId == o.roomId &&
               checkInDay == o.checkInDay &&
               checkOutDay == o.checkOutDay &&
               checkInMinute == o.checkInMinute &&
               checkOutMinute == o.checkOutMinute &&
               adults == o.adults &&
               children == o.children &&
               status == o.status &&
               std::abs(quotedRate - o.quotedRate) < 1e-6 &&
               std::abs(surgeMultiplier - o.surgeMultiplier) < 1e-6 &&
               promoCode == o.promoCode &&
               addOns == o.addOns &&
               invoiceId == o.invoiceId &&
               createdDay == o.createdDay;
    }
};

struct Invoice {
    int         id              = 0;
    std::string number          = "INV-1000";
    int         bookingId       = 0;
    int         guestId         = 0;
    int         issuedDay       = 0;
    int         nights          = 1;
    double      roomSubtotal    = 0.0;
    double      addOnSubtotal   = 0.0;
    double      loyaltyDiscount = 0.0;
    double      promoDiscount   = 0.0;
    double      taxableAmount   = 0.0;
    double      taxRate         = Cfg::TAX_RATE;
    double      taxAmount       = 0.0;
    double      grandTotal      = 0.0;
    bool        paid            = false;
    std::string paymentMethod   = "Cash";

    bool operator==(const Invoice& o) const {
        return id == o.id &&
               number == o.number &&
               bookingId == o.bookingId &&
               guestId == o.guestId &&
               issuedDay == o.issuedDay &&
               nights == o.nights &&
               std::abs(roomSubtotal - o.roomSubtotal) < 1e-6 &&
               std::abs(addOnSubtotal - o.addOnSubtotal) < 1e-6 &&
               std::abs(loyaltyDiscount - o.loyaltyDiscount) < 1e-6 &&
               std::abs(promoDiscount - o.promoDiscount) < 1e-6 &&
               std::abs(taxableAmount - o.taxableAmount) < 1e-6 &&
               std::abs(taxRate - o.taxRate) < 1e-6 &&
               std::abs(taxAmount - o.taxAmount) < 1e-6 &&
               std::abs(grandTotal - o.grandTotal) < 1e-6 &&
               paid == o.paid &&
               paymentMethod == o.paymentMethod;
    }
};

struct User {
    int         id           = 0;
    std::string username     = "";
    std::string saltHex      = "";
    std::string passHashHex  = "";
    Role        role         = Role::Receptionist;
    std::string displayName  = "";
    bool        active       = true;
    int         lastLoginDay = 0;

    bool operator==(const User& o) const {
        return id == o.id &&
               username == o.username &&
               saltHex == o.saltHex &&
               passHashHex == o.passHashHex &&
               role == o.role &&
               displayName == o.displayName &&
               active == o.active &&
               lastLoginDay == o.lastLoginDay;
    }
};

struct HousekeepingTask {
    int         id            = 0;
    int         roomId        = 0;
    TaskType    type          = TaskType::Cleaning;
    TaskStatus  status        = TaskStatus::Queued;
    int         severity      = 1; // 1..5, maintenance only
    int         createdDay    = 0;
    int         createdMinute = 0;
    double      priorityScore = 0.0;
    std::string assignedTo    = "";
    std::string note          = "";

    bool operator==(const HousekeepingTask& o) const {
        return id == o.id &&
               roomId == o.roomId &&
               type == o.type &&
               status == o.status &&
               severity == o.severity &&
               createdDay == o.createdDay &&
               createdMinute == o.createdMinute &&
               std::abs(priorityScore - o.priorityScore) < 1e-6 &&
               assignedTo == o.assignedTo &&
               note == o.note;
    }
};

struct AuditEntry {
    int         index       = 0;
    long long   timestamp   = 0;
    int         userId      = 0;
    std::string username    = "system";
    std::string action      = "";
    std::string details     = "";
    std::string prevHashHex = "";
    std::string hashHex     = "";

    bool operator==(const AuditEntry& o) const {
        return index == o.index &&
               timestamp == o.timestamp &&
               userId == o.userId &&
               username == o.username &&
               action == o.action &&
               details == o.details &&
               prevHashHex == o.prevHashHex &&
               hashHex == o.hashHex;
    }
};

struct WaitlistEntry {
    int      id         = 0;
    int      guestId    = 0;
    RoomType wantedType = RoomType::Standard;
    int      fromDay    = 0;
    int      toDay      = 1;
    double   maxBudget  = Cfg::RATE_STANDARD;
    int      createdDay = 0;

    bool operator==(const WaitlistEntry& o) const {
        return id == o.id &&
               guestId == o.guestId &&
               wantedType == o.wantedType &&
               fromDay == o.fromDay &&
               toDay == o.toDay &&
               std::abs(maxBudget - o.maxBudget) < 1e-6 &&
               createdDay == o.createdDay;
    }
};

struct GuestPreferences {
    double    budget         = 15000.0;
    bool      wantsLowFloor  = false;
    bool      noFloorPref    = true;
    ViewType  view           = ViewType::City;
    bool      anyView        = true;
    int       elevatorPref   = 1; // 0 near, 1 any, 2 far
    int       noiseTolerance = 3; // 1..5
    BedConfig bed            = BedConfig::Double;
    bool      anyBed         = true;
};

struct RoomMatch {
    int         roomId     = 0;
    double      score01    = 0.0;
    int         percent    = 0;
    std::string reasonText = "";
};

struct DailyStat {
    int    day          = 0;
    double revenue      = 0.0;
    double occupancyPct = 0.0;
    int    checkins     = 0;
    int    checkouts    = 0;

    bool operator==(const DailyStat& o) const {
        return day == o.day &&
               std::abs(revenue - o.revenue) < 1e-6 &&
               std::abs(occupancyPct - o.occupancyPct) < 1e-6 &&
               checkins == o.checkins &&
               checkouts == o.checkouts;
    }
};

struct ServiceOrder {
    int         id        = 0;
    int         bookingId = 0;
    AddOnType   type      = AddOnType::Dining;
    std::string item      = "";
    int         qty       = 1;
    double      price     = 0.0;
    int         stage     = 0; // 0 Ordered, 1 Preparing, 2 Delivered

    bool operator==(const ServiceOrder& o) const {
        return id == o.id &&
               bookingId == o.bookingId &&
               type == o.type &&
               item == o.item &&
               qty == o.qty &&
               std::abs(price - o.price) < 1e-6 &&
               stage == o.stage;
    }
};

// Persistent system settings stored in data/settings.dat (Section 5)
struct AppSettings {
    int    themeChoice         = 0; // 0 = MidnightObsidian, 1 = ExecutivePearl
    double taxRate             = Cfg::TAX_RATE;
    bool   surgeEnabled        = true;
    double manualSurgeOverride = 0.0; // 0.0 = automatic surge calculation
    int    nextGuestId         = 1;
    int    nextBookingId       = 1;
    int    nextInvoiceId       = 1001;
    int    nextUserId          = 4;
    int    nextTaskId          = 1;
    int    nextWaitlistId      = 1;
    int    nextOrderId         = 1;

    // Interface preferences and first-run state (appended to settings.dat; older files simply keep defaults)
    int         textScaleIdx   = 1;     // 0 = 90%, 1 = 100%, 2 = 115%, 3 = 130%
    bool        welcomeShown   = false; // first-launch "sample data or empty system" dialog already answered
    std::string teamName1      = "";
    std::string teamName2      = "";
    std::string teamName3      = "";

    bool operator==(const AppSettings& o) const {
        return themeChoice == o.themeChoice &&
               std::abs(taxRate - o.taxRate) < 1e-6 &&
               surgeEnabled == o.surgeEnabled &&
               std::abs(manualSurgeOverride - o.manualSurgeOverride) < 1e-6 &&
               nextGuestId == o.nextGuestId &&
               nextBookingId == o.nextBookingId &&
               nextInvoiceId == o.nextInvoiceId &&
               nextUserId == o.nextUserId &&
               nextTaskId == o.nextTaskId &&
               nextWaitlistId == o.nextWaitlistId &&
               nextOrderId == o.nextOrderId &&
               textScaleIdx == o.textScaleIdx &&
               welcomeShown == o.welcomeShown &&
               teamName1 == o.teamName1 &&
               teamName2 == o.teamName2 &&
               teamName3 == o.teamName3;
    }
};

// Self-Test single check result used by CLI --selftest and Viva Tools "Self-Test" tab (Section 11)
struct SelfTestEntry {
    std::string name    = "";
    bool        passed  = false;
    std::string details = "";
};

// =============================================================================
// 4. CIVIL DATE & CURRENCY UTILITIES (Section 1 Law 5)
// =============================================================================
namespace DateUtil {

    // Days from 1970-01-01 to 2000-01-01 in proleptic Gregorian calendar = 10957
    constexpr int32_t kUnixEpochTo2000Days = 10957;

    /*
     * Purpose: Determines whether a given Gregorian year is a leap year.
     * Inputs: year (e.g. 2026).
     * Outputs: true if leap year, false otherwise.
     * Big-O: O(1) time, O(1) space.
     */
    inline bool isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    /*
     * Purpose: Returns the number of days in a given month (1..12) of a given year.
     * Inputs: year, month (1 = Jan .. 12 = Dec).
     * Outputs: Integer days in month (28..31).
     * Big-O: O(1) time, O(1) space.
     */
    inline int daysInMonth(int year, int month) {
        static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12) {
            return 30;
        }
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }
        return kDays[month - 1];
    }

    /*
     * Purpose: Converts a civil Gregorian date (year, month, day) to dayNumber (days since 2000-01-01).
     *          Uses Howard Hinnant's timezone-free civil calendar algorithm.
     * Inputs: y (year), m (month 1..12), d (day 1..31).
     * Outputs: int32_t days since 2000-01-01 (2000-01-01 -> 0).
     * Big-O: O(1) time, O(1) space.
     */
    inline int32_t toDayNumber(int y, int m, int d) {
        y -= (m <= 2) ? 1 : 0;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153U * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2U) / 5U +
                             static_cast<unsigned>(d) - 1U;
        const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
        const int32_t daysSince1970 = era * 146097 + static_cast<int32_t>(doe) - 719468;
        return daysSince1970 - kUnixEpochTo2000Days;
    }

    /*
     * Purpose: Converts a dayNumber (days since 2000-01-01) back into civil (year, month, day).
     * Inputs: dayNumber (days since 2000-01-01), out references y, m, d.
     * Outputs: Populates y, m (1..12), d (1..31).
     * Big-O: O(1) time, O(1) space.
     */
    inline void fromDayNumber(int32_t dayNumber, int& y, int& m, int& d) {
        const int32_t z = dayNumber + kUnixEpochTo2000Days + 719468;
        const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
        y = static_cast<int>(yoe) + era * 400;
        const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
        const unsigned mp = (5U * doy + 2U) / 153U;
        d = static_cast<int>(doy - (153U * mp + 2U) / 5U + 1U);
        m = static_cast<int>(mp) + (mp < 10U ? 3 : -9);
        y += (m <= 2) ? 1 : 0;
    }

    /*
     * Purpose: Formats a dayNumber as an ISO-8601 date string "YYYY-MM-DD".
     * Inputs: dayNumber (days since 2000-01-01).
     * Outputs: Formatted std::string "YYYY-MM-DD".
     * Big-O: O(1) time, O(1) space.
     */
    inline std::string formatDate(int32_t dayNumber) {
        int y = 2000, m = 1, d = 1;
        fromDayNumber(dayNumber, y, m, d);
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
        return std::string(buf);
    }

    /*
     * Purpose: Formats a dayNumber for display in the UI as "21 Nov 2026" (files keep ISO-8601).
     * Inputs: dayNumber (days since 2000-01-01).
     * Outputs: Formatted std::string "D Mon YYYY".
     * Big-O: O(1) time, O(1) space.
     */
    inline std::string formatDateUI(int32_t dayNumber) {
        static const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int y = 2000, m = 1, d = 1;
        fromDayNumber(dayNumber, y, m, d);
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%d %s %04d", d, kMonths[(m >= 1 && m <= 12) ? m - 1 : 0], y);
        return std::string(buf);
    }

    /*
     * Purpose: Parses a "YYYY-MM-DD" string into a dayNumber, validating calendar bounds.
     * Inputs: dateStr ("YYYY-MM-DD"), outDayNumber (output dayNumber).
     * Outputs: true if valid date and parsed cleanly, false otherwise.
     * Big-O: O(1) time, O(1) space.
     */
    inline bool parseDate(const std::string& dateStr, int32_t& outDayNumber) {
        if (dateStr.size() != 10 || dateStr[4] != '-' || dateStr[7] != '-') {
            return false;
        }
        for (size_t i = 0; i < 10; ++i) {
            if (i == 4 || i == 7) {
                continue;
            }
            if (dateStr[i] < '0' || dateStr[i] > '9') {
                return false;
            }
        }
        const int y = std::stoi(dateStr.substr(0, 4));
        const int m = std::stoi(dateStr.substr(5, 2));
        const int d = std::stoi(dateStr.substr(8, 2));
        if (y < 1970 || y > 2100 || m < 1 || m > 12) {
            return false;
        }
        if (d < 1 || d > daysInMonth(y, m)) {
            return false;
        }
        outDayNumber = toDayNumber(y, m, d);
        return true;
    }

    /*
     * Purpose: Formats minuteOfDay (0..1439) into "HH:MM" 24-hour string.
     * Inputs: minuteOfDay (minutes since 00:00).
     * Outputs: std::string "HH:MM".
     * Big-O: O(1) time, O(1) space.
     */
    inline std::string formatTime(int32_t minuteOfDay) {
        if (minuteOfDay < 0) {
            minuteOfDay = 0;
        }
        const int hh = (minuteOfDay / 60) % 24;
        const int mm = minuteOfDay % 60;
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
        return std::string(buf);
    }

    /*
     * Purpose: Returns day of week for dayNumber: 0 = Monday .. 4 = Friday, 5 = Saturday, 6 = Sunday.
     *          (2000-01-01 was a Saturday -> index 5).
     * Inputs: dayNumber (days since 2000-01-01).
     * Outputs: Weekday integer in [0..6].
     * Big-O: O(1) time, O(1) space.
     */
    inline int dayOfWeek(int32_t dayNumber) {
        int32_t r = (dayNumber + 5) % 7;
        if (r < 0) {
            r += 7;
        }
        return static_cast<int>(r);
    }

    /*
     * Purpose: Checks whether a given dayNumber falls on a Friday (4) or Saturday (5) night for weekend surge.
     * Inputs: dayNumber (days since 2000-01-01).
     * Outputs: true if Friday or Saturday, false otherwise.
     * Big-O: O(1) time, O(1) space.
     */
    inline bool isWeekendNight(int32_t dayNumber) {
        const int dow = dayOfWeek(dayNumber);
        return (dow == 4 || dow == 5);
    }

    /*
     * Purpose: Checks whether a given dayNumber falls in peak season (Dec-Jan or Jun-Aug) per Section 9.8.
     * Inputs: dayNumber (days since 2000-01-01).
     * Outputs: true if month is 12, 1, 6, 7, or 8.
     * Big-O: O(1) time, O(1) space.
     */
    inline bool isPeakSeason(int32_t dayNumber) {
        int y = 2000, m = 1, d = 1;
        fromDayNumber(dayNumber, y, m, d);
        return (m == 12 || m == 1 || m == 6 || m == 7 || m == 8);
    }

    /*
     * Purpose: Returns the system's current local dayNumber (days since 2000-01-01).
     * Inputs: None.
     * Outputs: Current local dayNumber.
     * Big-O: O(1) time, O(1) space.
     */
    inline int32_t todayDayNumber() {
        const std::time_t now = std::time(nullptr);
        const std::tm* localTm = std::localtime(&now);
        if (localTm == nullptr) {
            return toDayNumber(2026, 11, 18);
        }
        return toDayNumber(localTm->tm_year + 1900, localTm->tm_mon + 1, localTm->tm_mday);
    }

    /*
     * Purpose: Returns the system's current minuteOfDay (0..1439).
     * Inputs: None.
     * Outputs: Current local minuteOfDay.
     * Big-O: O(1) time, O(1) space.
     */
    inline int32_t currentMinuteOfDay() {
        const std::time_t now = std::time(nullptr);
        const std::tm* localTm = std::localtime(&now);
        if (localTm == nullptr) {
            return 720;
        }
        return localTm->tm_hour * 60 + localTm->tm_min;
    }

} // namespace DateUtil

/*
 * Purpose: Formats a numeric amount in PKR with comma thousands separators ("PKR 12,500") per Section 1 Law 5.
 * Inputs: amount (double value in PKR).
 * Outputs: Formatted std::string with "PKR " prefix and comma separators.
 * Big-O: O(D) where D is the number of decimal digits (<= 20), i.e., O(1).
 */
inline std::string formatPKR(double amount) {
    const long long rounded = static_cast<long long>(std::llround(amount));
    const bool negative = (rounded < 0);
    const unsigned long long absVal = static_cast<unsigned long long>(negative ? -rounded : rounded);

    std::string digits = std::to_string(absVal);
    std::string formatted;
    formatted.reserve(digits.size() + digits.size() / 3 + 8);

    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        if (count > 0 && count % 3 == 0) {
            formatted.push_back(',');
        }
        formatted.push_back(digits[static_cast<size_t>(i)]);
        ++count;
    }

    std::string result = negative ? "PKR -" : "PKR ";
    for (int i = static_cast<int>(formatted.size()) - 1; i >= 0; --i) {
        result.push_back(formatted[static_cast<size_t>(i)]);
    }
    return result;
}

} // namespace hms

#endif // HMS_MODELS_H
