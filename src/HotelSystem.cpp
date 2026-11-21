#include "HotelSystem.h"
#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hms {

namespace {

/*
 * Purpose: Converts an ASCII string to uppercase for case-insensitive promo code matching.
 * Inputs: text string.
 * Outputs: Uppercase copy of text.
 * Big-O: O(N) time and space where N = text.size().
 */
std::string toUpperAscii(const std::string& text) {
    std::string out = text;
    for (char& ch : out) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return out;
}

/*
 * Purpose: Escapes HTML special characters (&, <, >, ", ') to keep generated HTML invoices well-formed.
 * Inputs: raw string.
 * Outputs: HTML-escaped string.
 * Big-O: O(N) time and space.
 */
std::string htmlEscape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 16U);
    for (char ch : raw) {
        switch (ch) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out.push_back(ch); break;
        }
    }
    return out;
}

/*
 * Purpose: Formats an 80-column plain ASCII receipt line with left text and right-aligned value.
 * Inputs: leftText, rightText.
 * Outputs: 80-character line bounded by "| " and " |".
 * Big-O: O(1) time (fixed 80 chars), O(1) space.
 */
std::string formatReceiptRow80(const std::string& leftText, const std::string& rightText) {
    constexpr std::size_t kInnerWidth = 76U; // "| " (2) + 76 + " |" (2) = 80
    std::string left = leftText;
    std::string right = rightText;
    if (left.size() + right.size() + 1U > kInnerWidth) {
        const std::size_t maxLeft = (kInnerWidth > right.size() + 2U) ? (kInnerWidth - right.size() - 2U) : 0U;
        left = left.substr(0U, maxLeft);
    }
    const std::size_t pad = kInnerWidth - left.size() - right.size();
    return "| " + left + std::string(pad, ' ') + right + " |";
}

/*
 * Purpose: Centers text inside an 80-column plain ASCII receipt box row.
 * Inputs: text string.
 * Outputs: 80-character centered line.
 * Big-O: O(1) time, O(1) space.
 */
std::string formatReceiptCenter80(const std::string& text) {
    constexpr std::size_t kInnerWidth = 76U;
    const std::string trimmed = text.substr(0U, std::min(text.size(), kInnerWidth));
    const std::size_t leftPad = (kInnerWidth - trimmed.size()) / 2U;
    const std::size_t rightPad = kInnerWidth - trimmed.size() - leftPad;
    return "| " + std::string(leftPad, ' ') + trimmed + std::string(rightPad, ' ') + " |";
}

} // namespace

// =============================================================================
// 1. LIFECYCLE, INDEXING & PERIODIC TICK
// =============================================================================
/*
 * Purpose: Constructs HotelSystem bound to the given data/, backups/, and exports/ directories.
 * Inputs: dataDir, backupDir, exportDir paths.
 * Outputs: Initialized HotelSystem instance.
 * Big-O: O(1) time, O(1) space.
 */
HotelSystem::HotelSystem(const std::string& dataDir, const std::string& backupDir, const std::string& exportDir)
    : m_fileManager(dataDir, backupDir, exportDir) {
    updateLastSavedClock();
}

/*
 * Purpose: Loads persisted database files, runs crash recovery, verifies the audit chain,
 *          and rebuilds runtime indexes (CNIC hash table, housekeeping min-heap, waitlist FIFO queue).
 * Inputs: None.
 * Outputs: true if initialization succeeded.
 * Big-O: O(R + G + T log T + W) where R=rooms, G=guests, T=tasks, W=waitlist entries.
 */
bool HotelSystem::initialize() {
    std::vector<std::string> alerts;
    const bool loaded = m_fileManager.loadAll(m_db, alerts);
    for (const std::string& alert : alerts) {
        pushNotice(NoticeType::Warning, "Storage Recovery", alert);
    }
    rebuildRuntimeIndexes();
    m_lastAuditTamperIndex = verifyAuditLogChain();
    updateLastSavedClock();
    return loaded;
}

/*
 * Purpose: Rebuilds the CNIC hash table, housekeeping TaskMinHeap, and waitlist FIFO std::queue from m_db.
 * Inputs: None.
 * Outputs: Populates runtime auxiliary structures in-place.
 * Big-O: O(G + T log T + W) time, O(G + T + W) space.
 */
void HotelSystem::rebuildRuntimeIndexes() {
    m_cnicHashTable.rebuild(m_db.guests);
    syncWaitlistQueueFromDb();
    recomputeAllTaskPriorities();
}

/*
 * Purpose: Synchronizes the FIFO std::queue<WaitlistEntry> with m_db.waitlist ordered by createdDay and id.
 * Inputs: None.
 * Outputs: Replaces m_waitlistQueue contents.
 * Big-O: O(W log W) time, O(W) space.
 */
void HotelSystem::syncWaitlistQueueFromDb() {
    std::vector<WaitlistEntry> ordered = m_db.waitlist;
    mergeSort(
        ordered,
        [](const WaitlistEntry& a, const WaitlistEntry& b) {
            if (a.createdDay != b.createdDay) {
                return a.createdDay < b.createdDay;
            }
            return a.id < b.id;
        },
        nullptr,
        nullptr,
        "MergeSort (Waitlist FIFO)"
    );

    std::queue<WaitlistEntry> freshQueue;
    for (const WaitlistEntry& entry : ordered) {
        freshQueue.push(entry);
    }
    m_waitlistQueue = std::move(freshQueue);
}

/*
 * Purpose: Atomically saves all 11 .dat files to disk and refreshes the status-bar save timestamp.
 * Inputs: None.
 * Outputs: true if all tables saved cleanly.
 * Big-O: O(Total Records) time and space.
 */
bool HotelSystem::saveAll() {
    const bool ok = m_fileManager.saveAll(m_db);
    if (ok) {
        updateLastSavedClock();
    }
    return ok;
}

/*
 * Purpose: Formats the current local wall-clock time as HH:MM:SS for the status bar.
 * Inputs: None.
 * Outputs: Updates m_lastSavedTimeStr.
 * Big-O: O(1) time, O(1) space.
 */
void HotelSystem::updateLastSavedClock() {
    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    m_lastSavedTimeStr = buf;
}

/*
 * Purpose: Advances periodic system timers: login lockout countdown, 30-second housekeeping priority
 *          recomputation (Section 9.9), and 5-minute automatic snapshot backups (Section 5).
 * Inputs: deltaTimeSeconds (elapsed seconds since last frame).
 * Outputs: Updates timers and triggers periodic maintenance when due.
 * Big-O: O(1) per normal frame; O(T log T) every 30s; O(B) every 300s.
 */
void HotelSystem::tick(double deltaTimeSeconds) {
    if (deltaTimeSeconds <= 0.0) {
        return;
    }
    if (m_lockoutRemainingSeconds > 0.0) {
        m_lockoutRemainingSeconds = std::max(0.0, m_lockoutRemainingSeconds - deltaTimeSeconds);
        if (m_lockoutRemainingSeconds <= 0.0) {
            m_failedLoginAttempts = 0;
        }
    }

    m_heapRecomputeTimer += deltaTimeSeconds;
    if (m_heapRecomputeTimer >= 30.0) {
        m_heapRecomputeTimer = 0.0;
        recomputeAllTaskPriorities();
    }

    m_autoBackupTimer += deltaTimeSeconds;
    if (m_autoBackupTimer >= 300.0) {
        m_autoBackupTimer = 0.0;
        if (isLoggedIn()) {
            std::string snapName;
            if (m_fileManager.createBackupSnapshot(&snapName)) {
                pushNotice(NoticeType::Info, "Auto Backup", "Snapshot saved: " + snapName);
            }
        }
    }
}

// =============================================================================
// 2. AUTHENTICATION, RBAC & STAFF MANAGEMENT (Section 7)
// =============================================================================
/*
 * Purpose: Server-side Role-Based Access Control matrix checking whether Role may execute Action (Section 7).
 * Inputs: role (Admin, Receptionist, Housekeeping), action (Action enum).
 * Outputs: true if permitted, false if denied.
 * Big-O: O(1) time, O(1) space.
 */
bool HotelSystem::can(Role role, Action action) {
    if (role == Role::Admin) {
        return true; // Admin has full access to all screens and actions
    }
    if (role == Role::Receptionist) {
        switch (action) {
            case Action::ViewDashboard:
            case Action::ViewRoomMatrix:
            case Action::UpdateRoomStatus:
            case Action::SmartCheckIn:
            case Action::CreateBooking:
            case Action::EditBooking:
            case Action::CancelBooking:
            case Action::CheckInGuest:
            case Action::CheckOutGuest:
            case Action::RoomSwap:
            case Action::AddServiceCharge:
            case Action::ManageGuests:
            case Action::ManageBilling:
            case Action::ExportInvoice:
            case Action::ManageWaitlist:
            case Action::ManageHousekeeping:
            case Action::ManageServiceOrders:
                return true;
            default:
                return false;
        }
    }
    // Role::Housekeeping
    switch (action) {
        case Action::ViewDashboard:
        case Action::ViewRoomMatrix:
        case Action::UpdateRoomStatus:
        case Action::ManageHousekeeping:
        case Action::ManageServiceOrders:
            return true;
        default:
            return false;
    }
}

/*
 * Purpose: Verifies that the currently logged-in user has permission for action.
 *          On denial, pushes an Error toast and writes a RBAC_DENIED audit log entry (Section 7).
 * Inputs: action, attemptedActionName (human-readable action label).
 * Outputs: true if permitted, false if denied.
 * Big-O: O(1) time, O(1) space.
 */
bool HotelSystem::checkPermission(Action action, const std::string& attemptedActionName) {
    const User* u = currentUser();
    if (u == nullptr) {
        pushNotice(NoticeType::Error, "Authentication Required", "Please sign in to perform: " + attemptedActionName);
        return false;
    }
    if (!can(u->role, action)) {
        const std::string detail = std::string(toString(u->role)) + " denied action: " + attemptedActionName;
        pushNotice(NoticeType::Error, "Access Denied", detail);
        logAudit("RBAC_DENIED", detail);
        m_fileManager.saveAuditLog(m_db.auditLog);
        return false;
    }
    return true;
}

/*
 * Purpose: Authenticates a user against m_db.users with 1,000-round salted SHA-256 verification.
 *          Enforces 3 failed attempts -> 30 second lockout (Section 7), verifies the audit chain on login,
 *          and logs both LOGIN and LOGIN_FAILED events.
 * Inputs: username, password.
 * Outputs: LoginResult containing success flag, lockout state, and user-facing message.
 * Big-O: O(U) where U is number of staff accounts + O(AuditLog) chain verification on success.
 */
LoginResult HotelSystem::login(const std::string& username, const std::string& password) {
    LoginResult res{};
    if (m_lockoutRemainingSeconds > 0.0) {
        res.lockedOut = true;
        res.lockoutRemaining = m_lockoutRemainingSeconds;
        res.message = "Account locked due to 3 failed attempts. Wait " +
                      std::to_string(static_cast<int>(std::ceil(m_lockoutRemainingSeconds))) + "s.";
        return res;
    }

    for (User& u : m_db.users) {
        if (u.username == username) {
            if (!u.active) {
                FileManager::appendAuditEntry(m_db.auditLog, u.id, u.username, "LOGIN_FAILED", "Disabled account login attempt");
                m_fileManager.saveAuditLog(m_db.auditLog);
                res.message = "This account has been disabled by an administrator.";
                return res;
            }
            if (verifyPassword(password, u.saltHex, u.passHashHex)) {
                m_currentUserId = u.id;
                m_failedLoginAttempts = 0;
                m_lockoutRemainingSeconds = 0.0;
                u.lastLoginDay = DateUtil::todayDayNumber();
                clearUndoHistory();

                logAudit("LOGIN", "User signed in (" + std::string(toString(u.role)) + ")");
                m_lastAuditTamperIndex = verifyAuditLogChain();
                m_fileManager.saveUsers(m_db.users);
                m_fileManager.saveAuditLog(m_db.auditLog);

                res.success = true;
                res.message = "Welcome back, " + u.displayName;
                pushNotice(NoticeType::Success, "Signed In", "Logged in as " + u.displayName + " (" + toString(u.role) + ")");
                return res;
            }
            break;
        }
    }

    ++m_failedLoginAttempts;
    FileManager::appendAuditEntry(
        m_db.auditLog,
        0,
        username.empty() ? "unknown" : username,
        "LOGIN_FAILED",
        "Invalid credentials (attempt " + std::to_string(m_failedLoginAttempts) + "/3)"
    );
    m_fileManager.saveAuditLog(m_db.auditLog);

    if (m_failedLoginAttempts >= 3) {
        m_lockoutRemainingSeconds = 30.0;
        res.lockedOut = true;
        res.lockoutRemaining = 30.0;
        res.message = "3 failed attempts. Locked out for 30 seconds.";
    } else {
        res.message = "Invalid username or password (" + std::to_string(3 - m_failedLoginAttempts) + " attempts left).";
    }
    return res;
}

/*
 * Purpose: Logs out the current user, writes a LOGOUT audit entry, clears session Undo/Redo stacks,
 *          saves all tables, and creates a snapshot backup (Section 5).
 * Inputs: None.
 * Outputs: Clears active session state.
 * Big-O: O(Total Records) for save and backup snapshot.
 */
void HotelSystem::logout() {
    if (m_currentUserId == -1) {
        return;
    }
    logAudit("LOGOUT", "User signed out cleanly");
    saveAll();
    m_fileManager.createBackupSnapshot();
    clearUndoHistory();
    m_currentUserId = -1;
}

/*
 * Purpose: Returns a pointer to the currently signed-in User, or nullptr if signed out.
 * Inputs: None.
 * Outputs: Const pointer to User or nullptr.
 * Big-O: O(U) where U is number of staff users.
 */
const User* HotelSystem::currentUser() const {
    if (m_currentUserId == -1) {
        return nullptr;
    }
    for (const User& u : m_db.users) {
        if (u.id == m_currentUserId) {
            return &u;
        }
    }
    return nullptr;
}

/*
 * Purpose: Returns the active user's Role (defaults to Housekeeping if signed out for least privilege).
 * Inputs: None.
 * Outputs: Role enum.
 * Big-O: O(U) time, O(1) space.
 */
Role HotelSystem::currentRole() const {
    const User* u = currentUser();
    return (u != nullptr) ? u->role : Role::Housekeeping;
}

/*
 * Purpose: Creates a new staff user account (Admin only), hashing the password with a fresh 16-byte salt.
 * Inputs: username, password, role, displayName, optional outError.
 * Outputs: true on success, false on validation or RBAC error.
 * Big-O: O(U) time, O(1) space.
 */
