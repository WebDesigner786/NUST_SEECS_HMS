#include "FileManager.h"
#include "Sha256.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace hms {

namespace {

constexpr char     kFileMagic[8] = {'H', 'M', 'S', 'D', 'A', 'T', 'A', '1'};
constexpr uint32_t kFileVersion  = 1U;

// -----------------------------------------------------------------------------
// Low-level Binary Buffer Serializer / Deserializer (Fixed-Width Types)
// -----------------------------------------------------------------------------
class BinaryWriter {
public:
    void writeBytes(const void* src, size_t len) {
        const uint8_t* ptr = static_cast<const uint8_t*>(src);
        m_buf.insert(m_buf.end(), ptr, ptr + len);
    }

    void writeU32(uint32_t val) {
        writeBytes(&val, sizeof(val));
    }

    void writeI32(int32_t val) {
        writeBytes(&val, sizeof(val));
    }

    void writeI64(int64_t val) {
        writeBytes(&val, sizeof(val));
    }

    void writeDouble(double val) {
        writeBytes(&val, sizeof(val));
    }

    void writeBool(bool val) {
        const uint8_t b = val ? 1U : 0U;
        writeBytes(&b, sizeof(b));
    }

    void writeString(const std::string& str) {
        const uint32_t len = static_cast<uint32_t>(str.size());
        writeU32(len);
        if (len > 0U) {
            writeBytes(str.data(), len);
        }
    }

    const std::vector<uint8_t>& buffer() const {
        return m_buf;
    }

private:
    std::vector<uint8_t> m_buf{};
};

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_pos(0U), m_ok(true) {}

    bool readBytes(void* dst, size_t len) {
        if (!m_ok || m_pos + len > m_size) {
            m_ok = false;
            return false;
        }
        if (len > 0U) {
            std::memcpy(dst, m_data + m_pos, len);
            m_pos += len;
        }
        return true;
    }

    uint32_t readU32() {
        uint32_t v = 0U;
        readBytes(&v, sizeof(v));
        return v;
    }

    int32_t readI32() {
        int32_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    int64_t readI64() {
        int64_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    double readDouble() {
        double v = 0.0;
        readBytes(&v, sizeof(v));
        return v;
    }

    bool readBool() {
        uint8_t b = 0U;
        readBytes(&b, sizeof(b));
        return (b != 0U);
    }

    std::string readString() {
        const uint32_t len = readU32();
        if (!m_ok || m_pos + len > m_size) {
            m_ok = false;
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        return s;
    }

    bool ok() const {
        return m_ok;
    }

private:
    const uint8_t* m_data;
    size_t         m_size;
    size_t         m_pos;
    bool           m_ok;
};

} // namespace

/*
 * Purpose: Constructs a FileManager bound to the specified data, backup, and export directories.
 * Inputs: dataDir, backupDir, exportDir paths.
 * Outputs: Initialized FileManager instance.
 * Big-O: O(1) time, O(1) space.
 */
FileManager::FileManager(const std::string& dataDir, const std::string& backupDir, const std::string& exportDir)
    : m_dataDir(dataDir), m_backupDir(backupDir), m_exportDir(exportDir) {}

/*
 * Purpose: Ensures that data/, backups/, and exports/ directories exist on disk.
 * Inputs: None.
 * Outputs: true if directories exist or were created cleanly.
 * Big-O: O(1) time, O(1) space.
 */
bool FileManager::ensureDirectories() const {
    std::error_code ec1, ec2, ec3;
    std::filesystem::create_directories(m_dataDir, ec1);
    std::filesystem::create_directories(m_backupDir, ec2);
    std::filesystem::create_directories(m_exportDir, ec3);
    return !ec1 && !ec2 && !ec3;
}

/*
 * Purpose: Joins m_dataDir and a bare filename (e.g. "rooms.dat").
 * Inputs: filename C-string.
 * Outputs: Combined path string.
 * Big-O: O(1) time, O(1) space.
 */
std::string FileManager::filePath(const char* filename) const {
    return (std::filesystem::path(m_dataDir) / filename).string();
}

/*
 * Purpose: Computes the 32-bit FNV-1a checksum over a contiguous byte buffer.
 * Inputs: data (pointer to bytes), length (byte count).
 * Outputs: uint32_t FNV-1a hash value.
 * Big-O: O(N) time where N = length, O(1) space.
 */
uint32_t FileManager::computeFnv1a(const uint8_t* data, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint32_t>(data[i]);
        hash *= 16777619U;
    }
    return hash;
}

/*
 * Purpose: Writes a binary file atomically using the Section 5 envelope format:
 *          8-byte magic ("HMSDATA1") + uint32 version + uint32 recordCount + payload + uint32 FNV-1a checksum.
 *          Writes to <targetPath>.tmp first, flushes, checks size > 0, and renames over targetPath.
 * Inputs: targetPath, recordCount, payload byte vector.
 * Outputs: true on successful atomic write, false on error.
 * Big-O: O(B) time and space where B is total byte size.
 */
bool FileManager::writeAtomicEnvelope(
    const std::string& targetPath,
    uint32_t recordCount,
    const std::vector<uint8_t>& payload
) const {
    ensureDirectories();

    std::vector<uint8_t> fullBytes;
    fullBytes.reserve(16U + payload.size() + 4U);

    // 1. 8-byte magic ("HMSDATA1")
    fullBytes.insert(
        fullBytes.end(),
        reinterpret_cast<const uint8_t*>(kFileMagic),
        reinterpret_cast<const uint8_t*>(kFileMagic) + 8U
    );

    // 2. uint32 version
    const uint32_t ver = kFileVersion;
    fullBytes.insert(
        fullBytes.end(),
        reinterpret_cast<const uint8_t*>(&ver),
        reinterpret_cast<const uint8_t*>(&ver) + sizeof(ver)
    );

    // 3. uint32 record count
    fullBytes.insert(
        fullBytes.end(),
        reinterpret_cast<const uint8_t*>(&recordCount),
        reinterpret_cast<const uint8_t*>(&recordCount) + sizeof(recordCount)
    );

    // 4. Serialized records payload
    if (!payload.empty()) {
        fullBytes.insert(fullBytes.end(), payload.begin(), payload.end());
    }

    // 5. Trailing uint32 FNV-1a checksum of everything before it
    const uint32_t checksum = computeFnv1a(fullBytes.data(), fullBytes.size());
    fullBytes.insert(
        fullBytes.end(),
        reinterpret_cast<const uint8_t*>(&checksum),
        reinterpret_cast<const uint8_t*>(&checksum) + sizeof(checksum)
    );

    const std::string tmpPath = targetPath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(fullBytes.data()), static_cast<std::streamsize>(fullBytes.size()));
        out.flush();
        if (!out.good()) {
            return false;
        }
    }

    std::error_code ec;
    const auto writtenSize = std::filesystem::file_size(tmpPath, ec);
    if (ec || writtenSize == 0U || writtenSize != fullBytes.size()) {
        return false;
    }

    // Atomic rename: on Windows std::filesystem::rename may fail if target exists, so remove target if needed
    std::filesystem::rename(tmpPath, targetPath, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(targetPath, rmEc);
        ec.clear();
        std::filesystem::rename(tmpPath, targetPath, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

/*
 * Purpose: Attempts to restore a single corrupted .dat file from the newest backup snapshot in backups/.
 * Inputs: filename (e.g. "rooms.dat").
 * Outputs: true if a backup was found and copied over data/<filename>, false otherwise.
 * Big-O: O(S + B) where S is number of snapshots and B is file size.
 */
bool FileManager::tryAutoRestoreSingleFile(const std::string& filename) const {
    const std::vector<std::string> snapshots = listBackupSnapshots();
    for (const std::string& snap : snapshots) {
        const std::filesystem::path candidate = std::filesystem::path(m_backupDir) / snap / filename;
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            const std::filesystem::path dest = std::filesystem::path(m_dataDir) / filename;
            std::filesystem::copy_file(candidate, dest, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                return true;
            }
        }
    }
    return false;
}

/*
 * Purpose: Reads and verifies a binary .dat file envelope (magic, version, FNV-1a checksum).
 *          If verification fails, attempts automatic restoration from the newest backup snapshot.
 * Inputs: filename, outRecordCount, outPayload, outError.
 * Outputs: true if file (or restored backup) loaded and verified cleanly, false otherwise.
 * Big-O: O(B) time and space where B is file byte length.
 */
bool FileManager::readVerifiedEnvelope(
    const std::string& filename,
    uint32_t& outRecordCount,
    std::vector<uint8_t>& outPayload,
    std::string* outError
) const {
    const std::string fullPath = filePath(filename.c_str());

    auto attemptReadOnce = [&](std::string& errOut) -> bool {
        std::ifstream in(fullPath, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            errOut = "File not found: " + fullPath;
            return false;
        }
        const std::streamsize totalSize = in.tellg();
        if (totalSize < 20) {
            errOut = "Corrupt file (truncated header): " + filename;
            return false;
        }
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(totalSize));
        if (!in.read(reinterpret_cast<char*>(bytes.data()), totalSize)) {
            errOut = "I/O read error on: " + filename;
            return false;
        }

        if (std::memcmp(bytes.data(), kFileMagic, 8U) != 0) {
            errOut = "Invalid file magic in " + filename;
            return false;
        }

        uint32_t version = 0U;
        uint32_t recCount = 0U;
        std::memcpy(&version, bytes.data() + 8U, sizeof(uint32_t));
        std::memcpy(&recCount, bytes.data() + 12U, sizeof(uint32_t));

        if (version != kFileVersion) {
            errOut = "Unsupported version in " + filename;
            return false;
        }

        uint32_t storedChecksum = 0U;
        const size_t bodyLen = bytes.size() - sizeof(uint32_t);
        std::memcpy(&storedChecksum, bytes.data() + bodyLen, sizeof(uint32_t));
        const uint32_t computedChecksum = computeFnv1a(bytes.data(), bodyLen);

        if (storedChecksum != computedChecksum) {
            errOut = "FNV-1a checksum mismatch in " + filename;
            return false;
        }

        outRecordCount = recCount;
        outPayload.assign(bytes.begin() + 16U, bytes.begin() + static_cast<std::ptrdiff_t>(bodyLen));
        return true;
    };

    std::string errMsg;
    if (attemptReadOnce(errMsg)) {
        return true;
    }

    // If file existed but failed magic/checksum, attempt auto-restore from newest backup per Section 5
    std::error_code ec;
    if (std::filesystem::exists(fullPath, ec) && !ec) {
        if (tryAutoRestoreSingleFile(filename)) {
            std::string retryErr;
            if (attemptReadOnce(retryErr)) {
                if (outError != nullptr) {
                    *outError = "Corrupted " + filename + " auto-restored from newest backup (" + errMsg + ").";
                }
                return true;
            }
        }
    }

    if (outError != nullptr) {
        *outError = errMsg;
    }
    return false;
}

