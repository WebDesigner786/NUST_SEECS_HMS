#ifndef HMS_FILE_MANAGER_H
#define HMS_FILE_MANAGER_H

#include "Models.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hms {

// Holds the complete in-memory database state loaded/saved across the 11 .dat files in data/
struct HotelDatabase {
    std::vector<Room>             rooms{};
    std::vector<Guest>            guests{};
    std::vector<Booking>          bookings{};
    std::vector<Invoice>          invoices{};
    std::vector<User>             users{};
    std::vector<AuditEntry>       auditLog{};
    std::vector<HousekeepingTask> tasks{};
    std::vector<WaitlistEntry>    waitlist{};
    std::vector<ServiceOrder>     orders{};
    std::vector<DailyStat>        stats{};
    AppSettings                   settings{};
};

class FileManager {
public:
    explicit FileManager(
        const std::string& dataDir = "data",
        const std::string& backupDir = "backups",
        const std::string& exportDir = "exports"
    );

    // Ensures data/, backups/, exports/ exist on disk
    bool ensureDirectories() const;

    // Crash recovery at startup: checks for leftover .tmp files in data/, removes them, and returns count recovered
    int recoverInterruptedWrites(std::vector<AuditEntry>& auditLog, std::vector<std::string>& outMessages) const;

    // Computes 32-bit FNV-1a checksum over a byte buffer
    static uint32_t computeFnv1a(const uint8_t* data, size_t length);

    // Individual atomic binary save functions (.tmp write -> verify size > 0 -> rename over target)
    bool saveRooms(const std::vector<Room>& rooms) const;
    bool saveGuests(const std::vector<Guest>& guests) const;
    bool saveBookings(const std::vector<Booking>& bookings) const;
    bool saveInvoices(const std::vector<Invoice>& invoices) const;
    bool saveUsers(const std::vector<User>& users) const;
    bool saveAuditLog(const std::vector<AuditEntry>& auditLog) const;
    bool saveTasks(const std::vector<HousekeepingTask>& tasks) const;
    bool saveWaitlist(const std::vector<WaitlistEntry>& waitlist) const;
    bool saveOrders(const std::vector<ServiceOrder>& orders) const;
    bool saveStats(const std::vector<DailyStat>& stats) const;
    bool saveSettings(const AppSettings& settings) const;

    // Saves all 11 .dat files atomically
    bool saveAll(const HotelDatabase& db) const;

    // Individual binary load functions (verifies magic "HMSDATA1", version, and trailing FNV-1a checksum;
    // on corruption, logs error and attempts auto-restore from newest backup)
    bool loadRooms(std::vector<Room>& outRooms, std::string* outError = nullptr) const;
    bool loadGuests(std::vector<Guest>& outGuests, std::string* outError = nullptr) const;
    bool loadBookings(std::vector<Booking>& outBookings, std::string* outError = nullptr) const;
    bool loadInvoices(std::vector<Invoice>& outInvoices, std::string* outError = nullptr) const;
    bool loadUsers(std::vector<User>& outUsers, std::string* outError = nullptr) const;
    bool loadAuditLog(std::vector<AuditEntry>& outAuditLog, std::string* outError = nullptr) const;
    bool loadTasks(std::vector<HousekeepingTask>& outTasks, std::string* outError = nullptr) const;
    bool loadWaitlist(std::vector<WaitlistEntry>& outWaitlist, std::string* outError = nullptr) const;
    bool loadOrders(std::vector<ServiceOrder>& outOrders, std::string* outError = nullptr) const;
    bool loadStats(std::vector<DailyStat>& outStats, std::string* outError = nullptr) const;
    bool loadSettings(AppSettings& outSettings, std::string* outError = nullptr) const;

    // Loads all existing .dat files or initializes clean default state if first launch
    bool loadAll(HotelDatabase& db, std::vector<std::string>& outAlerts) const;

    // Appends a signed AuditEntry to auditLog (and optionally flushes audit_log.dat)
    static AuditEntry appendAuditEntry(
        std::vector<AuditEntry>& auditLog,
        int userId,
        const std::string& username,
        const std::string& action,
        const std::string& details,
        long long explicitTimestamp = 0
    );

    // Snapshot backup management: copies data/*.dat into backups/YYYYMMDD_HHMMSS/ and prunes to newest 10
    bool createBackupSnapshot(std::string* outSnapshotName = nullptr) const;
    std::vector<std::string> listBackupSnapshots() const;
    bool restoreFromSnapshot(const std::string& snapshotName, std::string* outError = nullptr) const;

    // Default generators (50 hotel rooms across 5 floors + 3 default RBAC accounts)
    static std::vector<Room> generateDefaultRooms();
    static std::vector<User> generateDefaultUsers();

    // Wipes data/ and recreates the 50 default rooms + 3 default users (Section 10)
    bool resetCleanState(HotelDatabase& db, int actorUserId = 1, const std::string& actorUsername = "admin") const;

    // Populates rich Viva Demo dataset (50 rooms, 35 diverse guests, 30-day history, active states, >=60 chained audit entries)
    bool seedVivaData(HotelDatabase& db, int actorUserId = 1, const std::string& actorUsername = "admin") const;

    // Phase 1 / Section 11 Self-Test suite for SHA-256, Audit Chain, and Binary Save/Load Round-Trip
    static std::vector<SelfTestEntry> runPhase1SelfTests(bool verboseConsole = true);

    const std::string& getDataDir() const { return m_dataDir; }
    const std::string& getBackupDir() const { return m_backupDir; }
    const std::string& getExportDir() const { return m_exportDir; }

private:
    std::string filePath(const char* filename) const;
    bool writeAtomicEnvelope(const std::string& targetPath, uint32_t recordCount, const std::vector<uint8_t>& payload) const;
    bool readVerifiedEnvelope(
        const std::string& filename,
        uint32_t& outRecordCount,
        std::vector<uint8_t>& outPayload,
        std::string* outError
    ) const;
    bool tryAutoRestoreSingleFile(const std::string& filename) const;
    void pruneOldBackups(size_t maxKeep = 10U) const;

    std::string m_dataDir;
    std::string m_backupDir;
    std::string m_exportDir;
};

} // namespace hms

#endif // HMS_FILE_MANAGER_H