bool HotelSystem::createUser(
    const std::string& username,
    const std::string& password,
    Role role,
    const std::string& displayName,
    std::string* outError
) {
    if (!checkPermission(Action::ManageStaff, "Create Staff Account")) {
        if (outError != nullptr) {
            *outError = "Permission denied";
        }
        return false;
    }
    if (username.size() < 3U || password.size() < 6U || displayName.size() < 2U) {
        if (outError != nullptr) {
            *outError = "Username (>=3 chars), password (>=6 chars), and display name (>=2 chars) are required.";
        }
        return false;
    }
    for (const User& existing : m_db.users) {
        if (existing.username == username) {
            if (outError != nullptr) {
                *outError = "Username '" + username + "' already exists.";
            }
            return false;
        }
    }

    User u{};
    u.id           = m_db.settings.nextUserId++;
    u.username     = username;
    u.saltHex      = generateSaltHex();
    u.passHashHex  = hashPassword(u.saltHex, password);
    u.role         = role;
    u.displayName  = displayName;
    u.active       = true;
    u.lastLoginDay = 0;

    m_db.users.push_back(u);
    logAudit("USER_CREATE", "Created staff user '" + username + "' as " + toString(role));
    m_fileManager.saveUsers(m_db.users);
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Success, "Staff Created", "Account '" + username + "' added.");
    return true;
}

/*
 * Purpose: Enables or disables an existing staff user account (Admin only; prevents self-disable).
 * Inputs: userId, active, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(U) time, O(1) space.
 */
bool HotelSystem::setUserActive(int userId, bool active, std::string* outError) {
    if (!checkPermission(Action::ManageStaff, "Toggle Staff Account Status")) {
        if (outError != nullptr) {
            *outError = "Permission denied";
        }
        return false;
    }
    if (userId == m_currentUserId && !active) {
        if (outError != nullptr) {
            *outError = "You cannot disable your own currently signed-in account.";
        }
        return false;
    }
    for (User& u : m_db.users) {
        if (u.id == userId) {
            u.active = active;
            logAudit("USER_STATUS", std::string(active ? "Enabled" : "Disabled") + " user '" + u.username + "'");
            m_fileManager.saveUsers(m_db.users);
            m_fileManager.saveAuditLog(m_db.auditLog);
            pushNotice(NoticeType::Info, "Staff Updated", u.username + (active ? " enabled." : " disabled."));
            return true;
        }
    }
    if (outError != nullptr) {
        *outError = "User ID not found.";
    }
    return false;
}

// =============================================================================
// 3. GUEST CRM & LOYALTY STATE MACHINE (Section 9.4)
// =============================================================================
/*
 * Purpose: Registers a new guest after running hand-written validators (name, CNIC/passport, phone, email)
 *          and duplicate-CNIC lookup via the custom GuestCnicHashTable.
 * Inputs: inputGuest, optional outError.
 * Outputs: Newly assigned guestId (>0), or -1 on error.
 * Big-O: O(1) average time (hash table lookup/insert), O(1) auxiliary space.
 */
int HotelSystem::registerGuest(const Guest& inputGuest, std::string* outError) {
    if (!checkPermission(Action::ManageGuests, "Register Guest")) {
        if (outError != nullptr) {
            *outError = "Permission denied";
        }
        return -1;
    }

    std::string err;
    if (!validatePersonName(inputGuest.fullName, &err)) {
        if (outError != nullptr) { *outError = err; }
        return -1;
    }

    Guest g = inputGuest;
    if (!g.isForeign) {
        if (!validateCnic(g.cnic, &err)) {
            if (outError != nullptr) { *outError = err; }
            return -1;
        }
        if (isDuplicateCnic(g.cnic)) {
            if (outError != nullptr) { *outError = "A guest with CNIC " + g.cnic + " is already registered."; }
            return -1;
        }
        std::string normalizedPhone;
        if (!validatePakistaniPhone(g.phone, &normalizedPhone, &err)) {
            if (outError != nullptr) { *outError = err; }
            return -1;
        }
        g.phone = normalizedPhone;
    } else {
        if (!validatePassport(g.passport, &err)) {
            if (outError != nullptr) { *outError = err; }
            return -1;
        }
    }

    if (!g.email.empty() && !validateEmail(g.email, &err)) {
        if (outError != nullptr) { *outError = err; }
        return -1;
    }

    g.id = m_db.settings.nextGuestId++;
    if (g.registeredDay <= 0) {
        g.registeredDay = DateUtil::todayDayNumber();
    }

    m_db.guests.push_back(g);
    if (!g.cnic.empty()) {
        m_cnicHashTable.insertOrUpdate(g.cnic, g.id);
    }

    logAudit("GUEST_REGISTER", "Registered guest #" + std::to_string(g.id) + " (" + g.fullName + ")");
    m_fileManager.saveGuests(m_db.guests);
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Success, "Guest Registered", g.fullName + " added to CRM.");
    return g.id;
}

/*
 * Purpose: Updates an existing guest profile after re-validating fields and keeping GuestCnicHashTable synced.
 * Inputs: updatedGuest, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(G) to locate guest record, O(1) hash table update.
 */