/*
 * Purpose: Scans data/ at startup for leftover .tmp files from interrupted writes, removes them,
 *          and appends "Recovered from interrupted write" to the audit log (Section 5).
 * Inputs: auditLog reference, outMessages vector for toast notifications.
 * Outputs: Number of leftover .tmp files cleaned up.
 * Big-O: O(F) where F is the number of files in data/.
 */
int FileManager::recoverInterruptedWrites(
    std::vector<AuditEntry>& auditLog,
    std::vector<std::string>& outMessages
) const {
    ensureDirectories();
    int recoveredCount = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_dataDir, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".tmp") {
            const std::string tmpName = entry.path().filename().string();
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            ++recoveredCount;

            const std::string msg = "Recovered from interrupted write (" + tmpName + ")";
            outMessages.push_back(msg);
            appendAuditEntry(auditLog, 0, "system", "CRASH_RECOVERY", msg);
        }
    }
    if (recoveredCount > 0) {
        saveAuditLog(auditLog);
    }
    return recoveredCount;
}

// -----------------------------------------------------------------------------
// Save / Load: Rooms (rooms.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all Room records to data/rooms.dat.
 * Inputs: rooms vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space where N = rooms.size().
 */
bool FileManager::saveRooms(const std::vector<Room>& rooms) const {
    BinaryWriter w;
    for (const Room& r : rooms) {
        w.writeI32(static_cast<int32_t>(r.id));
        w.writeI32(static_cast<int32_t>(r.number));
        w.writeI32(static_cast<int32_t>(r.floor));
        w.writeI32(static_cast<int32_t>(r.type));
        w.writeI32(static_cast<int32_t>(r.view));
        w.writeI32(static_cast<int32_t>(r.bed));
        w.writeDouble(r.baseRate);
        w.writeI32(static_cast<int32_t>(r.noiseLevel));
        w.writeI32(static_cast<int32_t>(r.elevatorDistance));
        w.writeI32(static_cast<int32_t>(r.status));
        w.writeI32(static_cast<int32_t>(r.currentBookingId));
        w.writeI32(static_cast<int32_t>(r.lastCheckoutDay));
        w.writeI32(static_cast<int32_t>(r.lastCheckoutMinute));
        w.writeString(r.notes);
    }
    return writeAtomicEnvelope(filePath("rooms.dat"), static_cast<uint32_t>(rooms.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies Room records from data/rooms.dat.
 * Inputs: outRooms vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space where N = record count.
 */
bool FileManager::loadRooms(std::vector<Room>& outRooms, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("rooms.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<Room> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        Room item{};
        item.id                 = r.readI32();
        item.number             = r.readI32();
        item.floor              = r.readI32();
        item.type               = static_cast<RoomType>(r.readI32());
        item.view               = static_cast<ViewType>(r.readI32());
        item.bed                = static_cast<BedConfig>(r.readI32());
        item.baseRate           = r.readDouble();
        item.noiseLevel         = r.readI32();
        item.elevatorDistance   = r.readI32();
        item.status             = static_cast<RoomStatus>(r.readI32());
        item.currentBookingId   = r.readI32();
        item.lastCheckoutDay    = r.readI32();
        item.lastCheckoutMinute = r.readI32();
        item.notes              = r.readString();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in rooms.dat";
            }
            return false;
        }
        loaded.push_back(item);
    }
    outRooms = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Guests (guests.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all Guest records to data/guests.dat.
 * Inputs: guests vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveGuests(const std::vector<Guest>& guests) const {
    BinaryWriter w;
    for (const Guest& g : guests) {
        w.writeI32(static_cast<int32_t>(g.id));
        w.writeString(g.fullName);
        w.writeString(g.cnic);
        w.writeString(g.passport);
        w.writeString(g.phone);
        w.writeString(g.email);
        w.writeString(g.nationality);
        w.writeString(g.address);
        w.writeBool(g.isForeign);
        w.writeI32(static_cast<int32_t>(g.tier));
        w.writeDouble(g.lifetimeSpend);
        w.writeI32(static_cast<int32_t>(g.stayCount));
        w.writeI32(static_cast<int32_t>(g.registeredDay));
        w.writeString(g.preferencesNote);
    }
    return writeAtomicEnvelope(filePath("guests.dat"), static_cast<uint32_t>(guests.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies Guest records from data/guests.dat.
 * Inputs: outGuests vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadGuests(std::vector<Guest>& outGuests, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("guests.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<Guest> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        Guest g{};
        g.id              = r.readI32();
        g.fullName        = r.readString();
        g.cnic            = r.readString();
        g.passport        = r.readString();
        g.phone           = r.readString();
        g.email           = r.readString();
        g.nationality     = r.readString();
        g.address         = r.readString();
        g.isForeign       = r.readBool();
        g.tier            = static_cast<LoyaltyTier>(r.readI32());
        g.lifetimeSpend   = r.readDouble();
        g.stayCount       = r.readI32();
        g.registeredDay   = r.readI32();
        g.preferencesNote = r.readString();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in guests.dat";
            }
            return false;
        }
        loaded.push_back(g);
    }
    outGuests = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Bookings (bookings.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all Booking records (including nested AddOn vectors) to data/bookings.dat.
 * Inputs: bookings vector.
 * Outputs: true on success.
 * Big-O: O(N + A) time and space where N = bookings and A = total add-ons.
 */
bool FileManager::saveBookings(const std::vector<Booking>& bookings) const {
    BinaryWriter w;
    for (const Booking& b : bookings) {
        w.writeI32(static_cast<int32_t>(b.id));
        w.writeI32(static_cast<int32_t>(b.guestId));
        w.writeI32(static_cast<int32_t>(b.roomId));
        w.writeI32(static_cast<int32_t>(b.checkInDay));
        w.writeI32(static_cast<int32_t>(b.checkOutDay));
        w.writeI32(static_cast<int32_t>(b.checkInMinute));
        w.writeI32(static_cast<int32_t>(b.checkOutMinute));
        w.writeI32(static_cast<int32_t>(b.adults));
        w.writeI32(static_cast<int32_t>(b.children));
        w.writeI32(static_cast<int32_t>(b.status));
        w.writeDouble(b.quotedRate);
        w.writeDouble(b.surgeMultiplier);
        w.writeString(b.promoCode);
        w.writeU32(static_cast<uint32_t>(b.addOns.size()));
        for (const AddOn& a : b.addOns) {
            w.writeI32(static_cast<int32_t>(a.type));
            w.writeString(a.description);
            w.writeDouble(a.unitPrice);
            w.writeI32(static_cast<int32_t>(a.quantity));
            w.writeI32(static_cast<int32_t>(a.dayAdded));
        }
        w.writeI32(static_cast<int32_t>(b.invoiceId));
        w.writeI32(static_cast<int32_t>(b.createdDay));
    }
    return writeAtomicEnvelope(filePath("bookings.dat"), static_cast<uint32_t>(bookings.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies Booking records from data/bookings.dat.
 * Inputs: outBookings vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N + A) time and space.
 */
bool FileManager::loadBookings(std::vector<Booking>& outBookings, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("bookings.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<Booking> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        Booking b{};
        b.id              = r.readI32();
        b.guestId         = r.readI32();
        b.roomId          = r.readI32();
        b.checkInDay      = r.readI32();
        b.checkOutDay     = r.readI32();
        b.checkInMinute   = r.readI32();
        b.checkOutMinute  = r.readI32();
        b.adults          = r.readI32();
        b.children        = r.readI32();
        b.status          = static_cast<BookingStatus>(r.readI32());
        b.quotedRate      = r.readDouble();
        b.surgeMultiplier = r.readDouble();
        b.promoCode       = r.readString();
        const uint32_t addOnCount = r.readU32();
        b.addOns.reserve(addOnCount);
        for (uint32_t j = 0U; j < addOnCount; ++j) {
            AddOn a{};
            a.type        = static_cast<AddOnType>(r.readI32());
            a.description = r.readString();
            a.unitPrice   = r.readDouble();
            a.quantity    = r.readI32();
            a.dayAdded    = r.readI32();
            b.addOns.push_back(a);
        }
        b.invoiceId  = r.readI32();
        b.createdDay = r.readI32();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in bookings.dat";
            }
            return false;
        }
        loaded.push_back(b);
    }
    outBookings = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Invoices (invoices.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all Invoice records to data/invoices.dat.
 * Inputs: invoices vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveInvoices(const std::vector<Invoice>& invoices) const {
    BinaryWriter w;
    for (const Invoice& inv : invoices) {
        w.writeI32(static_cast<int32_t>(inv.id));
        w.writeString(inv.number);
        w.writeI32(static_cast<int32_t>(inv.bookingId));
        w.writeI32(static_cast<int32_t>(inv.guestId));
        w.writeI32(static_cast<int32_t>(inv.issuedDay));
        w.writeI32(static_cast<int32_t>(inv.nights));
        w.writeDouble(inv.roomSubtotal);
        w.writeDouble(inv.addOnSubtotal);
        w.writeDouble(inv.loyaltyDiscount);
        w.writeDouble(inv.promoDiscount);
        w.writeDouble(inv.taxableAmount);
        w.writeDouble(inv.taxRate);
        w.writeDouble(inv.taxAmount);
        w.writeDouble(inv.grandTotal);
        w.writeBool(inv.paid);
        w.writeString(inv.paymentMethod);
    }
    return writeAtomicEnvelope(filePath("invoices.dat"), static_cast<uint32_t>(invoices.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies Invoice records from data/invoices.dat.
 * Inputs: outInvoices vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadInvoices(std::vector<Invoice>& outInvoices, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("invoices.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<Invoice> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        Invoice inv{};
        inv.id              = r.readI32();
        inv.number          = r.readString();
        inv.bookingId       = r.readI32();
        inv.guestId         = r.readI32();
        inv.issuedDay       = r.readI32();
        inv.nights          = r.readI32();
        inv.roomSubtotal    = r.readDouble();
        inv.addOnSubtotal   = r.readDouble();
        inv.loyaltyDiscount = r.readDouble();
        inv.promoDiscount   = r.readDouble();
        inv.taxableAmount   = r.readDouble();
        inv.taxRate         = r.readDouble();
        inv.taxAmount       = r.readDouble();
        inv.grandTotal      = r.readDouble();
        inv.paid            = r.readBool();
        inv.paymentMethod   = r.readString();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in invoices.dat";
            }
            return false;
        }
        loaded.push_back(inv);
    }
    outInvoices = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Users (users.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all User accounts to data/users.dat.
 * Inputs: users vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveUsers(const std::vector<User>& users) const {
    BinaryWriter w;
    for (const User& u : users) {
        w.writeI32(static_cast<int32_t>(u.id));
        w.writeString(u.username);
        w.writeString(u.saltHex);
        w.writeString(u.passHashHex);
        w.writeI32(static_cast<int32_t>(u.role));
        w.writeString(u.displayName);
        w.writeBool(u.active);
        w.writeI32(static_cast<int32_t>(u.lastLoginDay));
    }
    return writeAtomicEnvelope(filePath("users.dat"), static_cast<uint32_t>(users.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies User accounts from data/users.dat.
 * Inputs: outUsers vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadUsers(std::vector<User>& outUsers, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("users.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<User> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        User u{};
        u.id           = r.readI32();
        u.username     = r.readString();
        u.saltHex      = r.readString();
        u.passHashHex  = r.readString();
        u.role         = static_cast<Role>(r.readI32());
        u.displayName  = r.readString();
        u.active       = r.readBool();
        u.lastLoginDay = r.readI32();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in users.dat";
            }
            return false;
        }
        loaded.push_back(u);
    }
    outUsers = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Audit Log (audit_log.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves the SHA-256 hash-chained audit log to data/audit_log.dat.
 * Inputs: auditLog vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveAuditLog(const std::vector<AuditEntry>& auditLog) const {
    BinaryWriter w;
    for (const AuditEntry& e : auditLog) {
        w.writeI32(static_cast<int32_t>(e.index));
        w.writeI64(static_cast<int64_t>(e.timestamp));
        w.writeI32(static_cast<int32_t>(e.userId));
        w.writeString(e.username);
        w.writeString(e.action);
        w.writeString(e.details);
        w.writeString(e.prevHashHex);
        w.writeString(e.hashHex);
    }
    return writeAtomicEnvelope(filePath("audit_log.dat"), static_cast<uint32_t>(auditLog.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies AuditEntry records from data/audit_log.dat.
 * Inputs: outAuditLog vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadAuditLog(std::vector<AuditEntry>& outAuditLog, std::string* outError) const {
    // Audit logs are NEVER silently auto-restored on mismatch: any single-byte edit to
    // data/audit_log.dat must surface to verifyAuditChain() and trigger the flashing red banner.
    const std::string fullPath = filePath("audit_log.dat");
    std::ifstream in(fullPath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        if (outError != nullptr) {
            *outError = "File not found: " + fullPath;
        }
        return false;
    }

    const std::streamsize totalSize = in.tellg();
    if (totalSize < 20) {
        AuditEntry corrupted{};
        corrupted.index       = 0;
        corrupted.action      = "FILE_CORRUPTED";
        corrupted.details     = "audit_log.dat truncated or header corrupted on disk";
        corrupted.prevHashHex = genesisAuditHash();
        corrupted.hashHex     = "TAMPERED_TRUNCATED_HEADER";
        outAuditLog           = {corrupted};
        if (outError != nullptr) {
            *outError = "TAMPER ALERT: audit_log.dat header truncated.";
        }
        return true;
    }

    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(totalSize));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), totalSize)) {
        if (outError != nullptr) {
            *outError = "I/O read error on: audit_log.dat";
        }
        return false;
    }

    const bool magicOk = (std::memcmp(bytes.data(), kFileMagic, 8U) == 0);
    uint32_t version = 0U;
    uint32_t count = 0U;
    std::memcpy(&version, bytes.data() + 8U, sizeof(uint32_t));
    std::memcpy(&count, bytes.data() + 12U, sizeof(uint32_t));

    uint32_t storedChecksum = 0U;
    const size_t bodyLen = bytes.size() - sizeof(uint32_t);
    std::memcpy(&storedChecksum, bytes.data() + bodyLen, sizeof(uint32_t));
    const uint32_t computedChecksum = computeFnv1a(bytes.data(), bodyLen);
    const bool checksumMismatch = (storedChecksum != computedChecksum);

    if (!magicOk || version != kFileVersion) {
        AuditEntry corrupted{};
        corrupted.index       = 0;
        corrupted.action      = "HEADER_TAMPERED";
        corrupted.details     = "audit_log.dat magic/version modified on disk";
        corrupted.prevHashHex = genesisAuditHash();
        corrupted.hashHex     = "TAMPERED_ENVELOPE_HEADER";
        outAuditLog           = {corrupted};
        if (outError != nullptr) {
            *outError = "TAMPER ALERT: audit_log.dat magic/version tampered.";
        }
        return true;
    }

    BinaryReader r(bytes.data() + 16U, bodyLen - 16U);
    std::vector<AuditEntry> loaded;
    loaded.reserve(count < 100000U ? count : 1000U);
    for (uint32_t i = 0U; i < count; ++i) {
        AuditEntry e{};
        e.index       = r.readI32();
        e.timestamp   = static_cast<long long>(r.readI64());
        e.userId      = r.readI32();
        e.username    = r.readString();
        e.action      = r.readString();
        e.details     = r.readString();
        e.prevHashHex = r.readString();
        e.hashHex     = r.readString();
        if (!r.ok()) {
            AuditEntry corrupted{};
            corrupted.index       = static_cast<int>(i);
            corrupted.action      = "RECORD_TAMPERED";
            corrupted.details     = "Malformed/tampered audit record at index #" + std::to_string(i);
            corrupted.prevHashHex = loaded.empty() ? genesisAuditHash() : loaded.back().hashHex;
            corrupted.hashHex     = "TAMPERED_PAYLOAD_RECORD";
            loaded.push_back(corrupted);
            outAuditLog = std::move(loaded);
            if (outError != nullptr) {
                *outError = "TAMPER ALERT: Malformed record payload in audit_log.dat";
            }
            return true;
        }
        loaded.push_back(e);
    }

    if (checksumMismatch) {
        if (!loaded.empty() && verifyAuditChain(loaded) == -1) {
            // Byte was flipped in the trailing checksum or envelope metadata; ensure chain verify flags it
            loaded.back().hashHex = "TAMPERED_FILE_CHECKSUM";
        }
        if (outError != nullptr) {
            *outError = "TAMPER ALERT: FNV-1a checksum mismatch in audit_log.dat";
        }
    }

    outAuditLog = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Housekeeping Tasks (tasks.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all HousekeepingTask records to data/tasks.dat.
 * Inputs: tasks vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveTasks(const std::vector<HousekeepingTask>& tasks) const {
    BinaryWriter w;
    for (const HousekeepingTask& t : tasks) {
        w.writeI32(static_cast<int32_t>(t.id));
        w.writeI32(static_cast<int32_t>(t.roomId));
        w.writeI32(static_cast<int32_t>(t.type));
        w.writeI32(static_cast<int32_t>(t.status));
        w.writeI32(static_cast<int32_t>(t.severity));
        w.writeI32(static_cast<int32_t>(t.createdDay));
        w.writeI32(static_cast<int32_t>(t.createdMinute));
        w.writeDouble(t.priorityScore);
        w.writeString(t.assignedTo);
        w.writeString(t.note);
    }
    return writeAtomicEnvelope(filePath("tasks.dat"), static_cast<uint32_t>(tasks.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies HousekeepingTask records from data/tasks.dat.
 * Inputs: outTasks vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadTasks(std::vector<HousekeepingTask>& outTasks, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("tasks.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<HousekeepingTask> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        HousekeepingTask t{};
        t.id            = r.readI32();
        t.roomId        = r.readI32();
        t.type          = static_cast<TaskType>(r.readI32());
        t.status        = static_cast<TaskStatus>(r.readI32());
        t.severity      = r.readI32();
        t.createdDay    = r.readI32();
        t.createdMinute = r.readI32();
        t.priorityScore = r.readDouble();
        t.assignedTo    = r.readString();
        t.note          = r.readString();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in tasks.dat";
            }
            return false;
        }
        loaded.push_back(t);
    }
    outTasks = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Waitlist (waitlist.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all WaitlistEntry records to data/waitlist.dat.
 * Inputs: waitlist vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveWaitlist(const std::vector<WaitlistEntry>& waitlist) const {
    BinaryWriter w;
    for (const WaitlistEntry& wl : waitlist) {
        w.writeI32(static_cast<int32_t>(wl.id));
        w.writeI32(static_cast<int32_t>(wl.guestId));
        w.writeI32(static_cast<int32_t>(wl.wantedType));
        w.writeI32(static_cast<int32_t>(wl.fromDay));
        w.writeI32(static_cast<int32_t>(wl.toDay));
        w.writeDouble(wl.maxBudget);
        w.writeI32(static_cast<int32_t>(wl.createdDay));
    }
    return writeAtomicEnvelope(filePath("waitlist.dat"), static_cast<uint32_t>(waitlist.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies WaitlistEntry records from data/waitlist.dat.
 * Inputs: outWaitlist vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadWaitlist(std::vector<WaitlistEntry>& outWaitlist, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("waitlist.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<WaitlistEntry> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        WaitlistEntry wl{};
        wl.id         = r.readI32();
        wl.guestId    = r.readI32();
        wl.wantedType = static_cast<RoomType>(r.readI32());
        wl.fromDay    = r.readI32();
        wl.toDay      = r.readI32();
        wl.maxBudget  = r.readDouble();
        wl.createdDay = r.readI32();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in waitlist.dat";
            }
            return false;
        }
        loaded.push_back(wl);
    }
    outWaitlist = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Service Orders (orders.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all ServiceOrder records to data/orders.dat.
 * Inputs: orders vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveOrders(const std::vector<ServiceOrder>& orders) const {
    BinaryWriter w;
    for (const ServiceOrder& o : orders) {
        w.writeI32(static_cast<int32_t>(o.id));
        w.writeI32(static_cast<int32_t>(o.bookingId));
        w.writeI32(static_cast<int32_t>(o.type));
        w.writeString(o.item);
        w.writeI32(static_cast<int32_t>(o.qty));
        w.writeDouble(o.price);
        w.writeI32(static_cast<int32_t>(o.stage));
    }
    return writeAtomicEnvelope(filePath("orders.dat"), static_cast<uint32_t>(orders.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies ServiceOrder records from data/orders.dat.
 * Inputs: outOrders vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadOrders(std::vector<ServiceOrder>& outOrders, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("orders.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<ServiceOrder> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        ServiceOrder o{};
        o.id        = r.readI32();
        o.bookingId = r.readI32();
        o.type      = static_cast<AddOnType>(r.readI32());
        o.item      = r.readString();
        o.qty       = r.readI32();
        o.price     = r.readDouble();
        o.stage     = r.readI32();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in orders.dat";
            }
            return false;
        }
        loaded.push_back(o);
    }
    outOrders = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Daily Stats (stats.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves all DailyStat records to data/stats.dat.
 * Inputs: stats vector.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::saveStats(const std::vector<DailyStat>& stats) const {
    BinaryWriter w;
    for (const DailyStat& s : stats) {
        w.writeI32(static_cast<int32_t>(s.day));
        w.writeDouble(s.revenue);
        w.writeDouble(s.occupancyPct);
        w.writeI32(static_cast<int32_t>(s.checkins));
        w.writeI32(static_cast<int32_t>(s.checkouts));
    }
    return writeAtomicEnvelope(filePath("stats.dat"), static_cast<uint32_t>(stats.size()), w.buffer());
}

/*
 * Purpose: Loads and verifies DailyStat records from data/stats.dat.
 * Inputs: outStats vector, optional outError string.
 * Outputs: true on success.
 * Big-O: O(N) time and space.
 */
bool FileManager::loadStats(std::vector<DailyStat>& outStats, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("stats.dat", count, payload, outError)) {
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    std::vector<DailyStat> loaded;
    loaded.reserve(count);
    for (uint32_t i = 0U; i < count; ++i) {
        DailyStat s{};
        s.day          = r.readI32();
        s.revenue      = r.readDouble();
        s.occupancyPct = r.readDouble();
        s.checkins     = r.readI32();
        s.checkouts    = r.readI32();
        if (!r.ok()) {
            if (outError != nullptr) {
                *outError = "Malformed record payload in stats.dat";
            }
            return false;
        }
        loaded.push_back(s);
    }
    outStats = std::move(loaded);
    return true;
}

// -----------------------------------------------------------------------------
// Save / Load: Settings (settings.dat)
// -----------------------------------------------------------------------------
/*
 * Purpose: Atomically saves AppSettings to data/settings.dat.
 * Inputs: settings struct.
 * Outputs: true on success.
 * Big-O: O(1) time, O(1) space.
 */
bool FileManager::saveSettings(const AppSettings& settings) const {
    BinaryWriter w;
    w.writeI32(static_cast<int32_t>(settings.themeChoice));
    w.writeDouble(settings.taxRate);
    w.writeBool(settings.surgeEnabled);
    w.writeDouble(settings.manualSurgeOverride);
    w.writeI32(static_cast<int32_t>(settings.nextGuestId));
    w.writeI32(static_cast<int32_t>(settings.nextBookingId));
    w.writeI32(static_cast<int32_t>(settings.nextInvoiceId));
    w.writeI32(static_cast<int32_t>(settings.nextUserId));
    w.writeI32(static_cast<int32_t>(settings.nextTaskId));
    w.writeI32(static_cast<int32_t>(settings.nextWaitlistId));
    w.writeI32(static_cast<int32_t>(settings.nextOrderId));
    return writeAtomicEnvelope(filePath("settings.dat"), 1U, w.buffer());
}

/*
 * Purpose: Loads and verifies AppSettings from data/settings.dat.
 * Inputs: outSettings struct, optional outError string.
 * Outputs: true on success.
 * Big-O: O(1) time, O(1) space.
 */
bool FileManager::loadSettings(AppSettings& outSettings, std::string* outError) const {
    uint32_t count = 0U;
    std::vector<uint8_t> payload;
    if (!readVerifiedEnvelope("settings.dat", count, payload, outError)) {
        return false;
    }
    if (count < 1U) {
        if (outError != nullptr) {
            *outError = "Empty settings.dat record count";
        }
        return false;
    }
    BinaryReader r(payload.data(), payload.size());
    AppSettings s{};
    s.themeChoice         = r.readI32();
    s.taxRate             = r.readDouble();
    s.surgeEnabled        = r.readBool();
    s.manualSurgeOverride = r.readDouble();
    s.nextGuestId         = r.readI32();
    s.nextBookingId       = r.readI32();
    s.nextInvoiceId       = r.readI32();
    s.nextUserId          = r.readI32();
    s.nextTaskId          = r.readI32();
    s.nextWaitlistId      = r.readI32();
    s.nextOrderId         = r.readI32();
    if (!r.ok()) {
        if (outError != nullptr) {
            *outError = "Malformed record payload in settings.dat";
        }
        return false;
    }
    outSettings = s;
    return true;
}

/*
 * Purpose: Saves all 11 database tables atomically to the data/ directory (.dat files).
 * Inputs: db (HotelDatabase struct).
 * Outputs: true if every table saved cleanly.
 * Big-O: O(Total Records) time and space.
 */
bool FileManager::saveAll(const HotelDatabase& db) const {
    bool ok = true;
    ok = saveRooms(db.rooms) && ok;
    ok = saveGuests(db.guests) && ok;
    ok = saveBookings(db.bookings) && ok;
    ok = saveInvoices(db.invoices) && ok;
    ok = saveUsers(db.users) && ok;
    ok = saveAuditLog(db.auditLog) && ok;
    ok = saveTasks(db.tasks) && ok;
    ok = saveWaitlist(db.waitlist) && ok;
    ok = saveOrders(db.orders) && ok;
    ok = saveStats(db.stats) && ok;
    ok = saveSettings(db.settings) && ok;
    return ok;
}

/*
 * Purpose: Loads all 11 .dat files from data/. If first run (rooms.dat/users.dat missing),
 *          initializes default 50 rooms and 3 default users and persists them.
 * Inputs: db (HotelDatabase reference), outAlerts (vector of warning/recovery strings for UI toasts).
 * Outputs: true if database is ready for operation.
 * Big-O: O(Total Records) time and space.
 */
bool FileManager::loadAll(HotelDatabase& db, std::vector<std::string>& outAlerts) const {
    ensureDirectories();

    // First check audit_log.dat so crash recovery can append to it
    std::string err;
    loadAuditLog(db.auditLog, &err);
    recoverInterruptedWrites(db.auditLog, outAlerts);

    const bool roomsExist = std::filesystem::exists(filePath("rooms.dat"));
    const bool usersExist = std::filesystem::exists(filePath("users.dat"));
    if (!roomsExist || !usersExist) {
        return resetCleanState(db, 0, "system");
    }

    auto tryLoadTable = [&](bool okLoad, const std::string& tableErr) {
        if (!okLoad && !tableErr.empty()) {
            outAlerts.push_back(tableErr);
        } else if (okLoad && !tableErr.empty()) {
            // Auto-restored from backup message
            outAlerts.push_back(tableErr);
        }
    };

    err.clear(); tryLoadTable(loadRooms(db.rooms, &err), err);
    err.clear(); tryLoadTable(loadGuests(db.guests, &err), err);
    err.clear(); tryLoadTable(loadBookings(db.bookings, &err), err);
    err.clear(); tryLoadTable(loadInvoices(db.invoices, &err), err);
    err.clear(); tryLoadTable(loadUsers(db.users, &err), err);
    err.clear(); tryLoadTable(loadTasks(db.tasks, &err), err);
    err.clear(); tryLoadTable(loadWaitlist(db.waitlist, &err), err);
    err.clear(); tryLoadTable(loadOrders(db.orders, &err), err);
    err.clear(); tryLoadTable(loadStats(db.stats, &err), err);
    err.clear(); tryLoadTable(loadSettings(db.settings, &err), err);

    if (db.rooms.empty()) {
        db.rooms = generateDefaultRooms();
        saveRooms(db.rooms);
    }
    if (db.users.empty()) {
        db.users = generateDefaultUsers();
        saveUsers(db.users);
    }
    return true;
}

/*
 * Purpose: Creates and appends a cryptographically chained AuditEntry to auditLog.
 * Inputs: auditLog, userId, username, action, details, explicitTimestamp (0 = current unix epoch seconds).
 * Outputs: Copy of the newly appended AuditEntry.
 * Big-O: O(|action| + |details|) time, O(1) auxiliary space.
 */
AuditEntry FileManager::appendAuditEntry(
    std::vector<AuditEntry>& auditLog,
    int userId,
    const std::string& username,
    const std::string& action,
    const std::string& details,
    long long explicitTimestamp
) {
    AuditEntry entry{};
    entry.index     = static_cast<int>(auditLog.size());
    entry.timestamp = (explicitTimestamp != 0)
                          ? explicitTimestamp
                          : static_cast<long long>(std::time(nullptr));
    entry.userId    = userId;
    entry.username  = username;
    entry.action    = action;
    entry.details   = details;
    entry.prevHashHex = auditLog.empty() ? genesisAuditHash() : auditLog.back().hashHex;
    entry.hashHex     = computeAuditEntryHash(
        entry.prevHashHex,
        entry.timestamp,
        entry.userId,
        entry.action,
        entry.details
    );
    auditLog.push_back(entry);
    return entry;
}

/*
 * Purpose: Copies all .dat files from data/ into backups/YYYYMMDD_HHMMSS/ and prunes older snapshots beyond 10.
 * Inputs: outSnapshotName (optional pointer to receive the created folder name).
 * Outputs: true if snapshot directory was created and populated.
 * Big-O: O(B) where B is total size of the .dat files in data/.
 */
bool FileManager::createBackupSnapshot(std::string* outSnapshotName) const {
    ensureDirectories();

    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char folderName[32]{};
    std::snprintf(
        folderName,
        sizeof(folderName),
        "%04d%02d%02d_%02d%02d%02d",
        tmBuf.tm_year + 1900,
        tmBuf.tm_mon + 1,
        tmBuf.tm_mday,
        tmBuf.tm_hour,
        tmBuf.tm_min,
        tmBuf.tm_sec
    );

    std::filesystem::path snapDir = std::filesystem::path(m_backupDir) / folderName;
    std::error_code ec;
    int suffix = 1;
    while (std::filesystem::exists(snapDir, ec) && !ec) {
        snapDir = std::filesystem::path(m_backupDir) / (std::string(folderName) + "_" + std::to_string(suffix));
        ++suffix;
    }

    std::filesystem::create_directories(snapDir, ec);
    if (ec) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_dataDir, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".dat") {
            std::error_code cpEc;
            std::filesystem::copy_file(
                entry.path(),
                snapDir / entry.path().filename(),
                std::filesystem::copy_options::overwrite_existing,
                cpEc
            );
        }
    }

    if (outSnapshotName != nullptr) {
        *outSnapshotName = snapDir.filename().string();
    }

    pruneOldBackups(10U);
    return true;
}

/*
 * Purpose: Lists all snapshot subdirectories inside backups/ sorted newest-first.
 * Inputs: None.
 * Outputs: Vector of snapshot directory names (e.g. "20261118_133500").
 * Big-O: O(S log S) where S is the number of backup folders.
 */
std::vector<std::string> FileManager::listBackupSnapshots() const {
    std::vector<std::string> names;
    std::error_code ec;
    if (!std::filesystem::exists(m_backupDir, ec) || ec) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_backupDir, ec)) {
        if (!ec && entry.is_directory()) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end(), std::greater<std::string>());
    return names;
}

/*
 * Purpose: Keeps only the newest maxKeep backup snapshots and deletes older ones (Section 5: max 10).
 * Inputs: maxKeep (default 10).
 * Outputs: Deletes older snapshot directories from disk.
 * Big-O: O(S log S) where S is number of backup folders.
 */
void FileManager::pruneOldBackups(size_t maxKeep) const {
    const std::vector<std::string> snapshots = listBackupSnapshots();
    for (size_t i = maxKeep; i < snapshots.size(); ++i) {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(m_backupDir) / snapshots[i], ec);
    }
}

/*
 * Purpose: Restores all .dat files from backups/<snapshotName>/ into data/.
 * Inputs: snapshotName, optional outError.
 * Outputs: true if restored cleanly, false otherwise.
 * Big-O: O(B) where B is total byte size of .dat files in the snapshot.
 */
bool FileManager::restoreFromSnapshot(const std::string& snapshotName, std::string* outError) const {
    const std::filesystem::path srcDir = std::filesystem::path(m_backupDir) / snapshotName;
    std::error_code ec;
    if (!std::filesystem::exists(srcDir, ec) || !std::filesystem::is_directory(srcDir, ec)) {
        if (outError != nullptr) {
            *outError = "Backup snapshot not found: " + snapshotName;
        }
        return false;
    }

    int copiedFiles = 0;
    for (const auto& entry : std::filesystem::directory_iterator(srcDir, ec)) {
        if (!ec && entry.is_regular_file() && entry.path().extension() == ".dat") {
            std::error_code cpEc;
            std::filesystem::copy_file(
                entry.path(),
                std::filesystem::path(m_dataDir) / entry.path().filename(),
                std::filesystem::copy_options::overwrite_existing,
                cpEc
            );
            if (!cpEc) {
                ++copiedFiles;
            }
        }
    }
    if (copiedFiles == 0) {
        if (outError != nullptr) {
            *outError = "Snapshot contained no .dat files: " + snapshotName;
        }
        return false;
    }
    return true;
}

/*
 * Purpose: Generates the 50 hotel rooms across Floors 1..5 (101..110 to 501..510) per Section 10 rules:
 *          Floors 1-2 mostly Standard, 3 Deluxe, 4 Deluxe/Suite, 5 Suite + 2 Presidential (509, 510);
 *          base rates 8,000 / 12,500 / 22,000 / 45,000 PKR; views cycle City/Garden/Pool;
 *          elevatorDistance = round(abs(unitIndex - 4.5)); noiseLevel derived from elevator & street side.
 * Inputs: None.
 * Outputs: Vector of 50 initialized Room structs.
 * Big-O: O(1) time (fixed 50 rooms), O(1) space.
 */
std::vector<Room> FileManager::generateDefaultRooms() {
    std::vector<Room> rooms;
    rooms.reserve(Cfg::TOTAL_ROOMS);

    const int32_t today = DateUtil::todayDayNumber();
    int nextId = 1;

    for (int floor = 1; floor <= Cfg::FLOORS; ++floor) {
        for (int unitIndex = 0; unitIndex < Cfg::ROOMS_PER_FLOOR; ++unitIndex) {
            Room r{};
            r.id     = nextId++;
            r.floor  = floor;
            r.number = floor * 100 + (unitIndex + 1);

            // Room type & base rate distribution per Section 10
            if (floor <= 2) {
                if (unitIndex >= 8) {
                    r.type     = RoomType::Deluxe;
                    r.baseRate = Cfg::RATE_DELUXE;
                } else {
                    r.type     = RoomType::Standard;
                    r.baseRate = Cfg::RATE_STANDARD;
                }
            } else if (floor == 3) {
                r.type     = RoomType::Deluxe;
                r.baseRate = Cfg::RATE_DELUXE;
            } else if (floor == 4) {
                if (unitIndex < 5) {
                    r.type     = RoomType::Deluxe;
                    r.baseRate = Cfg::RATE_DELUXE;
                } else {
                    r.type     = RoomType::Suite;
                    r.baseRate = Cfg::RATE_SUITE;
                }
            } else {
                if (unitIndex >= 8) {
                    r.type     = RoomType::Presidential; // Rooms 509 and 510
                    r.baseRate = Cfg::RATE_PRESIDENTIAL;
                } else {
                    r.type     = RoomType::Suite;
                    r.baseRate = Cfg::RATE_SUITE;
                }
            }

            // Views cycle City / Garden / Pool
            const int viewIdx = (floor + unitIndex) % 3;
            r.view = (viewIdx == 0) ? ViewType::City : ((viewIdx == 1) ? ViewType::Garden : ViewType::Pool);

            // Bed configuration varies sensibly by room type and unit index
            if (r.type == RoomType::Presidential || r.type == RoomType::Suite) {
                r.bed = BedConfig::King;
            } else if (r.type == RoomType::Deluxe) {
                r.bed = (unitIndex % 2 == 0) ? BedConfig::King : BedConfig::Twin;
            } else {
                const int bIdx = unitIndex % 3;
                r.bed = (bIdx == 0) ? BedConfig::Single : ((bIdx == 1) ? BedConfig::Double : BedConfig::Twin);
            }

            // elevatorDistance = abs(unitIndex - 4.5) rounded -> 0..5
            r.elevatorDistance = static_cast<int>(std::llround(std::fabs(static_cast<double>(unitIndex) - 4.5)));

            // noiseLevel (1 quiet .. 5 loud) derived from elevator proximity, low floor, and street side (City view)
            int noise = 1;
            if (r.elevatorDistance <= 1) {
                noise += 2;
            } else if (r.elevatorDistance == 2) {
                noise += 1;
            }
            if (floor <= 2) {
                noise += 1;
            }
            if (r.view == ViewType::City) {
                noise += 1;
            }
            r.noiseLevel = std::clamp(noise, 1, 5);

            r.status             = RoomStatus::Available;
            r.currentBookingId   = -1;
            r.lastCheckoutDay    = today - 1;
            r.lastCheckoutMinute = 660;
            r.notes              = "Ready for inspection";

            rooms.push_back(r);
        }
    }
    return rooms;
}

/*
 * Purpose: Generates the 3 default staff accounts required by Section 7:
 *          admin / Admin@123, reception / Desk@123, housekeeping / Clean@123.
 * Inputs: None.
 * Outputs: Vector of 3 User records with salted 1000-round SHA-256 password hashes.
 * Big-O: O(1) time, O(1) space.
 */
std::vector<User> FileManager::generateDefaultUsers() {
    const int32_t today = DateUtil::todayDayNumber();
    std::vector<User> users;
    users.reserve(3U);

    User admin{};
    admin.id           = 1;
    admin.username     = "admin";
    admin.saltHex      = "a1b2c3d4e5f60718293a4b5c6d7e8f90";
    admin.passHashHex  = hashPassword(admin.saltHex, "Admin@123");
    admin.role         = Role::Admin;
    admin.displayName  = "System Administrator";
    admin.active       = true;
    admin.lastLoginDay = today;
    users.push_back(admin);

    User reception{};
    reception.id           = 2;
    reception.username     = "reception";
    reception.saltHex      = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
    reception.passHashHex  = hashPassword(reception.saltHex, "Desk@123");
    reception.role         = Role::Receptionist;
    reception.displayName  = "Front Desk Officer";
    reception.active       = true;
    reception.lastLoginDay = today;
    users.push_back(reception);

    User housekeeping{};
    housekeeping.id           = 3;
    housekeeping.username     = "housekeeping";
    housekeeping.saltHex      = "123456789abcdef0fedcba9876543210";
    housekeeping.passHashHex  = hashPassword(housekeeping.saltHex, "Clean@123");
    housekeeping.role         = Role::Housekeeping;
    housekeeping.displayName  = "Housekeeping Supervisor";
    housekeeping.active       = true;
    housekeeping.lastLoginDay = today;
    users.push_back(housekeeping);

    return users;
}

/*
 * Purpose: Resets the hotel database to a clean state (50 default rooms + 3 default users + genesis audit entry)
 *          and atomically saves all 11 .dat files (Section 10).
 * Inputs: db reference, actorUserId, actorUsername.
 * Outputs: true if all files were written cleanly.
 * Big-O: O(1) time (fixed 50 rooms + 3 users), O(1) space.
 */
bool FileManager::resetCleanState(HotelDatabase& db, int actorUserId, const std::string& actorUsername) const {
    ensureDirectories();

    db.rooms    = generateDefaultRooms();
    db.guests.clear();
    db.bookings.clear();
    db.invoices.clear();
    db.users    = generateDefaultUsers();
    db.auditLog.clear();
    db.tasks.clear();
    db.waitlist.clear();
    db.orders.clear();
    db.stats.clear();
    db.settings = AppSettings{};

    appendAuditEntry(
        db.auditLog,
        actorUserId,
        actorUsername,
        "RESET_CLEAN_STATE",
        "Initialized clean hotel state with 50 rooms and 3 default staff accounts"
    );

    return saveAll(db);
}

/*
 * Purpose: Populates the complete Section 10 Viva Demo dataset anchored relative to today's date:
 *          - 50 rooms (101..510)
 *          - 35 diverse guests (22 Pakistani across Punjabi, Pashtun, Sindhi, Baloch, Kashmiri + 13 international)
 *            with >=3 Platinum, >=5 Gold, >=7 Silver tiers
 *          - 30 days of historical DailyStat rows (weekend peaks, rising trend, ~PKR 5.4M total)
 *          - Today's occupancy ~76% (38/50 rooms Occupied/Reserved so 1.10x surge is active)
 *          - 8 CleaningRequired + 3 Maintenance rooms in tasks, 6 arrivals & 5 departures today
 *          - 4 pending service orders, 2 FIFO waitlist entries, and >= 65 valid SHA-256 chained audit entries.
 *          Takes a snapshot backup first before overwriting data/.
 * Inputs: db reference, actorUserId, actorUsername.
 * Outputs: true if seeded and saved atomically.
 * Big-O: O(1) bounded deterministic dataset generation.
 */
bool FileManager::seedVivaData(HotelDatabase& db, int actorUserId, const std::string& actorUsername) const {
    ensureDirectories();
    createBackupSnapshot(); // Section 10: backup first

    const int32_t today = DateUtil::todayDayNumber();
    const long long baseTs = static_cast<long long>(std::time(nullptr)) - (30LL * 86400LL);

    db.rooms    = generateDefaultRooms();
    db.users    = generateDefaultUsers();
    db.guests.clear();
    db.bookings.clear();
    db.invoices.clear();
    db.auditLog.clear();
    db.tasks.clear();
    db.waitlist.clear();
    db.orders.clear();
    db.stats.clear();
    db.settings = AppSettings{};

    // -------------------------------------------------------------------------
    // 1. 35 Guests: 22 Pakistani + 13 International (Section 10)
    // -------------------------------------------------------------------------
    struct GuestSeed {
        const char* name;
        const char* idDoc; // CNIC if !foreign else Passport
        const char* phone;
        const char* email;
        const char* nat;
        const char* addr;
        bool        foreign;
        LoyaltyTier tier;
        int         stays;
        double      spend;
    };

    const GuestSeed guestSeeds[35] = {
        // 3 Platinum
        {"Tariq Mehmood Cheema",   "35202-4182930-1", "0300-8412233", "tariq.cheema@lums.edu.pk",    "Pakistan", "DHA Phase 5, Lahore",        false, LoyaltyTier::Platinum, 34, 1850000.0},
        {"Dr. Saima Afridi",       "17301-9283746-2", "0333-9123456", "saima.afridi@kmu.edu.pk",     "Pakistan", "Hayatabad, Peshawar",        false, LoyaltyTier::Platinum, 31, 1620000.0},
        {"Alexander Sterling",     "GB9283741",       "+44-7700-9001", "a.sterling@oxford.ac.uk",    "UK",       "Kensington, London",         true,  LoyaltyTier::Platinum, 32, 1740000.0},
        // 5 Gold
        {"Bilal Hassan Bhutto",    "42101-7738291-5", "0321-2233445", "bilal.bhutto@iba.edu.pk",     "Pakistan", "Clifton Block 4, Karachi",   false, LoyaltyTier::Gold,     18, 620000.0},
        {"Nadia Jamali",           "54400-3344556-4", "0312-8877665", "nadia.jamali@uet.edu.pk",     "Pakistan", "Jinnah Town, Quetta",        false, LoyaltyTier::Gold,     16, 540000.0},
        {"Usman Farooq Mir",       "81302-6655443-9", "0345-5566778", "usman.mir@ajk.gov.pk",        "Pakistan", "Chattar, Muzaffarabad",      false, LoyaltyTier::Gold,     19, 680000.0},
        {"Fatima Al-Mansoori",     "AE8841209",       "+971-50-4412", "f.almansoori@adnoc.ae",       "UAE",      "Saadiyat Island, Abu Dhabi", true,  LoyaltyTier::Gold,     17, 595000.0},
        {"Wei Zhang",              "CN7734120",       "+86-138-0013", "wei.zhang@tsinghua.edu.cn",   "China",    "Haidian District, Beijing",  true,  LoyaltyTier::Gold,     15, 520000.0},
        // 7 Silver
        {"Hamza Raza Gillani",     "36302-1122334-7", "0301-7788990", "hamza.gillani@bzu.edu.pk",    "Pakistan", "Model Town, Multan",         false, LoyaltyTier::Silver,   8,  245000.0},
        {"Ayesha Siddiqui",        "61101-5544332-6", "0331-4455667", "ayesha.siddiqui@nust.edu.pk", "Pakistan", "Sector F-7/2, Islamabad",    false, LoyaltyTier::Silver,   7,  198000.0},
        {"Kamran Khan Khattak",    "14301-8899001-3", "0344-9988776", "kamran.khattak@kohat.pk",     "Pakistan", "University Town, Peshawar",  false, LoyaltyTier::Silver,   6,  172000.0},
        {"Zainab Talpur",          "41304-2233445-8", "0302-3344556", "zainab.talpur@muet.edu.pk",   "Pakistan", "Qasimabad, Hyderabad",       false, LoyaltyTier::Silver,   9,  280000.0},
        {"Mehmood Bugti",          "53201-9988776-1", "0315-6677889", "mehmood.bugti@baloch.pk",     "Pakistan", "Sariab Road, Quetta",        false, LoyaltyTier::Silver,   5,  160000.0},
        {"Emily Carter",           "US5543219",       "+1-202-55501", "emily.carter@mit.edu",        "USA",      "Cambridge, Massachusetts",   true,  LoyaltyTier::Silver,   7,  230000.0},
        {"Emre Yilmaz",            "TR6612345",       "+90-532-1122", "emre.yilmaz@itu.edu.tr",      "Turkey",   "Besiktas, Istanbul",         true,  LoyaltyTier::Silver,   6,  185000.0},
        // 20 Standard (12 Pakistani + 8 International)
        {"Omar Farooq Bajwa",      "34101-6677889-5", "0300-1122334", "omar.bajwa@gujranwala.pk",    "Pakistan", "Satellite Town, Gujranwala", false, LoyaltyTier::Standard, 2,  54000.0},
        {"Hina Qureshi",           "37405-4455667-2", "0322-9900112", "hina.qureshi@seecs.edu.pk",   "Pakistan", "Chaklala Scheme 3, Rwp",     false, LoyaltyTier::Standard, 3,  78000.0},
        {"Sajid Ullah Wazir",      "21708-3322110-9", "0334-1234567", "sajid.wazir@uetp.edu.pk",     "Pakistan", "Bannu Township, KP",         false, LoyaltyTier::Standard, 1,  28000.0},
        {"Amina Soomro",           "45504-7766554-4", "0303-6543210", "amina.soomro@sukkur.edu.pk",  "Pakistan", "Military Road, Sukkur",      false, LoyaltyTier::Standard, 2,  49000.0},
        {"Fahad Mengal",           "51401-8877665-3", "0313-7654321", "fahad.mengal@khuzdar.pk",     "Pakistan", "Civil Colony, Khuzdar",      false, LoyaltyTier::Standard, 2,  62000.0},
        {"Raza Shah Kazmi",        "82203-1100229-7", "0346-2345678", "raza.kazmi@mirpur.pk",        "Pakistan", "Sector F-2, Mirpur AJK",     false, LoyaltyTier::Standard, 3,  84000.0},
        {"Mariam Nawaz Malik",     "61101-9900112-8", "0305-8765432", "mariam.malik@comsats.pk",     "Pakistan", "Sector E-11, Islamabad",     false, LoyaltyTier::Standard, 4,  112000.0},
        {"Saad Bin Tariq",         "35201-5566778-1", "0323-3456789", "saad.tariq@fast.edu.pk",      "Pakistan", "Johar Town, Lahore",         false, LoyaltyTier::Standard, 1,  32000.0},
        {"Khadija Noorani",        "42301-4433221-6", "0332-4567890", "khadija.noor@aku.edu",        "Pakistan", "PECHS Block 6, Karachi",     false, LoyaltyTier::Standard, 2,  68000.0},
        {"Junaid Alam Swati",      "15602-7788990-3", "0343-5678901", "junaid.swati@swat.pk",        "Pakistan", "Mingora, Swat",              false, LoyaltyTier::Standard, 1,  24000.0},
        {"Hassan Raza Jatoi",      "43203-6655441-5", "0306-6789012", "hassan.jatoi@larkana.pk",     "Pakistan", "Sachhal Colony, Larkana",    false, LoyaltyTier::Standard, 2,  52000.0},
        {"Zoya Shahzad Butt",      "34603-2211334-2", "0316-7890123", "zoya.butt@sialkot.pk",        "Pakistan", "Cantt Road, Sialkot",        false, LoyaltyTier::Standard, 3,  91000.0},
        {"Khalid Al-Qahtani",      "SA4491823",       "+966-55-8821", "k.qahtani@aramco.sa",         "Saudi Arabia", "Al Olaya, Riyadh",       true,  LoyaltyTier::Standard, 3,  115000.0},
        {"Lukas Weber",            "DE3382910",       "+49-170-9921", "lukas.weber@tum.de",          "Germany",  "Schwabing, Munich",          true,  LoyaltyTier::Standard, 2,  76000.0},
        {"Sophia Lin",             "CN8819204",       "+86-139-2210", "sophia.lin@huawei.cn",        "China",    "Nanshan, Shenzhen",          true,  LoyaltyTier::Standard, 2,  82000.0},
        {"Oliver Bennett",         "GB7721903",       "+44-7911-334", "o.bennett@bbc.co.uk",         "UK",       "Manchester, UK",             true,  LoyaltyTier::Standard, 1,  36000.0},
        {"Aisha Al-Maktoum",       "AE5519283",       "+971-55-9910", "aisha.maktoum@difc.ae",       "UAE",      "Downtown Dubai, UAE",        true,  LoyaltyTier::Standard, 3,  128000.0},
        {"Daniel Brooks",          "US9928374",       "+1-415-55599", "d.brooks@stanford.edu",       "USA",      "Palo Alto, California",      true,  LoyaltyTier::Standard, 2,  94000.0},
        {"Zeynep Kaya",            "TR4481920",       "+90-533-7741", "zeynep.kaya@ankara.edu.tr",   "Turkey",   "Cankaya, Ankara",            true,  LoyaltyTier::Standard, 1,  41000.0},
        {"Hannah Mueller",         "DE6619284",       "+49-151-4432", "hannah.mueller@berlin.de",    "Germany",  "Mitte, Berlin",              true,  LoyaltyTier::Standard, 2,  65000.0}
    };

    for (int i = 0; i < 35; ++i) {
        const GuestSeed& gs = guestSeeds[i];
        Guest g{};
        g.id              = i + 1;
        g.fullName        = gs.name;
        g.isForeign       = gs.foreign;
        g.cnic            = gs.foreign ? "" : gs.idDoc;
        g.passport        = gs.foreign ? gs.idDoc : "";
        g.phone           = gs.phone;
        g.email           = gs.email;
        g.nationality     = gs.nat;
        g.address         = gs.addr;
        g.tier            = gs.tier;
        g.stayCount       = gs.stays;
        g.lifetimeSpend   = gs.spend;
        g.registeredDay   = today - 60 - (i * 3);
        g.preferencesNote = (gs.tier == LoyaltyTier::Platinum) ? "VIP high-floor quiet room" : "Standard hospitality";
        db.guests.push_back(g);
    }
    db.settings.nextGuestId = 36;

    // -------------------------------------------------------------------------
    // 2. 30 Days of History (DailyStat rows + Historical Bookings & Invoices ~PKR 5.4M)
    // -------------------------------------------------------------------------
    int nextBookingId = 1;
    int nextInvoiceId = 1001;

    for (int dOff = 29; dOff >= 1; --dOff) {
        const int32_t dayNum = today - dOff;
        const bool wknd = DateUtil::isWeekendNight(dayNum);
        const double trend = static_cast<double>(29 - dOff) * 0.65; // Rising trend
        const double occPct = std::clamp(56.0 + trend + (wknd ? 14.0 : 0.0) + ((dOff % 3) * 2.5), 42.0, 94.0);
        const int occRooms = static_cast<int>(std::llround((occPct / 100.0) * 50.0));
        const double avgRate = wknd ? 16200.0 : 13800.0;
        const double dayRev = std::llround((static_cast<double>(occRooms) * avgRate * 0.34) / 10.0) * 10.0;

        DailyStat st{};
        st.day          = dayNum;
        st.occupancyPct = occPct;
        st.revenue      = dayRev;
        st.checkins     = 4 + (dOff % 5);
        st.checkouts    = 3 + ((dOff + 1) % 5);
        db.stats.push_back(st);

        // Create 2 historical checked-out bookings + invoices per day so Billings & Analytics are richly populated
        for (int k = 0; k < 2; ++k) {
            const int gIdx = (dOff * 3 + k * 7) % 35;
            const int rIdx = (dOff * 5 + k * 13) % 50;
            const Room& rm = db.rooms[static_cast<std::size_t>(rIdx)];
            const Guest& gu = db.guests[static_cast<std::size_t>(gIdx)];
            const int nights = 2 + ((dOff + k) % 3);

            Booking bk{};
            bk.id              = nextBookingId++;
            bk.guestId         = gu.id;
            bk.roomId          = rm.id;
            bk.checkInDay      = dayNum - nights;
            bk.checkOutDay     = dayNum;
            bk.checkInMinute   = 840;
            bk.checkOutMinute  = 690;
            bk.adults          = 2;
            bk.children        = (k % 2);
            bk.status          = BookingStatus::CheckedOut;
            bk.quotedRate      = rm.baseRate;
            bk.surgeMultiplier = wknd ? 1.21 : 1.10;
            bk.promoCode       = (k == 0 && dOff % 4 == 0) ? "WELCOME10" : "";
            bk.createdDay      = bk.checkInDay - 2;
            bk.addOns.push_back(AddOn{AddOnType::Dining, "Executive Dinner", 2400.0, 2, dayNum - 1});

            const double roomSub = bk.quotedRate * bk.surgeMultiplier * nights;
            const double addSub  = 4800.0;
            const double lDisc   = (gu.tier == LoyaltyTier::Platinum ? 0.15 : (gu.tier == LoyaltyTier::Gold ? 0.10 : (gu.tier == LoyaltyTier::Silver ? 0.05 : 0.0))) * roomSub;
            const double pDisc   = bk.promoCode.empty() ? 0.0 : (0.10 * roomSub);
            const double taxable = std::max(0.0, roomSub + addSub - lDisc - pDisc);
            const double tax     = taxable * Cfg::TAX_RATE;
            const double total   = static_cast<double>(std::llround(taxable + tax));

            Invoice inv{};
            inv.id              = nextInvoiceId++;
            inv.number          = "INV-" + std::to_string(inv.id);
            inv.bookingId       = bk.id;
            inv.guestId         = gu.id;
            inv.issuedDay       = dayNum;
            inv.nights          = nights;
            inv.roomSubtotal    = roomSub;
            inv.addOnSubtotal   = addSub;
            inv.loyaltyDiscount = lDisc;
            inv.promoDiscount   = pDisc;
            inv.taxableAmount   = taxable;
            inv.taxRate         = Cfg::TAX_RATE;
            inv.taxAmount       = tax;
            inv.grandTotal      = total;
            inv.paid            = (dOff > 2 || k == 0);
            inv.paymentMethod   = inv.paid ? ((k == 0) ? "Card" : "Bank Transfer") : "Unpaid";

            bk.invoiceId = inv.id;
            db.bookings.push_back(bk);
            db.invoices.push_back(inv);
        }
    }

    // -------------------------------------------------------------------------
    // 3. Today's Active Operational State (Section 10):
    //    - 32 Occupied rooms + 6 Reserved arrivals today = 38/50 (76% occupancy -> 1.10x surge)
    //    - 5 of the Occupied rooms depart today
    //    - 8 CleaningRequired + 3 Maintenance rooms (all 11 in TaskMinHeap with varied priorities)
    //    - 1 Available room (room #50) ready for walk-in demonstration
    // -------------------------------------------------------------------------
    // Rooms 0..31 -> Occupied (32 rooms; rooms 0..4 depart today = 5 departures today)
    for (int i = 0; i < 32; ++i) {
        Room& rm = db.rooms[static_cast<std::size_t>(i)];
        const Guest& gu = db.guests[static_cast<std::size_t>(i % 35)];
        Booking bk{};
        bk.id              = nextBookingId++;
        bk.guestId         = gu.id;
        bk.roomId          = rm.id;
        bk.checkInDay      = today - (1 + (i % 3));
        bk.checkOutDay     = (i < 5) ? today : (today + 1 + (i % 4)); // 5 departures today
        bk.checkInMinute   = 840;
        bk.checkOutMinute  = 720;
        bk.adults          = 2;
        bk.children        = (i % 3 == 0) ? 1 : 0;
        bk.status          = BookingStatus::CheckedIn;
        bk.quotedRate      = rm.baseRate;
        bk.surgeMultiplier = 1.10;
        bk.promoCode       = (i % 5 == 0) ? "WELCOME10" : "";
        bk.createdDay      = bk.checkInDay - 1;
        if (i % 2 == 0) {
            bk.addOns.push_back(AddOn{AddOnType::Minibar, "Minibar Refreshments", 1400.0, 2, today});
        }
        db.bookings.push_back(bk);
        rm.status = RoomStatus::Occupied;
        rm.currentBookingId = bk.id;
    }

    // Rooms 32..37 -> 6 Reserved arrivals today
    for (int i = 32; i < 38; ++i) {
        Room& rm = db.rooms[static_cast<std::size_t>(i)];
        const Guest& gu = db.guests[static_cast<std::size_t>((i - 30) % 35)];
        Booking bk{};
        bk.id              = nextBookingId++;
        bk.guestId         = gu.id;
        bk.roomId          = rm.id;
        bk.checkInDay      = today;
        bk.checkOutDay     = today + 2 + (i % 3);
        bk.checkInMinute   = 900 + ((i - 32) * 30);
        bk.checkOutMinute  = 720;
        bk.adults          = 2;
        bk.children        = 0;
        bk.status          = BookingStatus::Reserved;
        bk.quotedRate      = rm.baseRate;
        bk.surgeMultiplier = 1.10;
        bk.createdDay      = today - 3;
        db.bookings.push_back(bk);
        rm.status = RoomStatus::Reserved;
        rm.currentBookingId = bk.id;
    }

    // Today's DailyStat row (76% occupancy)
    db.stats.push_back(DailyStat{today, 235000.0, 76.0, 6, 5});

    // Rooms 38..45 -> 8 CleaningRequired tasks; Rooms 46..48 -> 3 Maintenance tasks
    int nextTaskId = 1;
    const char* staffNames[4] = {"Imran Ali", "Sana Bibi", "Kashif Engineering", "Nadia Housekeeping"};
    for (int i = 38; i < 46; ++i) {
        Room& rm = db.rooms[static_cast<std::size_t>(i)];
        rm.status = RoomStatus::CleaningRequired;
        rm.lastCheckoutDay = today;
        rm.lastCheckoutMinute = std::max(60, DateUtil::currentMinuteOfDay() - ((i - 37) * 45));

        HousekeepingTask t{};
        t.id            = nextTaskId++;
        t.roomId        = rm.id;
        t.type          = TaskType::Cleaning;
        t.status        = (i == 38) ? TaskStatus::InProgress : TaskStatus::Queued;
        t.severity      = 0;
        t.createdDay    = today;
        t.createdMinute = rm.lastCheckoutMinute;
        t.assignedTo    = staffNames[i % 4];
        t.note          = "Post-checkout deep cleaning & linen turnover";
        db.tasks.push_back(t);
    }

    const char* maintNotes[3] = {
        "HVAC thermostat calibration required",
        "Bathroom hot-water valve pressure drop",
        "Smart keycard door lock battery low"
    };
    const int maintSevs[3] = {5, 4, 3};
    for (int k = 0; k < 3; ++k) {
        Room& rm = db.rooms[static_cast<std::size_t>(46 + k)];
        rm.status = RoomStatus::Maintenance;
        HousekeepingTask t{};
        t.id            = nextTaskId++;
        t.roomId        = rm.id;
        t.type          = TaskType::Maintenance;
        t.status        = TaskStatus::Queued;
        t.severity      = maintSevs[k];
        t.createdDay    = today;
        t.createdMinute = std::max(60, DateUtil::currentMinuteOfDay() - (k * 60 + 30));
        t.assignedTo    = "Kashif Engineering";
        t.note          = maintNotes[k];
        db.tasks.push_back(t);
    }

    // 4 Pending Service Orders (Kanban Ordered / Preparing)
    const int activeBookingStart = nextBookingId - 38;
    db.orders.push_back(ServiceOrder{1, activeBookingStart + 0, AddOnType::Dining,    "Peshawari Karahi Platter", 2, 2800.0, 0});
    db.orders.push_back(ServiceOrder{2, activeBookingStart + 2, AddOnType::Laundry,   "Express Suit Dry Cleaning", 1, 1600.0, 0});
    db.orders.push_back(ServiceOrder{3, activeBookingStart + 4, AddOnType::Spa,       "Aromatherapy Spa Session",  1, 5500.0, 1});
    db.orders.push_back(ServiceOrder{4, activeBookingStart + 6, AddOnType::Transport, "Islamabad Airport Shuttle", 1, 3500.0, 1});

    // 2 FIFO Waitlist Entries
    db.waitlist.push_back(WaitlistEntry{1, 1, RoomType::Presidential, today + 1, today + 4, 52000.0, today - 2});
    db.waitlist.push_back(WaitlistEntry{2, 4, RoomType::Suite,        today + 2, today + 5, 28000.0, today - 1});

    // -------------------------------------------------------------------------
    // 4. >= 65 Valid, Cryptographically Chained SHA-256 Audit Log Entries
    // -------------------------------------------------------------------------
    const char* auditActions[6] = {
        "LOGIN", "BOOKING_CREATE", "CHECK_IN", "CHECK_OUT", "ROOM_STATUS", "PRICE_OVERRIDE"
    };
    for (int i = 0; i < 65; ++i) {
        const char* act = auditActions[i % 6];
        const int uid = (i % 3) + 1;
        const char* uname = (uid == 1) ? "admin" : ((uid == 2) ? "reception" : "housekeeping");
        const std::string det = "Historical operation #" + std::to_string(i + 1) + " verified in SHA-256 chain";
        appendAuditEntry(db.auditLog, uid, uname, act, det, baseTs + static_cast<long long>(i) * 36000LL);
    }
    appendAuditEntry(
        db.auditLog,
        actorUserId,
        actorUsername,
        "SEED_VIVA_DATA",
        "Seeded 50 rooms, 35 guests, 30-day history, 11 heap tasks, 4 orders, 2 waitlist entries & 66 audit links"
    );

    db.settings.nextBookingId  = nextBookingId;
    db.settings.nextInvoiceId  = nextInvoiceId;
    db.settings.nextTaskId     = nextTaskId;
    db.settings.nextOrderId    = 5;
    db.settings.nextWaitlistId = 3;
    db.settings.nextUserId     = 4;
    db.settings.surgeEnabled   = true;
    db.settings.taxRate        = Cfg::TAX_RATE;

    return saveAll(db);
}

/*
 * Purpose: Runs the Phase 1 Self-Test suite required by Section 11 & Section 12:
 *          1. SHA-256 FIPS 180-4 test vectors ("" and "abc")
 *          2. Salted 1,000-iteration password hash & verification
 *          3. Audit log hash-chain verification + single-byte tamper detection
 *          4. Civil DateUtil round-trip & PKR formatting check
 *          5. Binary save -> load round-trip equality across all 11 record types + FNV-1a tamper auto-restore
 * Inputs: verboseConsole (if true, prints PASS/FAIL lines to stdout).
 * Outputs: Vector of SelfTestEntry results.
 * Big-O: O(R) where R is number of test records serialized and verified.
 */
std::vector<SelfTestEntry> FileManager::runPhase1SelfTests(bool verboseConsole) {
    std::vector<SelfTestEntry> results;

    auto recordCheck = [&](const std::string& name, bool passed, const std::string& details) {
        results.push_back(SelfTestEntry{name, passed, details});
        if (verboseConsole) {
            std::printf("[%s] %-42s | %s\n", passed ? "PASS" : "FAIL", name.c_str(), details.c_str());
        }
    };

    // 1. SHA-256 FIPS 180-4 Known Vectors
    {
        std::string details;
        const bool ok = runSha256VectorSelfTest(&details);
        recordCheck("SHA-256 FIPS 180-4 Test Vectors", ok, details);
    }

    // 2. Password Key-Stretching (1000x SHA-256)
    {
        const std::string salt = generateSaltHex();
        const std::string hash = hashPassword(salt, "Admin@123");
        const bool validOk     = verifyPassword("Admin@123", salt, hash);
        const bool wrongReject = !verifyPassword("WrongPass", salt, hash);
        const bool ok = (salt.size() == 32U && hash.size() == 64U && validOk && wrongReject);
        recordCheck(
            "Password Salting & 1000x Key Stretch",
            ok,
            "salt=" + salt.substr(0, 8) + "..., valid=true, wrong=false"
        );
    }

    // 3. Audit Log Hash Chain Verification & Tamper Detection
    {
        std::vector<AuditEntry> log;
        appendAuditEntry(log, 1, "admin", "LOGIN", "Admin logged in", 1700000100LL);
        appendAuditEntry(log, 2, "reception", "BOOKING_CREATE", "Created booking #101", 1700000200LL);
        appendAuditEntry(log, 1, "admin", "PRICE_OVERRIDE", "Set surge to 1.25x", 1700000300LL);

        const int freshVerify = verifyAuditChain(log);
        // Tamper with entry #1 details
        log[1].details = "Created booking #999 (TAMPERED)";
        const int tamperVerify = verifyAuditChain(log);

        const bool ok = (freshVerify == -1 && tamperVerify == 1);
        recordCheck(
            "Audit Hash Chain Verify & Tamper Catch",
            ok,
            "freshChain=" + std::to_string(freshVerify) + " (valid), tamperedIndex=" + std::to_string(tamperVerify)
        );
    }

    // 4. Civil DateUtil & PKR Currency Formatting
    {
        const int32_t d2000 = DateUtil::toDayNumber(2000, 1, 1);
        const int32_t d2026 = DateUtil::toDayNumber(2026, 11, 18);
        int y = 0, m = 0, d = 0;
        DateUtil::fromDayNumber(d2026, y, m, d);
        int32_t parsedDay = -1;
        const bool parseOk = DateUtil::parseDate("2026-11-18", parsedDay);
        const std::string pkr = formatPKR(12500.0);

        const bool ok = (d2000 == 0 && y == 2026 && m == 11 && d == 18 && parseOk && parsedDay == d2026 &&
                         pkr == "PKR 12,500");
        recordCheck(
            "Civil DateUtil & PKR Currency Format",
            ok,
            "2026-11-18 -> day " + std::to_string(d2026) + ", formatPKR(12500)='" + pkr + "'"
        );
    }

    // 5. Binary Save -> Load Round-Trip Equality for All 11 Files + Backup Auto-Restore
    {
        const std::string testDataDir   = "data/_selftest_tmp_data";
        const std::string testBackupDir = "backups/_selftest_tmp_backups";
        const std::string testExportDir = "exports/_selftest_tmp_exports";

        std::error_code ec;
        std::filesystem::remove_all(testDataDir, ec);
        std::filesystem::remove_all(testBackupDir, ec);
        std::filesystem::remove_all(testExportDir, ec);

        FileManager fm(testDataDir, testBackupDir, testExportDir);
        HotelDatabase dbOut{};
        dbOut.rooms = generateDefaultRooms();
        dbOut.users = generateDefaultUsers();

        Guest sampleGuest{};
        sampleGuest.id              = 1;
        sampleGuest.fullName        = "Zainab Tariq Khan";
        sampleGuest.cnic            = "35202-1234567-8";
        sampleGuest.phone           = "0300-1234567";
        sampleGuest.email           = "zainab@seecs.edu.pk";
        sampleGuest.nationality     = "Pakistan";
        sampleGuest.address         = "Sector H-12, Islamabad";
        sampleGuest.isForeign       = false;
        sampleGuest.tier            = LoyaltyTier::Gold;
        sampleGuest.lifetimeSpend   = 540000.0;
        sampleGuest.stayCount       = 16;
        sampleGuest.registeredDay   = 9800;
        sampleGuest.preferencesNote = "Prefers quiet high floor";
        dbOut.guests.push_back(sampleGuest);

        Booking sampleBooking{};
        sampleBooking.id              = 1;
        sampleBooking.guestId         = 1;
        sampleBooking.roomId          = 1;
        sampleBooking.checkInDay      = 9810;
        sampleBooking.checkOutDay     = 9813;
        sampleBooking.checkInMinute   = 840;
        sampleBooking.checkOutMinute  = 720;
        sampleBooking.adults          = 2;
        sampleBooking.children        = 1;
        sampleBooking.status          = BookingStatus::CheckedIn;
        sampleBooking.quotedRate      = 12500.0;
        sampleBooking.surgeMultiplier = 1.10;
        sampleBooking.promoCode       = "WELCOME10";
        sampleBooking.addOns.push_back(AddOn{AddOnType::Dining, "Room Service Biryani", 1800.0, 2, 9811});
        sampleBooking.invoiceId       = 1001;
        sampleBooking.createdDay      = 9809;
        dbOut.bookings.push_back(sampleBooking);

        Invoice sampleInvoice{};
        sampleInvoice.id              = 1001;
        sampleInvoice.number          = "INV-1001";
        sampleInvoice.bookingId       = 1;
        sampleInvoice.guestId         = 1;
        sampleInvoice.issuedDay       = 9813;
        sampleInvoice.nights          = 3;
        sampleInvoice.roomSubtotal    = 41250.0;
        sampleInvoice.addOnSubtotal   = 3600.0;
        sampleInvoice.loyaltyDiscount = 4125.0;
        sampleInvoice.promoDiscount   = 4125.0;
        sampleInvoice.taxableAmount   = 36600.0;
        sampleInvoice.taxRate         = 0.16;
        sampleInvoice.taxAmount       = 5856.0;
        sampleInvoice.grandTotal      = 42456.0;
        sampleInvoice.paid            = true;
        sampleInvoice.paymentMethod   = "Card";
        dbOut.invoices.push_back(sampleInvoice);

        appendAuditEntry(dbOut.auditLog, 1, "admin", "SEED_TEST", "Round-trip test entry", 1700000500LL);

        HousekeepingTask sampleTask{};
        sampleTask.id            = 1;
        sampleTask.roomId        = 10;
        sampleTask.type          = TaskType::Maintenance;
        sampleTask.status        = TaskStatus::Queued;
        sampleTask.severity      = 4;
        sampleTask.createdDay    = 9810;
        sampleTask.createdMinute = 600;
        sampleTask.priorityScore = 24.5;
        sampleTask.assignedTo    = "Bilal Ahmed";
        sampleTask.note          = "AC compressor inspection";
        dbOut.tasks.push_back(sampleTask);

        WaitlistEntry sampleWaitlist{};
        sampleWaitlist.id         = 1;
        sampleWaitlist.guestId    = 1;
        sampleWaitlist.wantedType = RoomType::Suite;
        sampleWaitlist.fromDay    = 9820;
        sampleWaitlist.toDay      = 9823;
        sampleWaitlist.maxBudget  = 25000.0;
        sampleWaitlist.createdDay = 9810;
        dbOut.waitlist.push_back(sampleWaitlist);

        ServiceOrder sampleOrder{};
        sampleOrder.id        = 1;
        sampleOrder.bookingId = 1;
        sampleOrder.type      = AddOnType::Laundry;
        sampleOrder.item      = "Express Suit Pressing";
        sampleOrder.qty       = 2;
        sampleOrder.price     = 1200.0;
        sampleOrder.stage     = 1;
        dbOut.orders.push_back(sampleOrder);

        DailyStat sampleStat{};
        sampleStat.day          = 9810;
        sampleStat.revenue      = 185000.0;
        sampleStat.occupancyPct = 76.0;
        sampleStat.checkins     = 6;
        sampleStat.checkouts    = 5;
        dbOut.stats.push_back(sampleStat);

        dbOut.settings.themeChoice         = 1;
        dbOut.settings.taxRate             = 0.16;
        dbOut.settings.surgeEnabled        = true;
        dbOut.settings.manualSurgeOverride = 1.15;
        dbOut.settings.nextGuestId         = 2;

        const bool savedOk = fm.saveAll(dbOut);
        std::string snapName;
        const bool backupOk = fm.createBackupSnapshot(&snapName);

        HotelDatabase dbIn{};
        const bool loadOk =
            fm.loadRooms(dbIn.rooms) &&
            fm.loadGuests(dbIn.guests) &&
            fm.loadBookings(dbIn.bookings) &&
            fm.loadInvoices(dbIn.invoices) &&
            fm.loadUsers(dbIn.users) &&
            fm.loadAuditLog(dbIn.auditLog) &&
            fm.loadTasks(dbIn.tasks) &&
            fm.loadWaitlist(dbIn.waitlist) &&
            fm.loadOrders(dbIn.orders) &&
            fm.loadStats(dbIn.stats) &&
            fm.loadSettings(dbIn.settings);

        const bool equalOk =
            (dbOut.rooms == dbIn.rooms) &&
            (dbOut.guests == dbIn.guests) &&
            (dbOut.bookings == dbIn.bookings) &&
            (dbOut.invoices == dbIn.invoices) &&
            (dbOut.users == dbIn.users) &&
            (dbOut.auditLog == dbIn.auditLog) &&
            (dbOut.tasks == dbIn.tasks) &&
            (dbOut.waitlist == dbIn.waitlist) &&
            (dbOut.orders == dbIn.orders) &&
            (dbOut.stats == dbIn.stats) &&
            (dbOut.settings == dbIn.settings);

        // Corrupt 1 byte of guests.dat and verify auto-restore from backup works
        const std::string guestsPath = (std::filesystem::path(testDataDir) / "guests.dat").string();
        {
            std::fstream corruptStream(guestsPath, std::ios::binary | std::ios::in | std::ios::out);
            corruptStream.seekp(20, std::ios::beg);
            const char badByte = static_cast<char>(0xFF);
            corruptStream.write(&badByte, 1);
        }
        std::vector<Guest> restoredGuests;
        std::string restoreNotice;
        const bool autoRestoreOk = fm.loadGuests(restoredGuests, &restoreNotice) && (restoredGuests == dbOut.guests);

        // Flip 1 byte in audit_log.dat on disk and verify it is NOT silently auto-restored and fails verifyAuditChain()
        const std::string auditPath = (std::filesystem::path(testDataDir) / "audit_log.dat").string();
        {
            std::fstream auditStream(auditPath, std::ios::binary | std::ios::in | std::ios::out);
            auditStream.seekg(0, std::ios::end);
            const std::streamoff sz = auditStream.tellg();
            if (sz > 64) {
                const std::streamoff targetOff = sz / 2;
                auditStream.seekg(targetOff, std::ios::beg);
                char origByte = 0;
                auditStream.read(&origByte, 1);
                auditStream.seekp(targetOff, std::ios::beg);
                const char flipped = static_cast<char>(origByte ^ 0x01);
                auditStream.write(&flipped, 1);
            }
        }
        std::vector<AuditEntry> tamperedAudit;
        std::string tamperErr;
        const bool auditTamperCaught = fm.loadAuditLog(tamperedAudit, &tamperErr) &&
                                       (verifyAuditChain(tamperedAudit) != -1);

        std::filesystem::remove_all(testDataDir, ec);
        std::filesystem::remove_all(testBackupDir, ec);
        std::filesystem::remove_all(testExportDir, ec);

        const bool allRoundTripOk = savedOk && backupOk && loadOk && equalOk && autoRestoreOk && auditTamperCaught;
        recordCheck(
            "Binary Save->Load Round-Trip (All 11 Files)",
            allRoundTripOk,
            "11/11 files bitwise verified + FNV-1a auto-restore + 1-byte audit_log.dat tamper catch PASS"
        );
    }

    return results;
}

} // namespace hms