bool HotelSystem::updateGuest(const Guest& updatedGuest, std::string* outError) {
    if (!checkPermission(Action::ManageGuests, "Update Guest")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Guest* existing = findGuestById(updatedGuest.id);
    if (existing == nullptr) {
        if (outError != nullptr) { *outError = "Guest not found."; }
        return false;
    }

    std::string err;
    if (!validatePersonName(updatedGuest.fullName, &err)) {
        if (outError != nullptr) { *outError = err; }
        return false;
    }

    Guest g = updatedGuest;
    if (!g.isForeign) {
        if (!validateCnic(g.cnic, &err)) {
            if (outError != nullptr) { *outError = err; }
            return false;
        }
        if (isDuplicateCnic(g.cnic, g.id)) {
            if (outError != nullptr) { *outError = "CNIC " + g.cnic + " belongs to another guest."; }
            return false;
        }
        std::string normalizedPhone;
        if (!validatePakistaniPhone(g.phone, &normalizedPhone, &err)) {
            if (outError != nullptr) { *outError = err; }
            return false;
        }
        g.phone = normalizedPhone;
    } else if (!validatePassport(g.passport, &err)) {
        if (outError != nullptr) { *outError = err; }
        return false;
    }

    if (!g.email.empty() && !validateEmail(g.email, &err)) {
        if (outError != nullptr) { *outError = err; }
        return false;
    }

    if (!existing->cnic.empty() && existing->cnic != g.cnic) {
        m_cnicHashTable.remove(existing->cnic);
    }
    *existing = g;
    if (!g.cnic.empty()) {
        m_cnicHashTable.insertOrUpdate(g.cnic, g.id);
    }

    logAudit("GUEST_UPDATE", "Updated guest #" + std::to_string(g.id) + " (" + g.fullName + ")");
    m_fileManager.saveGuests(m_db.guests);
    m_fileManager.saveAuditLog(m_db.auditLog);
    return true;
}

/*
 * Purpose: Finds a mutable Guest pointer by guestId.
 * Inputs: guestId.
 * Outputs: Pointer to Guest or nullptr.
 * Big-O: O(G) time, O(1) space.
 */
Guest* HotelSystem::findGuestById(int guestId) {
    for (Guest& g : m_db.guests) {
        if (g.id == guestId) {
            return &g;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a const Guest pointer by guestId.
 * Inputs: guestId.
 * Outputs: Const pointer to Guest or nullptr.
 * Big-O: O(G) time, O(1) space.
 */
const Guest* HotelSystem::findGuestById(int guestId) const {
    for (const Guest& g : m_db.guests) {
        if (g.id == guestId) {
            return &g;
        }
    }
    return nullptr;
}

/*
 * Purpose: Looks up a guestId using the custom 127-bucket GuestCnicHashTable.
 * Inputs: cnic string.
 * Outputs: guestId if found, or -1 if absent.
 * Big-O: O(1) average time, O(1) space.
 */
int HotelSystem::findGuestIdByCnicHash(const std::string& cnic) const {
    return m_cnicHashTable.findGuestId(cnic);
}

/*
 * Purpose: Looks up a Guest by CNIC using custom MergeSort + Binary Search (for Smart Check-In Step 1).
 * Inputs: cnic string.
 * Outputs: Const pointer to matching Guest inside m_db.guests, or nullptr.
 * Big-O: O(G log G) to sort temporary copy + O(log G) binary search.
 */
const Guest* HotelSystem::findGuestByCnicBinarySearch(const std::string& cnic) const {
    if (cnic.empty() || m_db.guests.empty()) {
        return nullptr;
    }
    std::vector<Guest> sorted = m_db.guests;
    mergeSort(
        sorted,
        [](const Guest& a, const Guest& b) { return a.cnic < b.cnic; },
        nullptr,
        nullptr,
        "MergeSort (Guests by CNIC)"
    );
    const int idx = binarySearchGuestByCnic(sorted, cnic);
    if (idx < 0) {
        return nullptr;
    }
    return findGuestById(sorted[static_cast<std::size_t>(idx)].id);
}

/*
 * Purpose: Checks whether a CNIC already exists in the custom hash table for another guest.
 * Inputs: cnic string, ignoreGuestId (-1 when registering new guest).
 * Outputs: true if duplicate exists, false otherwise.
 * Big-O: O(1) average time, O(1) space.
 */
bool HotelSystem::isDuplicateCnic(const std::string& cnic, int ignoreGuestId) const {
    if (cnic.empty()) {
        return false;
    }
    const int existingId = m_cnicHashTable.findGuestId(cnic);
    return (existingId != -1 && existingId != ignoreGuestId);
}

/*
 * Purpose: Loyalty state machine (Section 9.4): promotes only, never auto-demotes.
 *          Silver >= 5 stays OR >= 150,000 PKR; Gold >= 15 stays OR >= 500,000 PKR;
 *          Platinum >= 30 stays OR >= 1,500,000 PKR.
 * Inputs: currentTier, stayCount, lifetimeSpend.
 * Outputs: Resulting LoyaltyTier (greater or equal to currentTier).
 * Big-O: O(1) time, O(1) space.
 */
LoyaltyTier HotelSystem::evaluateLoyaltyTier(LoyaltyTier currentTier, int stayCount, double lifetimeSpend) {
    LoyaltyTier earned = LoyaltyTier::Standard;
    if (stayCount >= Cfg::PLATINUM_STAYS || lifetimeSpend >= Cfg::PLATINUM_SPEND) {
        earned = LoyaltyTier::Platinum;
    } else if (stayCount >= Cfg::GOLD_STAYS || lifetimeSpend >= Cfg::GOLD_SPEND) {
        earned = LoyaltyTier::Gold;
    } else if (stayCount >= Cfg::SILVER_STAYS || lifetimeSpend >= Cfg::SILVER_SPEND) {
        earned = LoyaltyTier::Silver;
    }
    return (static_cast<int>(earned) > static_cast<int>(currentTier)) ? earned : currentTier;
}

/*
 * Purpose: Returns the room-subtotal discount fraction for a LoyaltyTier (0%, 5%, 10%, 15%).
 * Inputs: tier (LoyaltyTier).
 * Outputs: Discount fraction in [0.0, 0.15].
 * Big-O: O(1) time, O(1) space.
 */
double HotelSystem::loyaltyDiscountRate(LoyaltyTier tier) {
    switch (tier) {
        case LoyaltyTier::Standard: return Cfg::DISCOUNT_STANDARD;
        case LoyaltyTier::Silver:   return Cfg::DISCOUNT_SILVER;
        case LoyaltyTier::Gold:     return Cfg::DISCOUNT_GOLD;
        case LoyaltyTier::Platinum: return Cfg::DISCOUNT_PLATINUM;
    }
    return 0.0;
}

// =============================================================================
// 4. LOOKUP HELPERS (ROOMS, BOOKINGS, INVOICES)
// =============================================================================
/*
 * Purpose: Finds a mutable Room pointer by roomId.
 * Inputs: roomId.
 * Outputs: Pointer to Room or nullptr.
 * Big-O: O(R) where R = 50, i.e., O(1).
 */
Room* HotelSystem::findRoomById(int roomId) {
    for (Room& r : m_db.rooms) {
        if (r.id == roomId) {
            return &r;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a const Room pointer by roomId.
 * Inputs: roomId.
 * Outputs: Const pointer to Room or nullptr.
 * Big-O: O(R) where R = 50.
 */
const Room* HotelSystem::findRoomById(int roomId) const {
    for (const Room& r : m_db.rooms) {
        if (r.id == roomId) {
            return &r;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a mutable Room pointer by room number (101..510).
 * Inputs: roomNumber.
 * Outputs: Pointer to Room or nullptr.
 * Big-O: O(R) where R = 50.
 */
Room* HotelSystem::findRoomByNumber(int roomNumber) {
    for (Room& r : m_db.rooms) {
        if (r.number == roomNumber) {
            return &r;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a const Room pointer by room number (101..510).
 * Inputs: roomNumber.
 * Outputs: Const pointer to Room or nullptr.
 * Big-O: O(R) where R = 50.
 */
const Room* HotelSystem::findRoomByNumber(int roomNumber) const {
    for (const Room& r : m_db.rooms) {
        if (r.number == roomNumber) {
            return &r;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a mutable Booking pointer by bookingId.
 * Inputs: bookingId.
 * Outputs: Pointer to Booking or nullptr.
 * Big-O: O(B) time, O(1) space.
 */
Booking* HotelSystem::findBookingById(int bookingId) {
    for (Booking& b : m_db.bookings) {
        if (b.id == bookingId) {
            return &b;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a const Booking pointer by bookingId.
 * Inputs: bookingId.
 * Outputs: Const pointer to Booking or nullptr.
 * Big-O: O(B) time, O(1) space.
 */
const Booking* HotelSystem::findBookingById(int bookingId) const {
    for (const Booking& b : m_db.bookings) {
        if (b.id == bookingId) {
            return &b;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a mutable Invoice pointer by invoiceId.
 * Inputs: invoiceId.
 * Outputs: Pointer to Invoice or nullptr.
 * Big-O: O(I) time, O(1) space.
 */
Invoice* HotelSystem::findInvoiceById(int invoiceId) {
    for (Invoice& inv : m_db.invoices) {
        if (inv.id == invoiceId) {
            return &inv;
        }
    }
    return nullptr;
}

/*
 * Purpose: Finds a const Invoice pointer by invoiceId.
 * Inputs: invoiceId.
 * Outputs: Const pointer to Invoice or nullptr.
 * Big-O: O(I) time, O(1) space.
 */
const Invoice* HotelSystem::findInvoiceById(int invoiceId) const {
    for (const Invoice& inv : m_db.invoices) {
        if (inv.id == invoiceId) {
            return &inv;
        }
    }
    return nullptr;
}

// =============================================================================
// 5. ROOM STATUS, BOOKINGS, CHECK-IN/OUT, CANCELLATION & ROOM SWAP
// =============================================================================
/*
 * Purpose: Changes a room's operational status, records an UndoCommand on the session stack,
 *          logs to the audit chain, and checks the waitlist if the room becomes Available.
 * Inputs: roomId, newStatus, recordUndo, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(R + W) time, O(1) space.
 */
bool HotelSystem::setRoomStatus(int roomId, RoomStatus newStatus, bool recordUndo, std::string* outError) {
    if (!checkPermission(Action::UpdateRoomStatus, "Update Room Status")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Room* room = findRoomById(roomId);
    if (room == nullptr) {
        if (outError != nullptr) { *outError = "Room not found."; }
        return false;
    }
    if (room->status == newStatus) {
        return true;
    }

    const RoomStatus oldStatus = room->status;
    const int roomNum = room->number;
    room->status = newStatus;

    logAudit(
        "ROOM_STATUS",
        "Room " + std::to_string(roomNum) + " status changed: " + toString(oldStatus) + " -> " + toString(newStatus)
    );
    m_fileManager.saveRooms(m_db.rooms);
    m_fileManager.saveAuditLog(m_db.auditLog);

    if (recordUndo) {
        UndoCommand cmd{};
        cmd.name = "Room " + std::to_string(roomNum) + " -> " + toString(newStatus);
        cmd.apply = [this, roomId, newStatus]() {
            if (Room* r = findRoomById(roomId)) {
                r->status = newStatus;
                m_fileManager.saveRooms(m_db.rooms);
            }
        };
        cmd.revert = [this, roomId, oldStatus]() {
            if (Room* r = findRoomById(roomId)) {
                r->status = oldStatus;
                m_fileManager.saveRooms(m_db.rooms);
            }
        };
        pushUndoCommand(std::move(cmd));
    }

    if (newStatus == RoomStatus::Available) {
        checkWaitlistMatchForRoom(roomId);
    }
    return true;
}

/*
 * Purpose: Creates a new reservation (or immediate walk-in check-in) after verifying RBAC,
 *          date validity, and zero overlap via binary-search interval conflict detection (Section 9.6).
 *          Locks today's dynamic surge multiplier at booking time and attaches Gold free-breakfast perk if applicable.
 * Inputs: guestId, roomId, checkInDay, checkOutDay, adults, children, promoCode, immediateCheckIn, outError.
 * Outputs: New bookingId (>0), or -1 on conflict/error.
 * Big-O: O(B log B) for conflict detection, O(B) space.
 */
int HotelSystem::createBooking(
    int guestId,
    int roomId,
    int32_t checkInDay,
    int32_t checkOutDay,
    int adults,
    int children,
    const std::string& promoCode,
    bool immediateCheckIn,
    std::string* outError
) {
    const Action reqAction = immediateCheckIn ? Action::CheckInGuest : Action::CreateBooking;
    if (!checkPermission(reqAction, immediateCheckIn ? "Walk-In Check-In" : "Create Booking")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return -1;
    }

    Guest* guest = findGuestById(guestId);
    Room* room = findRoomById(roomId);
    if (guest == nullptr || room == nullptr) {
        if (outError != nullptr) { *outError = "Invalid guest or room selection."; }
        return -1;
    }
    if (checkOutDay <= checkInDay) {
        if (outError != nullptr) { *outError = "Check-out date must be after check-in date."; }
        return -1;
    }
    if (room->status == RoomStatus::Maintenance) {
        if (outError != nullptr) { *outError = "Room " + std::to_string(room->number) + " is under maintenance."; }
        return -1;
    }
    if (immediateCheckIn && room->status != RoomStatus::Available && room->status != RoomStatus::Reserved) {
        if (outError != nullptr) {
            *outError = "Room " + std::to_string(room->number) + " is not ready for immediate check-in (" +
                        toString(room->status) + ").";
        }
        return -1;
    }

    // Overbooking impossible by construction: binary-search half-open interval conflict check
    if (hasRoomBookingConflict(m_db.bookings, roomId, checkInDay, checkOutDay)) {
        if (outError != nullptr) {
            *outError = "Booking conflict: Room " + std::to_string(room->number) + " is already booked for these dates.";
        }
        return -1;
    }

    const YieldPricingResult yield = getYieldPricingForDay(checkInDay);
    const int32_t today = DateUtil::todayDayNumber();

    Booking b{};
    b.id              = m_db.settings.nextBookingId++;
    b.guestId         = guestId;
    b.roomId          = roomId;
    b.checkInDay      = checkInDay;
    b.checkOutDay     = checkOutDay;
    b.checkInMinute   = immediateCheckIn ? DateUtil::currentMinuteOfDay() : 840;
    b.checkOutMinute  = 720;
    b.adults          = std::max(1, adults);
    b.children        = std::max(0, children);
    b.status          = immediateCheckIn ? BookingStatus::CheckedIn : BookingStatus::Reserved;
    b.quotedRate      = room->baseRate;
    b.surgeMultiplier = yield.surge;
    b.promoCode       = toUpperAscii(promoCode);
    b.invoiceId       = -1;
    b.createdDay      = today;

    // Section 9.4 Gold/Platinum perk: complimentary PKR 0 "Breakfast" line item on each night
    if (guest->tier == LoyaltyTier::Gold || guest->tier == LoyaltyTier::Platinum) {
        const int nights = std::max(1, checkOutDay - checkInDay);
        b.addOns.push_back(AddOn{
            AddOnType::Dining,
            "Complimentary Breakfast (" + std::string(toString(guest->tier)) + " Perk)",
            0.0,
            nights,
            checkInDay
        });
    }

    m_db.bookings.push_back(b);

    if (immediateCheckIn) {
        room->status = RoomStatus::Occupied;
        room->currentBookingId = b.id;
    } else if (checkInDay <= today && room->status == RoomStatus::Available) {
        room->status = RoomStatus::Reserved;
        room->currentBookingId = b.id;
    }

    logAudit(
        immediateCheckIn ? "CHECK_IN" : "BOOKING_CREATE",
        "Booking #" + std::to_string(b.id) + " for " + guest->fullName + " in Room " + std::to_string(room->number) +
            " (" + DateUtil::formatDate(checkInDay) + " to " + DateUtil::formatDate(checkOutDay) + ")"
    );
    m_fileManager.saveBookings(m_db.bookings);
    m_fileManager.saveRooms(m_db.rooms);
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);

    pushNotice(
        NoticeType::Success,
        immediateCheckIn ? "Guest Checked In" : "Reservation Confirmed",
        guest->fullName + " -> Room " + std::to_string(room->number) + " (Booking #" + std::to_string(b.id) + ")"
    );
    return b.id;
}

/*
 * Purpose: Checks in an existing Reserved booking, marking the room Occupied.
 * Inputs: bookingId, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(B + R) time, O(1) space.
 */
bool HotelSystem::checkInBooking(int bookingId, std::string* outError) {
    if (!checkPermission(Action::CheckInGuest, "Check-In Guest")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr) {
        if (outError != nullptr) { *outError = "Booking not found."; }
        return false;
    }
    if (b->status != BookingStatus::Reserved) {
        if (outError != nullptr) { *outError = "Only Reserved bookings can be checked in."; }
        return false;
    }

    Room* room = findRoomById(b->roomId);
    if (room == nullptr) {
        if (outError != nullptr) { *outError = "Assigned room not found."; }
        return false;
    }
    if (room->status == RoomStatus::Occupied || room->status == RoomStatus::Maintenance ||
        room->status == RoomStatus::CleaningRequired) {
        if (outError != nullptr) {
            *outError = "Room " + std::to_string(room->number) + " is currently " + toString(room->status) + ".";
        }
        return false;
    }

    b->status = BookingStatus::CheckedIn;
    b->checkInMinute = DateUtil::currentMinuteOfDay();
    room->status = RoomStatus::Occupied;
    room->currentBookingId = b->id;

    const Guest* g = findGuestById(b->guestId);
    logAudit(
        "CHECK_IN",
        "Checked in Booking #" + std::to_string(b->id) + " (" + (g ? g->fullName : "Guest") + ") to Room " +
            std::to_string(room->number)
    );
    m_fileManager.saveBookings(m_db.bookings);
    m_fileManager.saveRooms(m_db.rooms);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Success, "Checked In", "Room " + std::to_string(room->number) + " is now Occupied.");
    return true;
}

/*
 * Purpose: Checks out a CheckedIn guest: computes final folio (with late checkout fee / Platinum waiver),
 *          generates Invoice, exports TXT & HTML receipts, updates Guest stayCount & lifetimeSpend,
 *          evaluates Loyalty promotion, sets room to CleaningRequired, and pushes a Cleaning task to TaskMinHeap.
 * Inputs: bookingId, checkoutMinuteOfDay, paymentMethod, optional outError.
 * Outputs: CheckOutResult detailing invoiceId, loyalty promotion, and exported file paths.
 * Big-O: O(B + T log T) time, O(1) space.
 */
CheckOutResult HotelSystem::checkOutBooking(
    int bookingId,
    int32_t checkoutMinuteOfDay,
    const std::string& paymentMethod,
    std::string* outError
) {
    CheckOutResult res{};
    if (!checkPermission(Action::CheckOutGuest, "Check-Out Guest")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        res.message = "Permission denied";
        return res;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr || b->status != BookingStatus::CheckedIn) {
        if (outError != nullptr) { *outError = "Booking is not currently Checked In."; }
        res.message = "Booking not checked in.";
        return res;
    }

    Guest* guest = findGuestById(b->guestId);
    Room* room = findRoomById(b->roomId);
    if (guest == nullptr || room == nullptr) {
        if (outError != nullptr) { *outError = "Associated guest or room missing."; }
        res.message = "Missing guest or room.";
        return res;
    }

    const int32_t today = DateUtil::todayDayNumber();
    const int32_t actualCheckoutDay = std::max(b->checkOutDay, b->checkInDay + 1);
    const FolioBreakdown folio = calculateFolio(*b, *guest, actualCheckoutDay, checkoutMinuteOfDay);

    Invoice inv{};
    inv.id              = m_db.settings.nextInvoiceId++;
    inv.number          = "INV-" + std::to_string(inv.id);
    inv.bookingId       = b->id;
    inv.guestId         = guest->id;
    inv.issuedDay       = today;
    inv.nights          = folio.nights;
    inv.roomSubtotal    = folio.roomSubtotal;
    inv.addOnSubtotal   = folio.addOnSubtotal + folio.lateCheckoutFee;
    inv.loyaltyDiscount = folio.loyaltyDiscount;
    inv.promoDiscount   = folio.promoDiscount;
    inv.taxableAmount   = folio.taxableAmount;
    inv.taxRate         = folio.taxRate;
    inv.taxAmount       = folio.taxAmount;
    inv.grandTotal      = folio.grandTotal;
    inv.paid            = !paymentMethod.empty();
    inv.paymentMethod   = paymentMethod.empty() ? "Pending" : paymentMethod;
    m_db.invoices.push_back(inv);

    b->status         = BookingStatus::CheckedOut;
    b->checkOutMinute = checkoutMinuteOfDay;
    b->invoiceId      = inv.id;

    // Update room state and auto-dispatch Cleaning task into TaskMinHeap (Section 9.9)
    room->status             = RoomStatus::CleaningRequired;
    room->currentBookingId   = -1;
    room->lastCheckoutDay    = today;
    room->lastCheckoutMinute = checkoutMinuteOfDay;

    dispatchHousekeepingTask(
        room->id,
        TaskType::Cleaning,
        0,
        "Housekeeping Team",
        "Auto-dispatched after checkout of Booking #" + std::to_string(b->id)
    );

    // Update Guest CRM & Loyalty State Machine (Section 9.4: promote only, never demote)
    const LoyaltyTier oldTier = guest->tier;
    guest->stayCount += 1;
    guest->lifetimeSpend += inv.grandTotal;
    const LoyaltyTier newTier = evaluateLoyaltyTier(oldTier, guest->stayCount, guest->lifetimeSpend);
    guest->tier = newTier;

    res.success         = true;
    res.invoiceId       = inv.id;
    res.oldTier         = oldTier;
    res.newTier         = newTier;
    res.loyaltyPromoted = (static_cast<int>(newTier) > static_cast<int>(oldTier));

    exportInvoiceFiles(inv, &res.txtExportPath, &res.htmlExportPath);

    logAudit(
        "CHECK_OUT",
        "Room " + std::to_string(room->number) + " checked out; " + inv.number + " (" + formatPKR(inv.grandTotal) + ")"
    );

    saveAll();

    res.message = "Room " + std::to_string(room->number) + " Checked Out — Invoice #" + inv.number + " Generated";
    pushNotice(NoticeType::Success, "Check-Out Complete", res.message);
    if (res.loyaltyPromoted) {
        pushNotice(
            NoticeType::Success,
            "Loyalty Promotion!",
            guest->fullName + " promoted from " + toString(oldTier) + " to " + toString(newTier) + "!"
        );
    }
    return res;
}

/*
 * Purpose: Cancels a Reserved booking or marks it as NoShow per Section 9.6 cancellation policy:
 *          >48h before check-in: free; 24-48h: 1 night charged; <24h or no-show: 100% first night.
 *          Creates a "Cancellation fee" invoice when applicable, frees the room, and scans the waitlist.
 * Inputs: bookingId, isNoShow, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(B + W) time, O(1) space.
 */
bool HotelSystem::cancelBooking(int bookingId, bool isNoShow, std::string* outError) {
    if (!checkPermission(Action::CancelBooking, isNoShow ? "Mark No-Show" : "Cancel Booking")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr || b->status != BookingStatus::Reserved) {
        if (outError != nullptr) { *outError = "Only Reserved bookings can be cancelled or marked No-Show."; }
        return false;
    }

    const int32_t today = DateUtil::todayDayNumber();
    const int32_t nowMin = DateUtil::currentMinuteOfDay();
    const long long hoursBeforeCheckIn =
        (static_cast<long long>(b->checkInDay - today) * 24LL) +
        (static_cast<long long>(b->checkInMinute - nowMin) / 60LL);

    double cancellationFee = 0.0;
    if (isNoShow || hoursBeforeCheckIn <= 48LL) {
        // Section 9.6: 24-48h -> 1 night charged; <24h or no-show -> 100% first night
        cancellationFee = roundToNearest10PKR(b->quotedRate * b->surgeMultiplier);
    }

    b->status = isNoShow ? BookingStatus::NoShow : BookingStatus::Cancelled;

    if (cancellationFee > 0.0) {
        b->addOns.push_back(AddOn{
            AddOnType::Dining,
            isNoShow ? "No-Show Cancellation fee (1st night)" : "Cancellation fee (1st night)",
            cancellationFee,
            1,
            today
        });

        Invoice inv{};
        inv.id              = m_db.settings.nextInvoiceId++;
        inv.number          = "INV-" + std::to_string(inv.id);
        inv.bookingId       = b->id;
        inv.guestId         = b->guestId;
        inv.issuedDay       = today;
        inv.nights          = 0;
        inv.roomSubtotal    = 0.0;
        inv.addOnSubtotal   = cancellationFee;
        inv.loyaltyDiscount = 0.0;
        inv.promoDiscount   = 0.0;
        inv.taxableAmount   = cancellationFee;
        inv.taxRate         = m_db.settings.taxRate;
        inv.taxAmount       = cancellationFee * inv.taxRate;
        inv.grandTotal      = static_cast<double>(std::llround(inv.taxableAmount + inv.taxAmount));
        inv.paid            = false;
        inv.paymentMethod   = "Unpaid";
        m_db.invoices.push_back(inv);
        b->invoiceId = inv.id;
    }

    Room* room = findRoomById(b->roomId);
    if (room != nullptr && room->currentBookingId == b->id) {
        room->status = RoomStatus::Available;
        room->currentBookingId = -1;
    }

    logAudit(
        isNoShow ? "BOOKING_NOSHOW" : "BOOKING_CANCEL",
        "Booking #" + std::to_string(b->id) + (isNoShow ? " marked No-Show" : " cancelled") +
            (cancellationFee > 0.0 ? (" (Fee: " + formatPKR(cancellationFee) + ")") : " (Free cancellation)")
    );
    saveAll();

    if (room != nullptr) {
        checkWaitlistMatchForRoom(room->id);
    }
    pushNotice(
        NoticeType::Warning,
        isNoShow ? "Marked No-Show" : "Booking Cancelled",
        "Booking #" + std::to_string(b->id) + (cancellationFee > 0.0 ? " — Fee: " + formatPKR(cancellationFee) : " — No fee")
    );
    return true;
}

/*
 * Purpose: Swaps a Reserved or CheckedIn booking to a new room after verifying conflict-free availability.
 *          Records an UndoCommand on the session Undo stack and writes an audit entry.
 * Inputs: bookingId, newRoomId, recordUndo, optional outError.
 * Outputs: true on success, false on conflict or invalid state.
 * Big-O: O(B log B) for conflict check.
 */
bool HotelSystem::swapBookingRoom(int bookingId, int newRoomId, bool recordUndo, std::string* outError) {
    if (!checkPermission(Action::RoomSwap, "Room Swap")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr || (b->status != BookingStatus::Reserved && b->status != BookingStatus::CheckedIn)) {
        if (outError != nullptr) { *outError = "Only active bookings can swap rooms."; }
        return false;
    }
    if (b->roomId == newRoomId) {
        return true;
    }

    Room* oldRoom = findRoomById(b->roomId);
    Room* newRoom = findRoomById(newRoomId);
    if (oldRoom == nullptr || newRoom == nullptr) {
        if (outError != nullptr) { *outError = "Room not found."; }
        return false;
    }
    if (newRoom->status == RoomStatus::Maintenance) {
        if (outError != nullptr) { *outError = "Target room is under maintenance."; }
        return false;
    }
    if (hasRoomBookingConflict(m_db.bookings, newRoomId, b->checkInDay, b->checkOutDay, b->id)) {
        if (outError != nullptr) {
            *outError = "Conflict: Room " + std::to_string(newRoom->number) + " is occupied during these dates.";
        }
        return false;
    }

    const int oldRoomId = oldRoom->id;
    const int oldRoomNum = oldRoom->number;
    const int newRoomNum = newRoom->number;
    const double oldQuotedRate = b->quotedRate;
    const double newQuotedRate = newRoom->baseRate;

    auto applySwap = [this, bookingId, oldRoomId, newRoomId, newQuotedRate]() {
        Booking* bk = findBookingById(bookingId);
        Room* rOld = findRoomById(oldRoomId);
        Room* rNew = findRoomById(newRoomId);
        if (bk && rOld && rNew) {
            bk->roomId = newRoomId;
            bk->quotedRate = newQuotedRate;
            if (bk->status == BookingStatus::CheckedIn) {
                rOld->status = RoomStatus::Available;
                rOld->currentBookingId = -1;
                rNew->status = RoomStatus::Occupied;
                rNew->currentBookingId = bk->id;
            } else if (rOld->currentBookingId == bk->id) {
                rOld->status = RoomStatus::Available;
                rOld->currentBookingId = -1;
                rNew->status = RoomStatus::Reserved;
                rNew->currentBookingId = bk->id;
            }
            m_fileManager.saveBookings(m_db.bookings);
            m_fileManager.saveRooms(m_db.rooms);
        }
    };

    auto revertSwap = [this, bookingId, oldRoomId, newRoomId, oldQuotedRate]() {
        Booking* bk = findBookingById(bookingId);
        Room* rOld = findRoomById(oldRoomId);
        Room* rNew = findRoomById(newRoomId);
        if (bk && rOld && rNew) {
            bk->roomId = oldRoomId;
            bk->quotedRate = oldQuotedRate;
            if (bk->status == BookingStatus::CheckedIn) {
                rNew->status = RoomStatus::Available;
                rNew->currentBookingId = -1;
                rOld->status = RoomStatus::Occupied;
                rOld->currentBookingId = bk->id;
            } else if (rNew->currentBookingId == bk->id) {
                rNew->status = RoomStatus::Available;
                rNew->currentBookingId = -1;
                rOld->status = RoomStatus::Reserved;
                rOld->currentBookingId = bk->id;
            }
            m_fileManager.saveBookings(m_db.bookings);
            m_fileManager.saveRooms(m_db.rooms);
        }
    };

    applySwap();
    logAudit(
        "ROOM_SWAP",
        "Booking #" + std::to_string(bookingId) + " swapped from Room " + std::to_string(oldRoomNum) +
            " to Room " + std::to_string(newRoomNum)
    );
    m_fileManager.saveAuditLog(m_db.auditLog);

    if (recordUndo) {
        UndoCommand cmd{};
        cmd.name = "Swap #" + std::to_string(bookingId) + " (" + std::to_string(oldRoomNum) + "->" + std::to_string(newRoomNum) + ")";
        cmd.apply = applySwap;
        cmd.revert = revertSwap;
        pushUndoCommand(std::move(cmd));
    }

    pushNotice(
        NoticeType::Info,
        "Room Swapped",
        "Booking #" + std::to_string(bookingId) + " moved to Room " + std::to_string(newRoomNum)
    );
    return true;
}

/*
 * Purpose: Edits check-in/check-out dates of an active booking (used by Gantt timeline drag-resize),
 *          enforcing conflict-free interval validation and pushing an UndoCommand.
 * Inputs: bookingId, newCheckInDay, newCheckOutDay, recordUndo, optional outError.
 * Outputs: true on success, false on conflict or invalid dates.
 * Big-O: O(B log B) for conflict check.
 */
bool HotelSystem::editBookingDates(
    int bookingId,
    int32_t newCheckInDay,
    int32_t newCheckOutDay,
    bool recordUndo,
    std::string* outError
) {
    if (!checkPermission(Action::EditBooking, "Edit Booking Dates")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr || (b->status != BookingStatus::Reserved && b->status != BookingStatus::CheckedIn)) {
        if (outError != nullptr) { *outError = "Only active bookings can be resized."; }
        return false;
    }
    if (newCheckOutDay <= newCheckInDay) {
        if (outError != nullptr) { *outError = "Check-out day must be after check-in day."; }
        return false;
    }
    if (hasRoomBookingConflict(m_db.bookings, b->roomId, newCheckInDay, newCheckOutDay, b->id)) {
        if (outError != nullptr) { *outError = "Date change conflicts with another booking on this room."; }
        return false;
    }

    const int32_t oldIn = b->checkInDay;
    const int32_t oldOut = b->checkOutDay;
    b->checkInDay = newCheckInDay;
    b->checkOutDay = newCheckOutDay;

    logAudit(
        "BOOKING_EDIT",
        "Booking #" + std::to_string(bookingId) + " dates updated to " +
            DateUtil::formatDate(newCheckInDay) + " -> " + DateUtil::formatDate(newCheckOutDay)
    );
    m_fileManager.saveBookings(m_db.bookings);
    m_fileManager.saveAuditLog(m_db.auditLog);

    if (recordUndo) {
        UndoCommand cmd{};
        cmd.name = "Resize Booking #" + std::to_string(bookingId);
        cmd.apply = [this, bookingId, newCheckInDay, newCheckOutDay]() {
            if (Booking* bk = findBookingById(bookingId)) {
                bk->checkInDay = newCheckInDay;
                bk->checkOutDay = newCheckOutDay;
                m_fileManager.saveBookings(m_db.bookings);
            }
        };
        cmd.revert = [this, bookingId, oldIn, oldOut]() {
            if (Booking* bk = findBookingById(bookingId)) {
                bk->checkInDay = oldIn;
                bk->checkOutDay = oldOut;
                m_fileManager.saveBookings(m_db.bookings);
            }
        };
        pushUndoCommand(std::move(cmd));
    }
    return true;
}

/*
 * Purpose: Adds an itemized AddOn charge (Minibar, Laundry, Dining, Spa, Transport) to a booking
 *          and pushes an UndoCommand onto the session stack.
 * Inputs: bookingId, addOn, recordUndo, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(B) time, O(1) space.
 */
bool HotelSystem::addChargeToBooking(int bookingId, const AddOn& addOn, bool recordUndo, std::string* outError) {
    if (!checkPermission(Action::AddServiceCharge, "Add Service Charge")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }

    Booking* b = findBookingById(bookingId);
    if (b == nullptr || (b->status != BookingStatus::CheckedIn && b->status != BookingStatus::Reserved)) {
        if (outError != nullptr) { *outError = "Charges can only be added to active bookings."; }
        return false;
    }
    if (addOn.quantity <= 0 || addOn.unitPrice < 0.0) {
        if (outError != nullptr) { *outError = "Invalid quantity or unit price."; }
        return false;
    }

    b->addOns.push_back(addOn);
    logAudit(
        "ADDON_CHARGE",
        "Booking #" + std::to_string(bookingId) + " charged " + addOn.description + " x" +
            std::to_string(addOn.quantity) + " (" + formatPKR(addOn.unitPrice * addOn.quantity) + ")"
    );
    m_fileManager.saveBookings(m_db.bookings);
    m_fileManager.saveAuditLog(m_db.auditLog);

    if (recordUndo) {
        UndoCommand cmd{};
        cmd.name = "Charge: " + addOn.description;
        cmd.apply = [this, bookingId, addOn]() {
            if (Booking* bk = findBookingById(bookingId)) {
                bk->addOns.push_back(addOn);
                m_fileManager.saveBookings(m_db.bookings);
            }
        };
        cmd.revert = [this, bookingId]() {
            if (Booking* bk = findBookingById(bookingId)) {
                if (!bk->addOns.empty()) {
                    bk->addOns.pop_back();
                    m_fileManager.saveBookings(m_db.bookings);
                }
            }
        };
        pushUndoCommand(std::move(cmd));
    }

    pushNotice(
        NoticeType::Info,
        "Charge Posted",
        addOn.description + " (" + formatPKR(addOn.unitPrice * addOn.quantity) + ") added to Booking #" +
            std::to_string(bookingId)
    );
    return true;
}

// =============================================================================
// 6. WAITLIST FIFO QUEUE & AUTO-MATCHING (Section 9.6)
// =============================================================================
/*
 * Purpose: Adds a guest to the FIFO Waitlist queue (std::queue + persisted m_db.waitlist).
 * Inputs: guestId, wantedType, fromDay, toDay, maxBudget, optional outError.
 * Outputs: New waitlistId (>0), or -1 on error.
 * Big-O: O(1) amortized push, O(W) save.
 */
int HotelSystem::addToWaitlist(
    int guestId,
    RoomType wantedType,
    int32_t fromDay,
    int32_t toDay,
    double maxBudget,
    std::string* outError
) {
    if (!checkPermission(Action::ManageWaitlist, "Add to Waitlist")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return -1;
    }
    if (findGuestById(guestId) == nullptr || toDay <= fromDay || maxBudget <= 0.0) {
        if (outError != nullptr) { *outError = "Invalid guest, date range, or budget for waitlist."; }
        return -1;
    }

    WaitlistEntry wl{};
    wl.id         = m_db.settings.nextWaitlistId++;
    wl.guestId    = guestId;
    wl.wantedType = wantedType;
    wl.fromDay    = fromDay;
    wl.toDay      = toDay;
    wl.maxBudget  = maxBudget;
    wl.createdDay = DateUtil::todayDayNumber();

    m_db.waitlist.push_back(wl);
    m_waitlistQueue.push(wl);

    logAudit("WAITLIST_ADD", "Guest #" + std::to_string(guestId) + " added to waitlist for " + toString(wantedType));
    m_fileManager.saveWaitlist(m_db.waitlist);
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Info, "Waitlist Updated", "Guest added to FIFO waitlist (#" + std::to_string(wl.id) + ").");
    return wl.id;
}

/*
 * Purpose: Removes an entry from m_db.waitlist and rebuilds the FIFO std::queue.
 * Inputs: waitlistId.
 * Outputs: true if removed, false if not found.
 * Big-O: O(W log W) time, O(W) space.
 */
bool HotelSystem::removeWaitlistEntry(int waitlistId) {
    for (std::size_t i = 0U; i < m_db.waitlist.size(); ++i) {
        if (m_db.waitlist[i].id == waitlistId) {
            m_db.waitlist.erase(m_db.waitlist.begin() + static_cast<std::ptrdiff_t>(i));
            syncWaitlistQueueFromDb();
            m_fileManager.saveWaitlist(m_db.waitlist);
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Scans the FIFO waitlist queue in arrival order when a room frees up; auto-matches the first entry
 *          whose wantedType, dates (conflict-free), and maxBudget fit, and pushes a toast with [Book Now] metadata.
 * Inputs: roomId.
 * Outputs: Matched waitlistId (>0), or -1 if no waitlist entry matches.
 * Big-O: O(W * B log B) where W is waitlist length.
 */
int HotelSystem::checkWaitlistMatchForRoom(int roomId) {
    const Room* room = findRoomById(roomId);
    if (room == nullptr || room->status != RoomStatus::Available) {
        return -1;
    }

    std::queue<WaitlistEntry> scanQueue = m_waitlistQueue;
    while (!scanQueue.empty()) {
        const WaitlistEntry wl = scanQueue.front();
        scanQueue.pop();

        if (wl.wantedType != room->type) {
            continue;
        }
        const YieldPricingResult yield = getYieldPricingForDay(wl.fromDay);
        const double estRate = roundToNearest10PKR(room->baseRate * yield.surge);
        if (estRate > wl.maxBudget) {
            continue;
        }
        if (hasRoomBookingConflict(m_db.bookings, roomId, wl.fromDay, wl.toDay)) {
            continue;
        }

        const Guest* g = findGuestById(wl.guestId);
        const std::string gName = (g != nullptr) ? g->fullName : ("Guest #" + std::to_string(wl.guestId));
        pushNotice(
            NoticeType::Success,
            "Waitlist Match Found!",
            gName + " matches Room " + std::to_string(room->number) + " (" + toString(room->type) + ") — Click [Book Now]",
            wl.id,
            room->id
        );
        return wl.id;
    }
    return -1;
}

// =============================================================================
// 7. BILLING, PROMO CODES, FOLIO CALCULATION & TXT/HTML EXPORT (Section 9.5)
// =============================================================================
/*
 * Purpose: Returns the promo code table required by Section 9.5:
 *          WELCOME10 (10% room), NUST500 (flat PKR 500), SEECS15 (15% on 3+ nights).
 * Inputs: None.
 * Outputs: Const reference to static vector of PromoCodeRule.
 * Big-O: O(1) time, O(1) space.
 */
const std::vector<PromoCodeRule>& HotelSystem::promoCodeTable() {
    static const std::vector<PromoCodeRule> kPromos = {
        PromoCodeRule{"WELCOME10", "10% off room subtotal", false, 0.10, 1},
        PromoCodeRule{"NUST500",   "Flat PKR 500 off",      true,  500.0, 1},
        PromoCodeRule{"SEECS15",   "15% off room (3+ nights)", false, 0.15, 3}
    };
    return kPromos;
}

/*
 * Purpose: Computes complete itemized FolioBreakdown per Section 9.5:
 *          nightRate = baseRate * surgeMultiplier (locked at booking),
 *          roomSubtotal = nights * nightRate,
 *          addOnSubtotal = sum(unitPrice * quantity),
 *          lateCheckoutFee = 50% of one night if actualCheckoutMinute > 720 (12:00) unless Platinum (waived until 16:00),
 *          loyaltyDiscount = tierPct * roomSubtotal,
 *          promoDiscount = single matching promo code (never below zero),
 *          taxable = max(0, roomSubtotal + addOnSubtotal + lateCheckoutFee - loyaltyDiscount - promoDiscount),
 *          tax = taxable * taxRate, grandTotal = round(taxable + tax).
 * Inputs: booking, guest, actualCheckoutDay, actualCheckoutMinute.
 * Outputs: Populated FolioBreakdown struct.
 * Big-O: O(A) where A is number of booking add-ons.
 */
FolioBreakdown HotelSystem::calculateFolio(
    const Booking& booking,
    const Guest& guest,
    int32_t actualCheckoutDay,
    int32_t actualCheckoutMinute
) const {
    FolioBreakdown f{};
    f.nights    = std::max(1, actualCheckoutDay - booking.checkInDay);
    f.nightRate = booking.quotedRate * std::max(1.0, booking.surgeMultiplier);
    f.roomSubtotal = f.nightRate * static_cast<double>(f.nights);

    double addOnSum = 0.0;
    for (const AddOn& a : booking.addOns) {
        addOnSum += a.unitPrice * static_cast<double>(a.quantity);
    }
    f.addOnSubtotal = addOnSum;

    // Late checkout after 12:00 (minute 720): +50% of one night unless Platinum (complimentary until 16:00 = 960)
    if (actualCheckoutMinute > 720) {
        if (guest.tier == LoyaltyTier::Platinum && actualCheckoutMinute <= 960) {
            f.lateCheckoutFee = 0.0;
            f.lateCheckoutWaived = true;
        } else if (guest.tier != LoyaltyTier::Platinum) {
            f.lateCheckoutFee = 0.50 * f.nightRate;
        }
    }

    f.freeBreakfastPerk = (guest.tier == LoyaltyTier::Gold || guest.tier == LoyaltyTier::Platinum);
    f.loyaltyDiscount   = loyaltyDiscountRate(guest.tier) * f.roomSubtotal;

    // Only ONE promo code applies
    const std::string codeUpper = toUpperAscii(booking.promoCode);
    for (const PromoCodeRule& rule : promoCodeTable()) {
        if (codeUpper == rule.code && f.nights >= rule.minNights) {
            f.appliedPromoCode = rule.code;
            f.promoDiscount = rule.isFlat ? rule.value : (rule.value * f.roomSubtotal);
            break;
        }
    }

    const double gross = f.roomSubtotal + f.addOnSubtotal + f.lateCheckoutFee;
    f.taxableAmount = std::max(0.0, gross - f.loyaltyDiscount - f.promoDiscount);
    f.taxRate       = m_db.settings.taxRate;
    f.taxAmount     = f.taxableAmount * f.taxRate;
    f.grandTotal    = static_cast<double>(std::llround(f.taxableAmount + f.taxAmount));
    return f;
}

/*
 * Purpose: Marks an existing Invoice as paid with the chosen payment method and persists it.
 * Inputs: invoiceId, paymentMethod, optional outError.
 * Outputs: true on success, false otherwise.
 * Big-O: O(I) time, O(1) space.
 */
bool HotelSystem::markInvoicePaid(int invoiceId, const std::string& paymentMethod, std::string* outError) {
    if (!checkPermission(Action::ManageBilling, "Mark Invoice Paid")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }
    Invoice* inv = findInvoiceById(invoiceId);
    if (inv == nullptr) {
        if (outError != nullptr) { *outError = "Invoice not found."; }
        return false;
    }
    inv->paid = true;
    inv->paymentMethod = paymentMethod.empty() ? "Cash" : paymentMethod;
    logAudit("INVOICE_PAID", "Marked " + inv->number + " paid via " + inv->paymentMethod);
    m_fileManager.saveInvoices(m_db.invoices);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Success, "Payment Recorded", inv->number + " marked PAID (" + inv->paymentMethod + ").");
    return true;
}

/*
 * Purpose: Exports an Invoice to both:
 *          1. exports/<number>.txt — 80-column aligned plain-ASCII receipt with box drawing
 *          2. exports/<number>.html — self-contained, inline CSS, @media print, hotel header,
 *             guest block, itemized table, totals, tier badge, and CSS decorative QR block (Section 9.5).
 * Inputs: invoice, optional outTxtPath, optional outHtmlPath.
 * Outputs: true if both files were written cleanly.
 * Big-O: O(A) where A is number of booking add-ons.
 */
bool HotelSystem::exportInvoiceFiles(
    const Invoice& invoice,
    std::string* outTxtPath,
    std::string* outHtmlPath
) const {
    m_fileManager.ensureDirectories();

    const Guest* guest = findGuestById(invoice.guestId);
    const Booking* booking = findBookingById(invoice.bookingId);
    const Room* room = (booking != nullptr) ? findRoomById(booking->roomId) : nullptr;

    const std::string guestName = (guest != nullptr) ? guest->fullName : ("Guest #" + std::to_string(invoice.guestId));
    const std::string guestIdDoc = (guest != nullptr)
                                       ? (guest->isForeign ? ("Passport: " + guest->passport) : ("CNIC: " + guest->cnic))
                                       : "N/A";
    const std::string guestPhone = (guest != nullptr) ? guest->phone : "N/A";
    const std::string guestTier  = (guest != nullptr) ? toString(guest->tier) : "Standard";
    const std::string roomDesc   = (room != nullptr)
                                       ? ("Room " + std::to_string(room->number) + " (" + toString(room->type) + ")")
                                       : "Hotel Room";

    const std::string baseName = (invoice.number.rfind("INV-", 0U) == 0U)
                                     ? invoice.number
                                     : ("INV-" + invoice.number);
    const std::string txtPath  = (std::filesystem::path(m_fileManager.getExportDir()) / (baseName + ".txt")).string();
    const std::string htmlPath = (std::filesystem::path(m_fileManager.getExportDir()) / (baseName + ".html")).string();

    // 1. Write 80-column aligned plain-ASCII receipt
    {
        std::ofstream txt(txtPath, std::ios::trunc);
        if (!txt.is_open()) {
            return false;
        }
        const std::string borderTop = "+" + std::string(78U, '=') + "+";
        const std::string borderMid = "+" + std::string(78U, '-') + "+";

        txt << borderTop << "\n";
        txt << formatReceiptCenter80("NUST SEECS GRAND HOTEL & RESIDENCES") << "\n";
        txt << formatReceiptCenter80("Sector H-12, Islamabad, Pakistan  |  Tel: +92 51 9085 2000") << "\n";
        txt << formatReceiptCenter80("OFFICIAL TAX INVOICE / FOLIO RECEIPT") << "\n";
        txt << borderTop << "\n";
        txt << formatReceiptRow80("Invoice No: " + invoice.number, "Issued: " + DateUtil::formatDate(invoice.issuedDay)) << "\n";
        txt << formatReceiptRow80("Guest: " + guestName + " [" + guestTier + "]", guestIdDoc) << "\n";
        txt << formatReceiptRow80("Accommodation: " + roomDesc, "Phone: " + guestPhone) << "\n";
        if (booking != nullptr) {
            txt << formatReceiptRow80(
                       "Stay: " + DateUtil::formatDate(booking->checkInDay) + " to " + DateUtil::formatDate(booking->checkOutDay),
                       "Nights: " + std::to_string(invoice.nights)
                   )
                << "\n";
        }
        txt << borderMid << "\n";
        txt << formatReceiptRow80("DESCRIPTION", "AMOUNT (PKR)") << "\n";
        txt << borderMid << "\n";
        txt << formatReceiptRow80(
                   "Room Charges (" + std::to_string(invoice.nights) + " nights)",
                   formatPKR(invoice.roomSubtotal)
               )
            << "\n";

        if (booking != nullptr) {
            for (const AddOn& a : booking->addOns) {
                txt << formatReceiptRow80(
                           "  + " + a.description + " (x" + std::to_string(a.quantity) + ")",
                           formatPKR(a.unitPrice * a.quantity)
                       )
                    << "\n";
            }
        }
        txt << borderMid << "\n";
        txt << formatReceiptRow80("Room Subtotal", formatPKR(invoice.roomSubtotal)) << "\n";
        txt << formatReceiptRow80("Add-Ons & Fees Subtotal", formatPKR(invoice.addOnSubtotal)) << "\n";
        if (invoice.loyaltyDiscount > 0.0) {
            txt << formatReceiptRow80("Loyalty Discount (" + guestTier + ")", "-" + formatPKR(invoice.loyaltyDiscount)) << "\n";
        }
        if (invoice.promoDiscount > 0.0) {
            const std::string pLabel = (booking && !booking->promoCode.empty())
                                           ? ("Promo Discount (" + booking->promoCode + ")")
                                           : "Promo Discount";
            txt << formatReceiptRow80(pLabel, "-" + formatPKR(invoice.promoDiscount)) << "\n";
        }
        txt << formatReceiptRow80("Taxable Amount", formatPKR(invoice.taxableAmount)) << "\n";
        txt << formatReceiptRow80(
                   "GST / Hotel Tax (" + std::to_string(static_cast<int>(std::llround(invoice.taxRate * 100.0))) + "%)",
                   formatPKR(invoice.taxAmount)
               )
            << "\n";
        txt << borderTop << "\n";
        txt << formatReceiptRow80("GRAND TOTAL DUE", formatPKR(invoice.grandTotal)) << "\n";
        txt << formatReceiptRow80(
                   "Payment Status: " + std::string(invoice.paid ? "PAID" : "UNPAID"),
                   "Method: " + invoice.paymentMethod
               )
            << "\n";
        txt << borderTop << "\n";
        txt << formatReceiptCenter80("Thank you for staying with NUST SEECS Grand Hotel!") << "\n";
        txt << borderTop << "\n";
    }

    // 2. Write self-contained print-ready HTML invoice with inline CSS and decorative QR block
    {
        std::ofstream html(htmlPath, std::ios::trunc);
        if (!html.is_open()) {
            return false;
        }
        const std::string qrSeed = sha256Hex(invoice.number + "|" + guestName + "|" + formatPKR(invoice.grandTotal));

        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n";
        html << "<title>Invoice " << htmlEscape(invoice.number) << " — NUST SEECS HMS</title>\n";
        html << "<style>\n"
                "body{font-family:'Segoe UI',Arial,sans-serif;background:#F4F6FA;color:#1B2233;margin:0;padding:32px;}\n"
                ".sheet{max-width:820px;margin:0 auto;background:#FFFFFF;border:1px solid #D5DAE6;border-radius:12px;padding:36px;box-shadow:0 12px 32px rgba(11,14,20,0.08);}\n"
                ".header{display:flex;justify-content:space-between;align-items:flex-start;border-bottom:2px solid #3B5BDB;padding-bottom:20px;margin-bottom:24px;}\n"
                ".brand{display:flex;align-items:center;gap:14px;}\n"
                ".logo{width:48px;height:48px;border-radius:10px;background:#3B5BDB;color:#FFF;font-size:26px;font-weight:700;display:flex;align-items:center;justify-content:center;}\n"
                ".badge{display:inline-block;padding:4px 10px;border-radius:999px;font-size:12px;font-weight:600;background:#EDF0F6;color:#3B5BDB;margin-left:8px;}\n"
                ".paid{background:#D1FAE5;color:#065F46;}.unpaid{background:#FEE2E2;color:#991B1B;}\n"
                ".grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-bottom:24px;background:#F8FAFC;padding:16px;border-radius:8px;border:1px solid #E2E8F0;}\n"
                "table{width:100%;border-collapse:collapse;margin-bottom:24px;}\n"
                "th{background:#EDF0F6;text-align:left;padding:10px 12px;font-size:13px;color:#475569;border-bottom:1px solid #CBD5E1;}\n"
                "td{padding:10px 12px;border-bottom:1px solid #E2E8F0;font-size:14px;}\n"
                ".num{text-align:right;font-variant-numeric:tabular-nums;}\n"
                ".footer-flex{display:flex;justify-content:space-between;align-items:flex-end;margin-top:20px;}\n"
                ".totals{width:340px;}\n"
                ".totals-row{display:flex;justify-content:space-between;padding:6px 0;font-size:14px;}\n"
                ".grand{border-top:2px solid #1B2233;margin-top:8px;padding-top:10px;font-size:18px;font-weight:700;color:#3B5BDB;}\n"
                ".qr{display:grid;grid-template-columns:repeat(7,8px);gap:2px;padding:8px;background:#FFF;border:1px solid #CBD5E1;border-radius:6px;width:fit-content;}\n"
                ".qr-on{width:8px;height:8px;background:#1B2233;}.qr-off{width:8px;height:8px;background:#F1F5F9;}\n"
                "@media print{body{background:#FFF;padding:0;}.sheet{box-shadow:none;border:none;}}\n"
                "</style>\n</head>\n<body>\n<div class=\"sheet\">\n";

        html << "<div class=\"header\">\n"
                "  <div class=\"brand\"><div class=\"logo\">H</div><div>"
                "<h1 style=\"margin:0;font-size:22px;\">NUST SEECS Grand Hotel</h1>"
                "<div style=\"color:#66708A;font-size:13px;\">Sector H-12, Islamabad &middot; Official Tax Folio</div>"
                "</div></div>\n"
                "  <div style=\"text-align:right;\">"
                "<div style=\"font-size:20px;font-weight:700;\">" << htmlEscape(invoice.number) << "</div>"
                "<div style=\"color:#66708A;font-size:13px;\">Issued: " << htmlEscape(DateUtil::formatDate(invoice.issuedDay)) << "</div>"
                "<span class=\"badge " << (invoice.paid ? "paid" : "unpaid") << "\">"
             << (invoice.paid ? "PAID" : "UNPAID") << "</span>"
                "</div>\n</div>\n";

        html << "<div class=\"grid\">\n"
                "  <div><strong>Billed To</strong><br>" << htmlEscape(guestName)
             << "<span class=\"badge\">" << htmlEscape(guestTier) << "</span><br>"
             << "<span style=\"color:#66708A;font-size:13px;\">" << htmlEscape(guestIdDoc) << " &middot; "
             << htmlEscape(guestPhone) << "</span></div>\n"
                "  <div style=\"text-align:right;\"><strong>Stay Details</strong><br>" << htmlEscape(roomDesc) << "<br>";
        if (booking != nullptr) {
            html << "<span style=\"color:#66708A;font-size:13px;\">"
                 << htmlEscape(DateUtil::formatDate(booking->checkInDay)) << " &rarr; "
                 << htmlEscape(DateUtil::formatDate(booking->checkOutDay)) << " (" << invoice.nights << " nights)</span>";
        }
        html << "</div>\n</div>\n";

        html << "<table><thead><tr><th>Item Description</th><th class=\"num\">Qty</th><th class=\"num\">Amount</th></tr></thead><tbody>\n";
        html << "<tr><td>Room Accommodation (" << htmlEscape(roomDesc) << ")</td><td class=\"num\">"
             << invoice.nights << "</td><td class=\"num\">" << htmlEscape(formatPKR(invoice.roomSubtotal)) << "</td></tr>\n";
        if (booking != nullptr) {
            for (const AddOn& a : booking->addOns) {
                html << "<tr><td>" << htmlEscape(a.description) << " (" << htmlEscape(toString(a.type))
                     << ")</td><td class=\"num\">" << a.quantity << "</td><td class=\"num\">"
                     << htmlEscape(formatPKR(a.unitPrice * a.quantity)) << "</td></tr>\n";
            }
        }
        html << "</tbody></table>\n";

        html << "<div class=\"footer-flex\">\n  <div><div class=\"qr\">";
        for (int cell = 0; cell < 49; ++cell) {
            const char hexChar = qrSeed[static_cast<std::size_t>(cell % 64)];
            const bool on = (hexChar >= '0' && hexChar <= '7') || (cell == 0 || cell == 6 || cell == 42 || cell == 48);
            html << "<div class=\"" << (on ? "qr-on" : "qr-off") << "\"></div>";
        }
        html << "</div><div style=\"font-size:11px;color:#66708A;margin-top:6px;\">Verify Hash: "
             << qrSeed.substr(0U, 16U) << "</div></div>\n";

        html << "  <div class=\"totals\">\n"
                "    <div class=\"totals-row\"><span>Room Subtotal</span><span>" << htmlEscape(formatPKR(invoice.roomSubtotal)) << "</span></div>\n"
                "    <div class=\"totals-row\"><span>Add-Ons &amp; Fees</span><span>" << htmlEscape(formatPKR(invoice.addOnSubtotal)) << "</span></div>\n"
                "    <div class=\"totals-row\"><span>Loyalty Discount</span><span>-" << htmlEscape(formatPKR(invoice.loyaltyDiscount)) << "</span></div>\n"
                "    <div class=\"totals-row\"><span>Promo Discount</span><span>-" << htmlEscape(formatPKR(invoice.promoDiscount)) << "</span></div>\n"
                "    <div class=\"totals-row\"><span>GST (" << static_cast<int>(std::llround(invoice.taxRate * 100.0)) << "%)</span><span>"
             << htmlEscape(formatPKR(invoice.taxAmount)) << "</span></div>\n"
                "    <div class=\"totals-row grand\"><span>Grand Total</span><span>" << htmlEscape(formatPKR(invoice.grandTotal)) << "</span></div>\n"
                "    <div style=\"text-align:right;font-size:12px;color:#66708A;margin-top:4px;\">Payment Method: "
             << htmlEscape(invoice.paymentMethod) << "</div>\n"
                "  </div>\n</div>\n</div>\n</body>\n</html>\n";
    }

    if (outTxtPath != nullptr) {
        *outTxtPath = txtPath;
    }
    if (outHtmlPath != nullptr) {
        *outHtmlPath = htmlPath;
    }
    return true;
}

// =============================================================================
// 8. YIELD PRICING, HOUSEKEEPING HEAP & SERVICE ORDERS
// =============================================================================
/*
 * Purpose: Counts rooms occupied or reserved on a given dayNumber.
 * Inputs: dayNumber.
 * Outputs: Integer count of occupied/reserved rooms in [0..50].
 * Big-O: O(B + R) time, O(1) space.
 */
int HotelSystem::countOccupiedOrReservedForDay(int32_t dayNumber) const {
    const int32_t today = DateUtil::todayDayNumber();
    if (dayNumber == today) {
        int count = 0;
        for (const Room& r : m_db.rooms) {
            if (r.status == RoomStatus::Occupied || r.status == RoomStatus::Reserved) {
                ++count;
            }
        }
        return count;
    }

    int count = 0;
    for (const Room& r : m_db.rooms) {
        for (const Booking& b : m_db.bookings) {
            if (b.roomId == r.id &&
                (b.status == BookingStatus::Reserved || b.status == BookingStatus::CheckedIn) &&
                dayNumber >= b.checkInDay && dayNumber < b.checkOutDay) {
                ++count;
                break;
            }
        }
    }
    return count;
}

/*
 * Purpose: Computes Dynamic Yield Management pricing for today's date.
 * Inputs: None.
 * Outputs: YieldPricingResult struct.
 * Big-O: O(R) time, O(1) space.
 */
YieldPricingResult HotelSystem::getTodayYieldPricing() const {
    return getYieldPricingForDay(DateUtil::todayDayNumber());
}

/*
 * Purpose: Computes Dynamic Yield Management pricing for a specific dayNumber.
 * Inputs: dayNumber.
 * Outputs: YieldPricingResult struct.
 * Big-O: O(B + R) time, O(1) space.
 */
YieldPricingResult HotelSystem::getYieldPricingForDay(int32_t dayNumber) const {
    const int occupied = countOccupiedOrReservedForDay(dayNumber);
    const int totalRooms = std::max(1, static_cast<int>(m_db.rooms.size()));
    return computeYieldPricing(
        occupied,
        totalRooms,
        DateUtil::isPeakSeason(dayNumber),
        DateUtil::isWeekendNight(dayNumber),
        m_db.settings.surgeEnabled,
        m_db.settings.manualSurgeOverride
    );
}

/*
 * Purpose: Applies an Admin manual surge multiplier override (0.0 clears override) with mandatory reason audit-logging.
 * Inputs: multiplier (0.0 or [1.0..1.6]), reason text, optional outError.
 * Outputs: true on success, false on RBAC or validation failure.
 * Big-O: O(1) time, O(1) space.
 */
bool HotelSystem::setManualSurgeOverride(double multiplier, const std::string& reason, std::string* outError) {
    if (!checkPermission(Action::OverridePricing, "Manual Price Override")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }
    if (multiplier != 0.0 && (multiplier < 1.0 || multiplier > Cfg::SURGE_CAP)) {
        if (outError != nullptr) { *outError = "Override multiplier must be 0.0 (auto) or between 1.00 and 1.60."; }
        return false;
    }
    if (multiplier > 0.0 && reason.size() < 3U) {
        if (outError != nullptr) { *outError = "A reason (>=3 chars) is required for manual price overrides."; }
        return false;
    }

    m_db.settings.manualSurgeOverride = multiplier;
    logAudit(
        "PRICE_OVERRIDE",
        (multiplier == 0.0)
            ? ("Cleared manual surge override (" + reason + ")")
            : ("Set manual surge multiplier to " + std::to_string(multiplier) + "x — Reason: " + reason)
    );
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Warning, "Pricing Override", "Surge multiplier updated and audit-logged.");
    return true;
}

/*
 * Purpose: Toggles dynamic surge pricing on or off (Admin only).
 * Inputs: enabled bool.
 * Outputs: true on success.
 * Big-O: O(1) time, O(1) space.
 */
bool HotelSystem::setSurgeEnabled(bool enabled) {
    if (!checkPermission(Action::ManageSettings, "Toggle Dynamic Surge")) {
        return false;
    }
    m_db.settings.surgeEnabled = enabled;
    logAudit("SETTINGS_UPDATE", std::string("Dynamic surge ") + (enabled ? "ENABLED" : "DISABLED"));
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    return true;
}

/*
 * Purpose: Updates the configurable hotel tax rate (Admin only, clamped 0.0 .. 0.50).
 * Inputs: newRate fraction (e.g. 0.16).
 * Outputs: true on success.
 * Big-O: O(1) time, O(1) space.
 */
bool HotelSystem::setTaxRate(double newRate) {
    if (!checkPermission(Action::ManageSettings, "Update Tax Rate")) {
        return false;
    }
    m_db.settings.taxRate = std::clamp(newRate, 0.0, 0.50);
    logAudit("SETTINGS_UPDATE", "Updated GST tax rate to " + std::to_string(m_db.settings.taxRate * 100.0) + "%");
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    return true;
}

/*
 * Purpose: Creates a new Cleaning or Maintenance task, computes its Section 9.9 priority score P,
 *          updates the room status accordingly, and pushes the task into the custom TaskMinHeap.
 * Inputs: roomId, type, severity (1..5 for Maintenance), assignedTo, note, optional outError.
 * Outputs: New taskId (>0), or -1 on error.
 * Big-O: O(log T) heap push + O(B) upcoming check-in scan.
 */
int HotelSystem::dispatchHousekeepingTask(
    int roomId,
    TaskType type,
    int severity,
    const std::string& assignedTo,
    const std::string& note,
    std::string* outError
) {
    if (!checkPermission(Action::ManageHousekeeping, "Dispatch Housekeeping / Maintenance")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return -1;
    }

    Room* room = findRoomById(roomId);
    if (room == nullptr) {
        if (outError != nullptr) { *outError = "Room not found."; }
        return -1;
    }

    const int32_t today = DateUtil::todayDayNumber();
    const int32_t nowMin = DateUtil::currentMinuteOfDay();

    HousekeepingTask t{};
    t.id            = m_db.settings.nextTaskId++;
    t.roomId        = roomId;
    t.type          = type;
    t.status        = TaskStatus::Queued;
    t.severity      = (type == TaskType::Maintenance) ? std::clamp(severity, 1, 5) : 0;
    t.createdDay    = today;
    t.createdMinute = nowMin;
    t.assignedTo    = assignedTo.empty() ? "Housekeeping Team" : assignedTo;
    t.note          = note;

    if (type == TaskType::Maintenance) {
        room->status = RoomStatus::Maintenance;
    } else if (room->status != RoomStatus::Occupied) {
        room->status = RoomStatus::CleaningRequired;
    }

    m_db.tasks.push_back(t);
    recomputeAllTaskPriorities();

    logAudit(
        "TASK_DISPATCH",
        std::string(toString(type)) + " task #" + std::to_string(t.id) + " dispatched for Room " +
            std::to_string(room->number) + (note.empty() ? "" : (" — " + note))
    );
    m_fileManager.saveTasks(m_db.tasks);
    m_fileManager.saveRooms(m_db.rooms);
    m_fileManager.saveSettings(m_db.settings);
    m_fileManager.saveAuditLog(m_db.auditLog);
    return t.id;
}

/*
 * Purpose: Marks a queued housekeeping task as InProgress.
 * Inputs: taskId.
 * Outputs: true on success.
 * Big-O: O(T log T) to refresh heap.
 */
bool HotelSystem::startHousekeepingTask(int taskId) {
    if (!checkPermission(Action::ManageHousekeeping, "Start Housekeeping Task")) {
        return false;
    }
    for (HousekeepingTask& t : m_db.tasks) {
        if (t.id == taskId) {
            t.status = TaskStatus::InProgress;
            recomputeAllTaskPriorities();
            m_fileManager.saveTasks(m_db.tasks);
            pushNotice(NoticeType::Info, "Task Started", "Task #" + std::to_string(taskId) + " is now In Progress.");
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Marks a housekeeping/maintenance task Done, removes it from TaskMinHeap, sets the room
 *          to Available (if no other open tasks exist for that room), checks waitlist matches, and pushes a toast.
 * Inputs: taskId.
 * Outputs: true on success.
 * Big-O: O(T + W) time.
 */
bool HotelSystem::completeHousekeepingTask(int taskId) {
    if (!checkPermission(Action::ManageHousekeeping, "Complete Housekeeping Task")) {
        return false;
    }
    for (HousekeepingTask& t : m_db.tasks) {
        if (t.id == taskId) {
            t.status = TaskStatus::Done;
            m_taskHeap.removeById(taskId);

            bool hasOtherOpenTask = false;
            for (const HousekeepingTask& other : m_db.tasks) {
                if (other.roomId == t.roomId && other.id != t.id && other.status != TaskStatus::Done) {
                    hasOtherOpenTask = true;
                    break;
                }
            }

            Room* room = findRoomById(t.roomId);
            if (room != nullptr && !hasOtherOpenTask && room->status != RoomStatus::Occupied) {
                room->status = RoomStatus::Available;
                logAudit("ROOM_STATUS", "Room " + std::to_string(room->number) + " marked Available after task #" + std::to_string(taskId));
                pushNotice(NoticeType::Success, "Room Ready", "Room " + std::to_string(room->number) + " is now Available.");
                checkWaitlistMatchForRoom(room->id);
            }

            m_fileManager.saveTasks(m_db.tasks);
            m_fileManager.saveRooms(m_db.rooms);
            m_fileManager.saveAuditLog(m_db.auditLog);
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Reassigns a housekeeping/maintenance task to another staff member.
 * Inputs: taskId, newAssignee.
 * Outputs: true on success.
 * Big-O: O(T log T) to refresh heap.
 */
bool HotelSystem::reassignHousekeepingTask(int taskId, const std::string& newAssignee) {
    if (!checkPermission(Action::ManageHousekeeping, "Reassign Housekeeping Task")) {
        return false;
    }
    for (HousekeepingTask& t : m_db.tasks) {
        if (t.id == taskId) {
            t.assignedTo = newAssignee;
            recomputeAllTaskPriorities();
            m_fileManager.saveTasks(m_db.tasks);
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Recomputes priority score P = 3U + V + E + 2*sev for every open housekeeping task
 *          and rebuilds the custom TaskMinHeap (called on init, task mutations, and every 30s).
 * Inputs: None.
 * Outputs: Updates task.priorityScore in m_db.tasks and rebuilds m_taskHeap.
 * Big-O: O(T * (B + log T)) time, O(T) space.
 */
void HotelSystem::recomputeAllTaskPriorities() {
    m_taskHeap.clear();
    const int32_t today = DateUtil::todayDayNumber();
    const int32_t nowMin = DateUtil::currentMinuteOfDay();
    const long long nowTotalMin = static_cast<long long>(today) * 1440LL + static_cast<long long>(nowMin);

    for (HousekeepingTask& t : m_db.tasks) {
        if (t.status == TaskStatus::Done) {
            continue;
        }

        // Find nearest upcoming Reserved check-in for this room
        bool hasUpcoming = false;
        double minHoursUntil = 1e9;
        LoyaltyTier arrivingTier = LoyaltyTier::Standard;

        for (const Booking& b : m_db.bookings) {
            if (b.roomId == t.roomId && b.status == BookingStatus::Reserved) {
                const long long checkInTotalMin =
                    static_cast<long long>(b.checkInDay) * 1440LL + static_cast<long long>(b.checkInMinute);
                const double diffHours = static_cast<double>(checkInTotalMin - nowTotalMin) / 60.0;
                if (diffHours >= -1.0 && diffHours < minHoursUntil) {
                    minHoursUntil = std::max(0.0, diffHours);
                    hasUpcoming = true;
                    const Guest* g = findGuestById(b.guestId);
                    arrivingTier = (g != nullptr) ? g->tier : LoyaltyTier::Standard;
                }
            }
        }

        double hoursSinceCheckout = 0.0;
        if (const Room* r = findRoomById(t.roomId)) {
            const long long checkoutTotalMin =
                static_cast<long long>(r->lastCheckoutDay) * 1440LL + static_cast<long long>(r->lastCheckoutMinute);
            hoursSinceCheckout = std::max(0.0, static_cast<double>(nowTotalMin - checkoutTotalMin) / 60.0);
        }

        t.priorityScore = TaskMinHeap::computePriorityScore(
            t.type,
            t.severity,
            minHoursUntil,
            hasUpcoming,
            arrivingTier,
            hoursSinceCheckout
        );
        m_taskHeap.push(t);
    }
}

/*
 * Purpose: Returns all open housekeeping tasks in exact min-heap pop order (highest P first).
 * Inputs: None.
 * Outputs: Vector of HousekeepingTask ordered by dispatch priority.
 * Big-O: O(T log T) time, O(T) space.
 */
std::vector<HousekeepingTask> HotelSystem::getHousekeepingDispatchOrder() const {
    return m_taskHeap.getTasksInDispatchOrder();
}

/*
 * Purpose: Creates a new ServiceOrder card in stage 0 (Ordered) for the Kanban board.
 * Inputs: bookingId, type, item, qty, price.
 * Outputs: New orderId (>0), or -1 on error.
 * Big-O: O(1) time, O(1) space.
 */
int HotelSystem::createServiceOrder(int bookingId, AddOnType type, const std::string& item, int qty, double price) {
    if (!checkPermission(Action::ManageServiceOrders, "Create Service Order")) {
        return -1;
    }
    if (findBookingById(bookingId) == nullptr || item.empty() || qty <= 0 || price < 0.0) {
        return -1;
    }

    ServiceOrder o{};
    o.id        = m_db.settings.nextOrderId++;
    o.bookingId = bookingId;
    o.type      = type;
    o.item      = item;
    o.qty       = qty;
    o.price     = price;
    o.stage     = 0;

    m_db.orders.push_back(o);
    m_fileManager.saveOrders(m_db.orders);
    m_fileManager.saveSettings(m_db.settings);
    return o.id;
}

/*
 * Purpose: Moves a ServiceOrder Kanban card across stages (0 Ordered, 1 Preparing, 2 Delivered).
 *          Moving to stage 2 (Delivered) automatically posts the charge to the guest's booking folio (Section 9.10 #6).
 * Inputs: orderId, newStage (0..2).
 * Outputs: true on success, false otherwise.
 * Big-O: O(O + B) time, O(1) space.
 */
bool HotelSystem::moveServiceOrderStage(int orderId, int newStage) {
    if (!checkPermission(Action::ManageServiceOrders, "Move Service Order Stage")) {
        return false;
    }
    const int clampedStage = std::clamp(newStage, 0, 2);
    for (ServiceOrder& o : m_db.orders) {
        if (o.id == orderId) {
            const int prevStage = o.stage;
            o.stage = clampedStage;
            if (prevStage != 2 && clampedStage == 2) {
                if (Booking* bk = findBookingById(o.bookingId)) {
                    bk->addOns.push_back(AddOn{
                        o.type,
                        o.item + " (Service Order #" + std::to_string(o.id) + ")",
                        o.price,
                        o.qty,
                        DateUtil::todayDayNumber()
                    });
                    m_fileManager.saveBookings(m_db.bookings);
                    pushNotice(
                        NoticeType::Success,
                        "Order Delivered",
                        o.item + " (" + formatPKR(o.price * o.qty) + ") posted to Booking #" + std::to_string(o.bookingId)
                    );
                }
            }
            m_fileManager.saveOrders(m_db.orders);
            return true;
        }
    }
    return false;
}

// =============================================================================
// 9. UNDO / REDO STACKS (Section 9.10 #2)
// =============================================================================
/*
 * Purpose: Pushes an UndoCommand onto the session undo stack and clears the redo stack.
 * Inputs: cmd (UndoCommand).
 * Outputs: Updates m_undoStack and clears m_redoStack.
 * Big-O: O(1) amortized time, O(1) space.
 */
void HotelSystem::pushUndoCommand(UndoCommand cmd) {
    m_undoStack.push(std::move(cmd));
    while (!m_redoStack.empty()) {
        m_redoStack.pop();
    }
}

/*
 * Purpose: Reverts the top command on the session undo stack and moves it to the redo stack (Ctrl+Z).
 * Inputs: None.
 * Outputs: true if an action was undone, false if undo stack is empty.
 * Big-O: O(1) stack op + cost of cmd.revert().
 */
bool HotelSystem::undo() {
    if (m_undoStack.empty()) {
        return false;
    }
    UndoCommand cmd = std::move(m_undoStack.top());
    m_undoStack.pop();
    if (cmd.revert) {
        cmd.revert();
    }
    const std::string actionName = cmd.name;
    m_redoStack.push(std::move(cmd));
    logAudit("UNDO", "Reverted action: " + actionName);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Info, "Undo", "Reverted: " + actionName);
    return true;
}

/*
 * Purpose: Re-applies the top command on the session redo stack and moves it back to the undo stack (Ctrl+Y).
 * Inputs: None.
 * Outputs: true if an action was redone, false if redo stack is empty.
 * Big-O: O(1) stack op + cost of cmd.apply().
 */
bool HotelSystem::redo() {
    if (m_redoStack.empty()) {
        return false;
    }
    UndoCommand cmd = std::move(m_redoStack.top());
    m_redoStack.pop();
    if (cmd.apply) {
        cmd.apply();
    }
    const std::string actionName = cmd.name;
    m_undoStack.push(std::move(cmd));
    logAudit("REDO", "Re-applied action: " + actionName);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Info, "Redo", "Re-applied: " + actionName);
    return true;
}

/*
 * Purpose: Empties both Undo and Redo stacks (called on login/logout/reset).
 * Inputs: None.
 * Outputs: Empty stacks.
 * Big-O: O(S) where S is stack size.
 */
void HotelSystem::clearUndoHistory() {
    while (!m_undoStack.empty()) {
        m_undoStack.pop();
    }
    while (!m_redoStack.empty()) {
        m_redoStack.pop();
    }
}

// =============================================================================
// 10. AUDIT CHAIN, NIGHT AUDIT & BACKUPS (Section 5, 6, 9.10 #5)
// =============================================================================
/*
 * Purpose: Appends an entry to m_db.auditLog attributed to the currently signed-in user (or system).
 * Inputs: action, details.
 * Outputs: Appends signed AuditEntry to m_db.auditLog.
 * Big-O: O(|action| + |details|) time, O(1) auxiliary space.
 */
void HotelSystem::logAudit(const std::string& action, const std::string& details) {
    const User* u = currentUser();
    const int uid = (u != nullptr) ? u->id : 0;
    const std::string uname = (u != nullptr) ? u->username : "system";
    FileManager::appendAuditEntry(m_db.auditLog, uid, uname, action, details);
}

/*
 * Purpose: Re-verifies every hash link in m_db.auditLog using SHA-256.
 * Inputs: None.
 * Outputs: -1 if verified intact, or 0-based index of first tampered entry.
 * Big-O: O(N * L) time, O(1) space.
 */
int HotelSystem::verifyAuditLogChain() const {
    return verifyAuditChain(m_db.auditLog);
}

/*
 * Purpose: Executes Night Audit (Admin only, Section 9.10 #5):
 *          1. Rolls overdue Reserved bookings (checkInDay < today) to NoShow with cancellation fee
 *          2. Tallies active room revenue, check-ins, check-outs, and unpaid invoices
 *          3. Writes/updates today's DailyStat record
 *          4. Exports exports/NightAudit_<YYYY-MM-DD>.txt
 *          5. Takes a timestamped backup snapshot in backups/
 * Inputs: optional outReportPath, optional outSummary.
 * Outputs: true on success, false on RBAC or I/O error.
 * Big-O: O(B + I + R) time, O(1) space.
 */
bool HotelSystem::runNightAudit(std::string* outReportPath, std::string* outSummary) {
    if (!checkPermission(Action::RunNightAudit, "Run Night Audit")) {
        return false;
    }

    const int32_t today = DateUtil::todayDayNumber();
    const std::string todayStr = DateUtil::formatDate(today);

    int rolledNoShows = 0;
    std::vector<int> overdueIds;
    for (const Booking& b : m_db.bookings) {
        if (b.status == BookingStatus::Reserved && b.checkInDay < today) {
            overdueIds.push_back(b.id);
        }
    }
    for (int bid : overdueIds) {
        if (cancelBooking(bid, true)) {
            ++rolledNoShows;
        }
    }

    int occupiedRooms = 0;
    double projectedRoomCharges = 0.0;
    for (const Booking& b : m_db.bookings) {
        if (b.status == BookingStatus::CheckedIn) {
            ++occupiedRooms;
            projectedRoomCharges += roundToNearest10PKR(b.quotedRate * b.surgeMultiplier);
        }
    }

    double invoicedToday = 0.0;
    int unpaidInvoicesCount = 0;
    double unpaidInvoicesTotal = 0.0;
    for (const Invoice& inv : m_db.invoices) {
        if (inv.issuedDay == today) {
            invoicedToday += inv.grandTotal;
        }
        if (!inv.paid) {
            ++unpaidInvoicesCount;
            unpaidInvoicesTotal += inv.grandTotal;
        }
    }

    int todayCheckins = 0;
    int todayCheckouts = 0;
    for (const Booking& b : m_db.bookings) {
        if (b.checkInDay == today && (b.status == BookingStatus::CheckedIn || b.status == BookingStatus::CheckedOut)) {
            ++todayCheckins;
        }
        if (b.checkOutDay == today && b.status == BookingStatus::CheckedOut) {
            ++todayCheckouts;
        }
    }

    const double occPct = (100.0 * static_cast<double>(occupiedRooms)) /
                          static_cast<double>(std::max<std::size_t>(1U, m_db.rooms.size()));
    const double totalDayRevenue = invoicedToday + projectedRoomCharges;

    DailyStat stat{};
    stat.day          = today;
    stat.revenue      = totalDayRevenue;
    stat.occupancyPct = occPct;
    stat.checkins     = todayCheckins;
    stat.checkouts    = todayCheckouts;

    bool replaced = false;
    for (DailyStat& existing : m_db.stats) {
        if (existing.day == today) {
            existing = stat;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_db.stats.push_back(stat);
    }

    m_fileManager.ensureDirectories();
    const std::string reportPath =
        (std::filesystem::path(m_fileManager.getExportDir()) / ("NightAudit_" + todayStr + ".txt")).string();
    {
        std::ofstream out(reportPath, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << "================================================================================\n";
        out << "NUST SEECS GRAND HOTEL — NIGHT AUDIT REPORT (" << todayStr << ")\n";
        out << "================================================================================\n";
        out << "Occupied Rooms          : " << occupiedRooms << " / " << m_db.rooms.size()
            << " (" << std::fixed << std::setprecision(1) << occPct << "%)\n";
        out << "Check-Ins Today         : " << todayCheckins << "\n";
        out << "Check-Outs Today        : " << todayCheckouts << "\n";
        out << "No-Shows Rolled         : " << rolledNoShows << "\n";
        out << "Room Charges Posted     : " << formatPKR(projectedRoomCharges) << "\n";
        out << "Invoices Issued Today   : " << formatPKR(invoicedToday) << "\n";
        out << "Total Daily Revenue     : " << formatPKR(totalDayRevenue) << "\n";
        out << "Unpaid Invoices Flagged : " << unpaidInvoicesCount << " (" << formatPKR(unpaidInvoicesTotal) << ")\n";
        out << "Audit Chain Integrity   : " << (verifyAuditLogChain() == -1 ? "VERIFIED" : "TAMPERED") << "\n";
        out << "================================================================================\n";
    }

    logAudit("NIGHT_AUDIT", "Closed business day " + todayStr + " — Revenue: " + formatPKR(totalDayRevenue));
    saveAll();

    std::string snapName;
    m_fileManager.createBackupSnapshot(&snapName);

    const std::string summary =
        "Night Audit " + todayStr + " complete: " + std::to_string(rolledNoShows) + " no-shows rolled, " +
        std::to_string(unpaidInvoicesCount) + " unpaid invoices flagged, backup " + snapName + " created.";

    if (outReportPath != nullptr) { *outReportPath = reportPath; }
    if (outSummary != nullptr) { *outSummary = summary; }
    pushNotice(NoticeType::Success, "Night Audit Complete", summary);
    return true;
}

/*
 * Purpose: Creates an on-demand snapshot backup (Admin only).
 * Inputs: optional outSnapshotName.
 * Outputs: true on success.
 * Big-O: O(B) where B is total .dat file size.
 */
bool HotelSystem::createBackupNow(std::string* outSnapshotName) {
    if (!checkPermission(Action::ManageBackups, "Create Backup Snapshot")) {
        return false;
    }
    saveAll();
    std::string name;
    if (!m_fileManager.createBackupSnapshot(&name)) {
        return false;
    }
    if (outSnapshotName != nullptr) {
        *outSnapshotName = name;
    }
    logAudit("BACKUP_CREATE", "Created manual backup snapshot: " + name);
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Success, "Backup Created", "Snapshot " + name + " saved.");
    return true;
}

/*
 * Purpose: Lists available backup snapshots sorted newest-first.
 * Inputs: None.
 * Outputs: Vector of snapshot directory names.
 * Big-O: O(S log S).
 */
std::vector<std::string> HotelSystem::listBackups() const {
    return m_fileManager.listBackupSnapshots();
}

/*
 * Purpose: Restores all .dat files from a chosen backup snapshot and reloads in-memory state (Admin only).
 * Inputs: snapshotName, optional outError.
 * Outputs: true on success.
 * Big-O: O(Total Records).
 */
bool HotelSystem::restoreBackup(const std::string& snapshotName, std::string* outError) {
    if (!checkPermission(Action::ManageBackups, "Restore Backup Snapshot")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }
    if (!m_fileManager.restoreFromSnapshot(snapshotName, outError)) {
        return false;
    }
    std::vector<std::string> alerts;
    m_fileManager.loadAll(m_db, alerts);
    rebuildRuntimeIndexes();
    clearUndoHistory();
    logAudit("BACKUP_RESTORE", "Restored database from backup snapshot: " + snapshotName);
    m_lastAuditTamperIndex = verifyAuditLogChain();
    m_fileManager.saveAuditLog(m_db.auditLog);
    pushNotice(NoticeType::Warning, "Backup Restored", "Database restored from " + snapshotName);
    return true;
}

/*
 * Purpose: Resets the database to a clean state (50 default rooms + 3 default staff users) (Admin only).
 * Inputs: optional outError.
 * Outputs: true on success.
 * Big-O: O(1) time.
 */
bool HotelSystem::resetCleanState(std::string* outError) {
    if (!checkPermission(Action::UseVivaTools, "Reset Clean State")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }
    const User* u = currentUser();
    const int uid = (u != nullptr) ? u->id : 1;
    const std::string uname = (u != nullptr) ? u->username : "admin";
    const bool ok = m_fileManager.resetCleanState(m_db, uid, uname);
    rebuildRuntimeIndexes();
    clearUndoHistory();
    m_lastAuditTamperIndex = verifyAuditLogChain();
    if (ok) {
        pushNotice(NoticeType::Warning, "Clean State Reset", "Wiped data/ and restored 50 default rooms + 3 users.");
    }
    return ok;
}

/*
 * Purpose: Seeds the full Section 10 Viva Demo dataset (Admin only, auto-backups first),
 *          rebuilds runtime indexes (CNIC hash table, TaskMinHeap, FIFO waitlist), and verifies the audit chain.
 * Inputs: optional outError.
 * Outputs: true on success, false on RBAC or I/O error.
 * Big-O: O(G + B + T log T + A).
 */
bool HotelSystem::seedVivaDemoData(std::string* outError) {
    if (!checkPermission(Action::UseVivaTools, "Seed Viva Demo Data")) {
        if (outError != nullptr) { *outError = "Permission denied"; }
        return false;
    }
    const User* u = currentUser();
    const int uid = (u != nullptr) ? u->id : 1;
    const std::string uname = (u != nullptr) ? u->username : "admin";
    const bool ok = m_fileManager.seedVivaData(m_db, uid, uname);
    rebuildRuntimeIndexes();
    clearUndoHistory();
    m_lastAuditTamperIndex = verifyAuditLogChain();
    if (ok) {
        updateLastSavedClock();
        pushNotice(
            NoticeType::Success,
            "Viva Demo Data Seeded",
            "Loaded 50 rooms, 35 guests, 30-day history (76% occ, 1.10x surge) & 66 chained audit entries."
        );
    }
    return ok;
}

// =============================================================================
// 11. NOTIFICATIONS / TOAST QUEUE
// =============================================================================
/*
 * Purpose: Queues a notification for the GUI Toast manager and appends it to the notification bell history.
 * Inputs: type, title, message, relatedWaitlistId, relatedRoomId.
 * Outputs: Appends to m_pendingNotices and m_notificationHistory.
 * Big-O: O(1) amortized time, O(1) space.
 */
void HotelSystem::pushNotice(
    NoticeType type,
    const std::string& title,
    const std::string& message,
    int relatedWaitlistId,
    int relatedRoomId
) {
    SystemNotice n{type, title, message, relatedWaitlistId, relatedRoomId};
    m_pendingNotices.push_back(n);
    if (m_notificationHistory.size() >= 50U) {
        m_notificationHistory.erase(m_notificationHistory.begin());
    }
    m_notificationHistory.push_back(n);
    ++m_unreadNotifications;
}

/*
 * Purpose: Moves all pending SystemNotice items to the caller (typically GuiRenderer::updateToasts).
 * Inputs: None.
 * Outputs: Vector of drained SystemNotice items.
 * Big-O: O(N) move, O(1) extra space.
 */
std::vector<SystemNotice> HotelSystem::drainNotices() {
    std::vector<SystemNotice> out = std::move(m_pendingNotices);
    m_pendingNotices.clear();
    return out;
}

// =============================================================================
// 12. PHASE 3 END-TO-END SELF-TESTS
// =============================================================================
/*
 * Purpose: Exercises HotelSystem end-to-end in an isolated temporary directory to verify:
 *          1. RBAC permissions & 3-strike 30s lockout
 *          2. Guest CRM validation, duplicate-CNIC rejection & loyalty promotion state machine
 *          3. Conflict-free booking engine, room swap, Undo/Redo stacks, and FIFO Waitlist auto-match
 *          4. Checkout billing, folio math, TXT (80-col) & HTML invoice export, and Night Audit
 * Inputs: verboseConsole (prints PASS/FAIL lines if true).
 * Outputs: Vector of SelfTestEntry results.
 * Big-O: O(1) bounded integration test suite.
 */
std::vector<SelfTestEntry> HotelSystem::runPhase3SelfTests(bool verboseConsole) {
    std::vector<SelfTestEntry> results;
    auto recordCheck = [&](const std::string& name, bool passed, const std::string& details) {
        results.push_back(SelfTestEntry{name, passed, details});
        if (verboseConsole) {
            std::printf("[%s] %-42s | %s\n", passed ? "PASS" : "FAIL", name.c_str(), details.c_str());
        }
    };

    const std::string tData   = "data/_phase3_test_data";
    const std::string tBackup = "backups/_phase3_test_backups";
    const std::string tExport = "exports/_phase3_test_exports";
    std::error_code ec;
    std::filesystem::remove_all(tData, ec);
    std::filesystem::remove_all(tBackup, ec);
    std::filesystem::remove_all(tExport, ec);

    HotelSystem sys(tData, tBackup, tExport);
    sys.initialize();

    // Test 1: RBAC Matrix & 3-Strike 30s Lockout
    {
        const LoginResult bad1 = sys.login("admin", "wrong1");
        const LoginResult bad2 = sys.login("admin", "wrong2");
        const LoginResult bad3 = sys.login("admin", "wrong3");
        const bool lockedAfter3 = (!bad1.success && !bad2.success && bad3.lockedOut && bad3.lockoutRemaining >= 29.0);

        // Advance timer 31s to clear lockout
        sys.tick(31.0);
        const LoginResult hkLogin = sys.login("housekeeping", "Clean@123");
        const bool hkDeniedOverride = !sys.setManualSurgeOverride(1.20, "Test");
        sys.logout();

        const LoginResult adminLogin = sys.login("admin", "Admin@123");
        const bool ok = lockedAfter3 && hkLogin.success && hkDeniedOverride && adminLogin.success;
        recordCheck(
            "RBAC Matrix & 3-Strike 30s Login Lockout",
            ok,
            "lockout=30s, housekeepingDeniedOverride=true, adminLogin=true"
        );
    }

    // Test 2: Guest CRM Registration, Duplicate CNIC Hash Check & Loyalty State Machine
    int guestId = -1;
    {
        Guest g{};
        g.fullName    = "Hamza Tariq";
        g.cnic        = "61101-1234567-9";
        g.phone       = "+92 300 5551234";
        g.email       = "hamza.tariq@seecs.edu.pk";
        g.nationality = "Pakistan";
        g.address     = "H-12 Islamabad";
        g.isForeign   = false;
        g.stayCount   = 4;
        g.lifetimeSpend = 140000.0;

        guestId = sys.registerGuest(g);
        const int dupId = sys.registerGuest(g); // Must fail via custom CNIC hash table
        const Guest* saved = sys.findGuestById(guestId);
        const bool phoneNormalized = (saved != nullptr && saved->phone == "0300-5551234");
        const bool hashLookupOk = (sys.findGuestIdByCnicHash("61101-1234567-9") == guestId);
        const bool binLookupOk = (sys.findGuestByCnicBinarySearch("61101-1234567-9") != nullptr);
        const LoyaltyTier promoted = HotelSystem::evaluateLoyaltyTier(LoyaltyTier::Standard, 5, 140000.0);
        const LoyaltyTier noDemote = HotelSystem::evaluateLoyaltyTier(LoyaltyTier::Gold, 1, 1000.0);

        const bool ok = (guestId > 0 && dupId == -1 && phoneNormalized && hashLookupOk && binLookupOk &&
                         promoted == LoyaltyTier::Silver && noDemote == LoyaltyTier::Gold);
        recordCheck(
            "CRM Registration, CNIC Dup & Loyalty Tier",
            ok,
            "guestId=" + std::to_string(guestId) + ", dupRejected=true, phoneNorm='0300-5551234'"
        );
    }

    // Test 3: Booking Conflict Prevention, Room Swap, Undo/Redo Stacks & Waitlist Auto-Match
    int bookingId = -1;
    {
        const int32_t today = DateUtil::todayDayNumber();
        bookingId = sys.createBooking(guestId, 1, today, today + 3, 2, 0, "WELCOME10", true);
        const int conflictId = sys.createBooking(guestId, 1, today + 1, today + 4, 1, 0, "", false);

        const bool swapOk = sys.swapBookingRoom(bookingId, 2, true);
        const bool swappedTo2 = (sys.findBookingById(bookingId)->roomId == 2);
        const bool undoOk = sys.undo();
        const bool revertedTo1 = (sys.findBookingById(bookingId)->roomId == 1);
        const bool redoOk = sys.redo();
        const bool redoneTo2 = (sys.findBookingById(bookingId)->roomId == 2);

        // Add waitlist entry for Standard room and trigger match on available Room 1
        const int wlId = sys.addToWaitlist(guestId, RoomType::Standard, today + 5, today + 7, 15000.0);
        const int matchedWl = sys.checkWaitlistMatchForRoom(1);

        const bool ok = (bookingId > 0 && conflictId == -1 && swapOk && swappedTo2 &&
                         undoOk && revertedTo1 && redoOk && redoneTo2 && wlId > 0 && matchedWl == wlId);
        recordCheck(
            "Booking Conflict, Undo/Redo & Waitlist",
            ok,
            "conflictBlocked=true, swapUndoRedo=true, waitlistMatched=#" + std::to_string(matchedWl)
        );
    }

    // Test 4: Checkout Folio, Loyalty Promotion, TXT/HTML Export & Night Audit
    {
        sys.addChargeToBooking(bookingId, AddOn{AddOnType::Minibar, "Espresso & Water", 1200.0, 2, DateUtil::todayDayNumber()});
        const CheckOutResult co = sys.checkOutBooking(bookingId, 700, "Card");

        const bool txtExists  = !co.txtExportPath.empty() && std::filesystem::exists(co.txtExportPath, ec);
        const bool htmlExists = !co.htmlExportPath.empty() && std::filesystem::exists(co.htmlExportPath, ec);

        std::string auditReportPath;
        const bool nightAuditOk = sys.runNightAudit(&auditReportPath);
        const bool reportExists = !auditReportPath.empty() && std::filesystem::exists(auditReportPath, ec);
        const bool chainValid   = (sys.verifyAuditLogChain() == -1);

        const bool ok = co.success && co.loyaltyPromoted && co.newTier == LoyaltyTier::Silver &&
                        txtExists && htmlExists && nightAuditOk && reportExists && chainValid;
        recordCheck(
            "Checkout, TXT/HTML Export & Night Audit",
            ok,
            "invoiceId=" + std::to_string(co.invoiceId) + ", promoted=Silver, exports=TXT+HTML, chain=VERIFIED"
        );
    }

    std::filesystem::remove_all(tData, ec);
    std::filesystem::remove_all(tBackup, ec);
    std::filesystem::remove_all(tExport, ec);

    return results;
}

} // namespace hms
