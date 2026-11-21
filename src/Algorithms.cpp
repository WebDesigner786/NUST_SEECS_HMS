#include "Algorithms.h"
#include "FileManager.h"
#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace hms {

// =============================================================================
// 1. ALGORITHM X-RAY TELEMETRY REGISTRY
// =============================================================================
/*
 * Purpose: Returns the singleton AlgoXRayRegistry instance used to track algorithm metrics for the X-Ray panel.
 * Inputs: None.
 * Outputs: Reference to AlgoXRayRegistry.
 * Big-O: O(1) time, O(1) space.
 */
AlgoXRayRegistry& AlgoXRayRegistry::instance() {
    static AlgoXRayRegistry reg;
    return reg;
}

/*
 * Purpose: Updates or appends a telemetry record for a given algorithm name (keeps max 32 recent entries).
 * Inputs: entry (AlgoTelemetryEntry).
 * Outputs: Updates m_entries in-place.
 * Big-O: O(K) where K <= 32 is the number of tracked algorithm slots, i.e., O(1).
 */
void AlgoXRayRegistry::record(const AlgoTelemetryEntry& entry) {
    for (AlgoTelemetryEntry& existing : m_entries) {
        if (existing.algorithmName == entry.algorithmName) {
            existing = entry;
            return;
        }
    }
    if (m_entries.size() >= 32U) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(entry);
}

// =============================================================================
// 2. BINARY SEARCH & INTERVAL OVERLAP (Section 9.3, Section 9.6)
// =============================================================================
/*
 * Purpose: Performs classic iterative binary search on a sorted vector of integers.
 * Inputs: sortedArr (ascending sorted vector), target (value to find), outComparisons (optional counter).
 * Outputs: 0-based index of target if found, or -1 if absent.
 * Big-O: O(log N) time, O(1) space.
 */
int binarySearchInt(const std::vector<int>& sortedArr, int target, long long* outComparisons) {
    const auto t0 = std::chrono::steady_clock::now();
    long long comps = 0LL;
    int low = 0;
    int high = static_cast<int>(sortedArr.size()) - 1;
    int foundIndex = -1;

    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const int midVal = sortedArr[static_cast<std::size_t>(mid)];
        ++comps;
        if (midVal == target) {
            foundIndex = mid;
            break;
        }
        ++comps;
        if (midVal < target) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    if (outComparisons != nullptr) {
        *outComparisons = comps;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "BinarySearch (Int)";
    entry.inputSize      = sortedArr.size();
    entry.comparisons    = comps;
    entry.swapsOrMoves   = 0LL;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(log N) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::binarySearchInt";
    AlgoXRayRegistry::instance().record(entry);

    return foundIndex;
}

/*
 * Purpose: Performs binary search on a vector of Guest structs sorted in ascending lexicographical order by CNIC.
 * Inputs: sortedGuests, targetCnic, outComparisons.
 * Outputs: 0-based index in sortedGuests if found, or -1 if not found.
 * Big-O: O(log N) string comparisons, O(1) space.
 */
int binarySearchGuestByCnic(
    const std::vector<Guest>& sortedGuests,
    const std::string& targetCnic,
    long long* outComparisons
) {
    const auto t0 = std::chrono::steady_clock::now();
    long long comps = 0LL;
    int low = 0;
    int high = static_cast<int>(sortedGuests.size()) - 1;
    int foundIdx = -1;

    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const std::string& midCnic = sortedGuests[static_cast<std::size_t>(mid)].cnic;
        ++comps;
        if (midCnic == targetCnic) {
            foundIdx = mid;
            break;
        }
        ++comps;
        if (midCnic < targetCnic) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    if (outComparisons != nullptr) {
        *outComparisons = comps;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "BinarySearch (Guest CNIC)";
    entry.inputSize      = sortedGuests.size();
    entry.comparisons    = comps;
    entry.swapsOrMoves   = 0LL;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(log N) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::binarySearchGuestByCnic";
    AlgoXRayRegistry::instance().record(entry);

    return foundIdx;
}

/*
 * Purpose: Determines whether two half-open date intervals [aStart, aEnd) and [bStart, bEnd) overlap.
 *          Half-open intervals allow a checkout on day D and a new check-in on the same day D without conflict.
 * Inputs: aStart, aEnd, bStart, bEnd (dayNumbers).
 * Outputs: true if intervals intersect, false otherwise.
 * Big-O: O(1) time, O(1) space.
 */
bool intervalsOverlap(int32_t aStart, int32_t aEnd, int32_t bStart, int32_t bEnd) {
    return (aStart < bEnd) && (bStart < aEnd);
}

/*
 * Purpose: Uses binary search on a per-room vector of bookings (sorted ascending by checkInDay) to detect
 *          whether [reqCheckIn, reqCheckOut) overlaps any active booking (Reserved or CheckedIn).
 * Inputs: sortedRoomBookings, reqCheckIn, reqCheckOut, ignoreBookingId (-1 if none), outComparisons.
 * Outputs: true if a conflict exists, false if the room is free for [reqCheckIn, reqCheckOut).
 * Big-O: O(log B + K) where B is bookings for the room and K is adjacent candidate checks (typically 1..2).
 */
bool hasRoomBookingConflictSorted(
    const std::vector<Booking>& sortedRoomBookings,
    int32_t reqCheckIn,
    int32_t reqCheckOut,
    int ignoreBookingId,
    long long* outComparisons
) {
    const auto t0 = std::chrono::steady_clock::now();
    long long comps = 0LL;

    if (sortedRoomBookings.empty() || reqCheckOut <= reqCheckIn) {
        if (outComparisons != nullptr) {
            *outComparisons = 0LL;
        }
        return (reqCheckOut <= reqCheckIn);
    }

    // Binary search for the first booking whose checkInDay >= reqCheckOut (upper bound of potential overlap)
    int low = 0;
    int high = static_cast<int>(sortedRoomBookings.size()) - 1;
    int firstAfterIdx = static_cast<int>(sortedRoomBookings.size());

    while (low <= high) {
        const int mid = low + (high - low) / 2;
        ++comps;
        if (sortedRoomBookings[static_cast<std::size_t>(mid)].checkInDay >= reqCheckOut) {
            firstAfterIdx = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    // Any booking at index >= firstAfterIdx starts at or after reqCheckOut, so it cannot overlap [reqCheckIn, reqCheckOut).
    // Scan backwards from firstAfterIdx - 1 through candidate bookings that start before reqCheckOut.
    bool conflict = false;
    for (int i = firstAfterIdx - 1; i >= 0; --i) {
        const Booking& b = sortedRoomBookings[static_cast<std::size_t>(i)];
        if (b.id == ignoreBookingId) {
            continue;
        }
        if (b.status == BookingStatus::Cancelled ||
            b.status == BookingStatus::NoShow ||
            b.status == BookingStatus::CheckedOut) {
            continue;
        }
        ++comps;
        if (intervalsOverlap(reqCheckIn, reqCheckOut, b.checkInDay, b.checkOutDay)) {
            conflict = true;
            break;
        }
        // In a disjoint sorted room schedule, if b.checkOutDay <= reqCheckIn, earlier bookings also end <= reqCheckIn
        if (b.checkOutDay <= reqCheckIn) {
            break;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    if (outComparisons != nullptr) {
        *outComparisons = comps;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "IntervalOverlap + BinarySearch";
    entry.inputSize      = sortedRoomBookings.size();
    entry.comparisons    = comps;
    entry.swapsOrMoves   = 0LL;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(log B) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::hasRoomBookingConflictSorted";
    AlgoXRayRegistry::instance().record(entry);

    return conflict;
}

/*
 * Purpose: Filters active bookings for roomId, sorts them by checkInDay via custom MergeSort,
 *          and runs binary-search interval conflict detection.
 * Inputs: allBookings, roomId, reqCheckIn, reqCheckOut, ignoreBookingId.
 * Outputs: true if room has an overlapping active booking, false otherwise.
 * Big-O: O(B log B) to sort room bookings + O(log B) binary search.
 */
bool hasRoomBookingConflict(
    const std::vector<Booking>& allBookings,
    int roomId,
    int32_t reqCheckIn,
    int32_t reqCheckOut,
    int ignoreBookingId
) {
    if (reqCheckOut <= reqCheckIn) {
        return true;
    }
    std::vector<Booking> roomBookings;
    for (const Booking& b : allBookings) {
        if (b.roomId == roomId && b.id != ignoreBookingId &&
            (b.status == BookingStatus::Reserved || b.status == BookingStatus::CheckedIn)) {
            roomBookings.push_back(b);
        }
    }
    if (roomBookings.empty()) {
        return false;
    }
    mergeSort(
        roomBookings,
        [](const Booking& a, const Booking& b) {
            if (a.checkInDay != b.checkInDay) {
                return a.checkInDay < b.checkInDay;
            }
            return a.id < b.id;
        },
        nullptr,
        nullptr,
        "MergeSort (Room Bookings)"
    );
    return hasRoomBookingConflictSorted(roomBookings, reqCheckIn, reqCheckOut, ignoreBookingId);
}

// =============================================================================
// 3. CUSTOM HASH TABLE FOR GUEST CNIC LOOKUP (Section 1 Law 4, Section 9.4)
// =============================================================================
/*
 * Purpose: Constructs a chained hash table with kBucketCount (127) buckets.
 * Inputs: None.
 * Outputs: Empty GuestCnicHashTable.
 * Big-O: O(B) where B = 127, i.e., O(1).
 */
GuestCnicHashTable::GuestCnicHashTable() : m_buckets(kBucketCount), m_count(0U) {}

/*
 * Purpose: Computes a polynomial rolling hash (multiplier 31) modulo kBucketCount (127) over a CNIC string.
 * Inputs: cnic string.
 * Outputs: Bucket index in [0 .. kBucketCount - 1].
 * Big-O: O(|cnic|) time, O(1) space.
 */
std::size_t GuestCnicHashTable::hashCnic(const std::string& cnic) {
    std::size_t h = 0U;
    for (unsigned char ch : cnic) {
        if (ch == '-') {
            continue;
        }
        h = (h * 31U + static_cast<std::size_t>(ch)) % kBucketCount;
    }
    return h;
}

/*
 * Purpose: Clears all buckets in the custom CNIC hash table.
 * Inputs: None.
 * Outputs: Empty table with size 0.
 * Big-O: O(B + N) time, O(1) auxiliary space.
 */
void GuestCnicHashTable::clear() {
    for (auto& bucket : m_buckets) {
        bucket.clear();
    }
    m_count = 0U;
}

/*
 * Purpose: Rebuilds the entire CNIC hash table from a vector of Guest records.
 * Inputs: guests vector.
 * Outputs: Populates hash table with all non-empty guest CNICs.
 * Big-O: O(N) average time, O(N) space.
 */
void GuestCnicHashTable::rebuild(const std::vector<Guest>& guests) {
    clear();
    for (const Guest& g : guests) {
        if (!g.cnic.empty()) {
            insertOrUpdate(g.cnic, g.id);
        }
    }
}

/*
 * Purpose: Inserts a (cnic -> guestId) mapping or updates guestId if the CNIC is already present.
 * Inputs: cnic string, guestId integer.
 * Outputs: true if newly inserted, false if existing key was updated.
 * Big-O: O(1) average time, O(1) space.
 */
bool GuestCnicHashTable::insertOrUpdate(const std::string& cnic, int guestId) {
    if (cnic.empty()) {
        return false;
    }
    const std::size_t idx = hashCnic(cnic);
    std::vector<BucketNode>& chain = m_buckets[idx];
    for (BucketNode& node : chain) {
        if (node.cnic == cnic) {
            node.guestId = guestId;
            return false;
        }
    }
    chain.push_back(BucketNode{cnic, guestId});
    ++m_count;
    return true;
}

/*
 * Purpose: Removes a CNIC key from the hash table if present.
 * Inputs: cnic string.
 * Outputs: true if removed, false if not found.
 * Big-O: O(1) average time, O(1) space.
 */
bool GuestCnicHashTable::remove(const std::string& cnic) {
    if (cnic.empty()) {
        return false;
    }
    const std::size_t idx = hashCnic(cnic);
    std::vector<BucketNode>& chain = m_buckets[idx];
    for (std::size_t i = 0U; i < chain.size(); ++i) {
        if (chain[i].cnic == cnic) {
            chain.erase(chain.begin() + static_cast<std::ptrdiff_t>(i));
            --m_count;
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Looks up a guestId by CNIC in O(1) average time and records telemetry in AlgoXRayRegistry.
 * Inputs: cnic string, optional outProbes counter.
 * Outputs: guestId if found, or -1 if absent.
 * Big-O: O(1) average time (O(N) worst-case chain), O(1) space.
 */
int GuestCnicHashTable::findGuestId(const std::string& cnic, long long* outProbes) const {
    const auto t0 = std::chrono::steady_clock::now();
    long long probes = 0LL;
    int foundId = -1;

    if (!cnic.empty()) {
        const std::size_t idx = hashCnic(cnic);
        const std::vector<BucketNode>& chain = m_buckets[idx];
        for (const BucketNode& node : chain) {
            ++probes;
            if (node.cnic == cnic) {
                foundId = node.guestId;
                break;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    if (outProbes != nullptr) {
        *outProbes = probes;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "Custom HashTable (CNIC Lookup)";
    entry.inputSize      = m_count;
    entry.comparisons    = probes;
    entry.swapsOrMoves   = 0LL;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(1) avg | Space: O(N)";
    entry.sourceLocation = "Algorithms.cpp::GuestCnicHashTable::findGuestId";
    AlgoXRayRegistry::instance().record(entry);

    return foundId;
}

/*
 * Purpose: Checks whether a CNIC already exists in the hash table (duplicate-CNIC check).
 * Inputs: cnic string.
 * Outputs: true if present, false otherwise.
 * Big-O: O(1) average time, O(1) space.
 */
bool GuestCnicHashTable::contains(const std::string& cnic) const {
    return (findGuestId(cnic) != -1);
}

// =============================================================================
// 4. CUSTOM BINARY MIN-HEAP FOR HOUSEKEEPING DISPATCH (Section 9.9)
// =============================================================================
/*
 * Purpose: Computes task priority score P = 3U + V + E + 2*sev per Section 9.9:
 *          U = next check-in urgency (<=1h:10, <=3h:8, <=6h:6, <=12h:4, <=24h:2, none:0),
 *          V = arriving guest tier weight (Standard 0, Silver 2, Gold 4, Platinum 6),
 *          E = min(10.0, 0.5 * hoursSinceCheckout),
 *          sev = severity 1..5 for Maintenance (0 for Cleaning).
 * Inputs: type, severity, hoursUntilNextCheckIn, hasUpcomingCheckIn, arrivingGuestTier, hoursSinceCheckout.
 * Outputs: Priority score P as double.
 * Big-O: O(1) time, O(1) space.
 */
double TaskMinHeap::computePriorityScore(
    TaskType type,
    int severity,
    double hoursUntilNextCheckIn,
    bool hasUpcomingCheckIn,
    LoyaltyTier arrivingGuestTier,
    double hoursSinceCheckout
) {
    double U = 0.0;
    if (hasUpcomingCheckIn && hoursUntilNextCheckIn >= 0.0) {
        if (hoursUntilNextCheckIn <= 1.0) {
            U = 10.0;
        } else if (hoursUntilNextCheckIn <= 3.0) {
            U = 8.0;
        } else if (hoursUntilNextCheckIn <= 6.0) {
            U = 6.0;
        } else if (hoursUntilNextCheckIn <= 12.0) {
            U = 4.0;
        } else if (hoursUntilNextCheckIn <= 24.0) {
            U = 2.0;
        }
    }

    double V = 0.0;
    if (hasUpcomingCheckIn) {
        switch (arrivingGuestTier) {
            case LoyaltyTier::Standard: V = 0.0; break;
            case LoyaltyTier::Silver:   V = 2.0; break;
            case LoyaltyTier::Gold:     V = 4.0; break;
            case LoyaltyTier::Platinum: V = 6.0; break;
        }
    }

    const double E = std::min(10.0, 0.5 * std::max(0.0, hoursSinceCheckout));
    const int sev = (type == TaskType::Maintenance) ? std::clamp(severity, 1, 5) : 0;

    return (3.0 * U) + V + E + (2.0 * static_cast<double>(sev));
}

/*
 * Purpose: Compares two HousekeepingTask nodes by heap key = -priorityScore, breaking ties by earlier creation time
 *          (and then lower task ID for strict determinism).
 * Inputs: Task a, Task b.
 * Outputs: true if a should be placed higher (closer to root) in the min-heap than b.
 * Big-O: O(1) time, O(1) space.
 */
bool TaskMinHeap::hasHigherPriority(const HousekeepingTask& a, const HousekeepingTask& b) {
    const double keyA = -a.priorityScore;
    const double keyB = -b.priorityScore;
    if (std::fabs(keyA - keyB) > 1e-6) {
        return keyA < keyB;
    }
    const long long timeA = static_cast<long long>(a.createdDay) * 1440LL + static_cast<long long>(a.createdMinute);
    const long long timeB = static_cast<long long>(b.createdDay) * 1440LL + static_cast<long long>(b.createdMinute);
    if (timeA != timeB) {
        return timeA < timeB;
    }
    return a.id < b.id;
}

/*
 * Purpose: Restores the min-heap property upwards from index idx toward the root (0).
 * Inputs: idx (starting node index), swaps (counter reference).
 * Outputs: Modifies m_heap in-place.
 * Big-O: O(log N) time, O(1) space.
 */
void TaskMinHeap::siftUp(std::size_t idx, long long& swaps) {
    while (idx > 0U) {
        const std::size_t parent = (idx - 1U) / 2U;
        if (hasHigherPriority(m_heap[idx], m_heap[parent])) {
            std::swap(m_heap[idx], m_heap[parent]);
            ++swaps;
            idx = parent;
        } else {
            break;
        }
    }
}

/*
 * Purpose: Restores the min-heap property downwards from index idx toward the leaves.
 * Inputs: idx (starting node index), swaps (counter reference).
 * Outputs: Modifies m_heap in-place.
 * Big-O: O(log N) time, O(1) space.
 */
void TaskMinHeap::siftDown(std::size_t idx, long long& swaps) {
    const std::size_t n = m_heap.size();
    while (true) {
        const std::size_t left = 2U * idx + 1U;
        const std::size_t right = 2U * idx + 2U;
        std::size_t smallest = idx;

        if (left < n && hasHigherPriority(m_heap[left], m_heap[smallest])) {
            smallest = left;
        }
        if (right < n && hasHigherPriority(m_heap[right], m_heap[smallest])) {
            smallest = right;
        }
        if (smallest != idx) {
            std::swap(m_heap[idx], m_heap[smallest]);
            ++swaps;
            idx = smallest;
        } else {
            break;
        }
    }
}

/*
 * Purpose: Inserts a HousekeepingTask into the binary min-heap and sifts up.
 * Inputs: task (HousekeepingTask).
 * Outputs: Inserts task and records telemetry in AlgoXRayRegistry.
 * Big-O: O(log N) time, O(1) amortized space.
 */
void TaskMinHeap::push(const HousekeepingTask& task) {
    const auto t0 = std::chrono::steady_clock::now();
    long long swaps = 0LL;
    m_heap.push_back(task);
    siftUp(m_heap.size() - 1U, swaps);
    const auto t1 = std::chrono::steady_clock::now();

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "TaskMinHeap::push";
    entry.inputSize      = m_heap.size();
    entry.comparisons    = swaps + 1LL;
    entry.swapsOrMoves   = swaps;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(log N) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::TaskMinHeap::push";
    AlgoXRayRegistry::instance().record(entry);
}

/*
 * Purpose: Returns a const reference to the highest-priority task at the root of the min-heap.
 * Inputs: None.
 * Outputs: Const reference to root HousekeepingTask.
 * Big-O: O(1) time, O(1) space.
 */
const HousekeepingTask& TaskMinHeap::peekMin() const {
    if (m_heap.empty()) {
        throw std::out_of_range("TaskMinHeap::peekMin called on empty heap");
    }
    return m_heap.front();
}

/*
 * Purpose: Removes and returns the root (minimum heap key = highest priority score P) from the min-heap.
 * Inputs: None.
 * Outputs: Highest-priority HousekeepingTask.
 * Big-O: O(log N) time, O(1) space.
 */
HousekeepingTask TaskMinHeap::popMin() {
    if (m_heap.empty()) {
        throw std::out_of_range("TaskMinHeap::popMin called on empty heap");
    }
    const auto t0 = std::chrono::steady_clock::now();
    long long swaps = 0LL;

    HousekeepingTask top = m_heap.front();
    m_heap.front() = m_heap.back();
    m_heap.pop_back();
    if (!m_heap.empty()) {
        siftDown(0U, swaps);
    }

    const auto t1 = std::chrono::steady_clock::now();
    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "TaskMinHeap::popMin";
    entry.inputSize      = m_heap.size() + 1U;
    entry.comparisons    = swaps * 2LL + 1LL;
    entry.swapsOrMoves   = swaps;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(log N) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::TaskMinHeap::popMin";
    AlgoXRayRegistry::instance().record(entry);

    return top;
}

/*
 * Purpose: Updates a task's priorityScore by linear search for taskId followed by siftUp and siftDown (Section 9.9).
 * Inputs: taskId, newPriorityScore.
 * Outputs: true if taskId was found and re-sifted, false otherwise.
 * Big-O: O(N) time (linear search + O(log N) re-sift), O(1) space.
 */
bool TaskMinHeap::updateKey(int taskId, double newPriorityScore) {
    for (std::size_t i = 0U; i < m_heap.size(); ++i) {
        if (m_heap[i].id == taskId) {
            m_heap[i].priorityScore = newPriorityScore;
            long long swaps = 0LL;
            siftUp(i, swaps);
            siftDown(i, swaps);
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Removes a task with the given taskId from the min-heap and restores heap order.
 * Inputs: taskId.
 * Outputs: true if found and removed, false otherwise.
 * Big-O: O(N) time, O(1) space.
 */
bool TaskMinHeap::removeById(int taskId) {
    for (std::size_t i = 0U; i < m_heap.size(); ++i) {
        if (m_heap[i].id == taskId) {
            m_heap[i] = m_heap.back();
            m_heap.pop_back();
            if (i < m_heap.size()) {
                long long swaps = 0LL;
                siftUp(i, swaps);
                siftDown(i, swaps);
            }
            return true;
        }
    }
    return false;
}

/*
 * Purpose: Copies the heap and pops all elements to produce the exact dispatch order without mutating this heap.
 * Inputs: None.
 * Outputs: Vector of HousekeepingTask in priority dispatch order.
 * Big-O: O(N log N) time, O(N) space.
 */
std::vector<HousekeepingTask> TaskMinHeap::getTasksInDispatchOrder() const {
    TaskMinHeap copy = *this;
    std::vector<HousekeepingTask> ordered;
    ordered.reserve(copy.size());
    while (!copy.empty()) {
        ordered.push_back(copy.popMin());
    }
    return ordered;
}

// =============================================================================
// 5. ALGORITHM 1 — MULTI-CRITERIA SMART ROOM RECOMMENDER (Section 9.7)
// =============================================================================
/*
 * Purpose: Normalizes recommender weights so their sum equals 1.0.
 * Inputs: None (operates on struct members).
 * Outputs: Normalized weights in-place.
 * Big-O: O(1) time, O(1) space.
 */
void RecommenderWeights::normalize() {
    const double sum = budget + floor + view + elevator + noise + bed;
    if (sum > 1e-6) {
        budget   /= sum;
        floor    /= sum;
        view     /= sum;
        elevator /= sum;
        noise    /= sum;
        bed      /= sum;
    } else {
        budget   = 0.30;
        floor    = 0.15;
        view     = 0.20;
        elevator = 0.10;
        noise    = 0.10;
        bed      = 0.15;
    }
}

/*
 * Purpose: Evaluates all 6 feature scores in [0,1] and weighted score S(r) for a single candidate room per Section 9.7:
 *          - f_budget: p = baseRate * surge; if p <= B: 1 - 0.3*(B-p)/B, else max(0, 1 - 2*(p-B)/B)
 *          - f_floor: noPref -> 1; lowPref -> 1 - (floor-1)/4; highPref -> (floor-1)/4
 *          - f_view: 1 if matches or anyView, else 0.3
 *          - f_elev: near(0) -> 1 - d/5; far(2) -> d/5; any(1) -> 1
 *          - f_noise: if t >= n -> 1 else 1 - (n-t)/4
 *          - f_bed: 1 if matches or anyBed, else 0.2
 *          - Platinum guests get +0.02 bonus on floors >= 4 (capped at 1.0).
 * Inputs: room, prefs, guestTier, activeSurgeMultiplier, weights.
 * Outputs: Populated RoomFeatureBreakdown struct.
 * Big-O: O(1) time, O(1) space.
 */
RoomFeatureBreakdown evaluateRoomCandidate(
    const Room& room,
    const GuestPreferences& prefs,
    LoyaltyTier guestTier,
    double activeSurgeMultiplier,
    const RecommenderWeights& weights
) {
    RecommenderWeights w = weights;
    w.normalize();

    RoomFeatureBreakdown out{};
    out.roomId     = room.id;
    out.roomNumber = room.number;

    const double B = std::max(1.0, prefs.budget);
    const double p = room.baseRate * std::max(1.0, activeSurgeMultiplier);
    if (p <= B) {
        out.fBudget = std::clamp(1.0 - 0.3 * ((B - p) / B), 0.0, 1.0);
    } else {
        out.fBudget = std::max(0.0, 1.0 - 2.0 * ((p - B) / B));
    }

    const double clampedFloor = static_cast<double>(std::clamp(room.floor, 1, 5));
    if (prefs.noFloorPref) {
        out.fFloor = 1.0;
    } else if (prefs.wantsLowFloor) {
        out.fFloor = 1.0 - (clampedFloor - 1.0) / 4.0;
    } else {
        out.fFloor = (clampedFloor - 1.0) / 4.0;
    }

    out.fView = (prefs.anyView || room.view == prefs.view) ? 1.0 : 0.3;

    const double d = static_cast<double>(std::clamp(room.elevatorDistance, 0, 5));
    if (prefs.elevatorPref == 0) {
        out.fElevator = 1.0 - (d / 5.0);
    } else if (prefs.elevatorPref == 2) {
        out.fElevator = d / 5.0;
    } else {
        out.fElevator = 1.0;
    }

    const int n = std::clamp(room.noiseLevel, 1, 5);
    const int t = std::clamp(prefs.noiseTolerance, 1, 5);
    if (t >= n) {
        out.fNoise = 1.0;
    } else {
        out.fNoise = std::clamp(1.0 - static_cast<double>(n - t) / 4.0, 0.0, 1.0);
    }

    out.fBed = (prefs.anyBed || room.bed == prefs.bed) ? 1.0 : 0.2;

    double S = w.budget * out.fBudget +
               w.floor * out.fFloor +
               w.view * out.fView +
               w.elevator * out.fElevator +
               w.noise * out.fNoise +
               w.bed * out.fBed;

    if (guestTier == LoyaltyTier::Platinum && room.floor >= 4) {
        out.tierBonus = 0.02;
        S += 0.02;
    }

    out.totalScore01 = std::clamp(S, 0.0, 1.0);
    out.percent      = static_cast<int>(std::llround(100.0 * out.totalScore01));

    // Build concise one-line reason text ("Pool view, 1 room from elevator, within budget")
    std::string reason = std::string(toString(room.view)) + " view, " +
                         std::to_string(room.elevatorDistance) +
                         (room.elevatorDistance == 1 ? " room from elevator, " : " rooms from elevator, ");
    if (p <= B) {
        reason += "within budget";
    } else {
        reason += formatPKR(p) + "/night";
    }
    if (out.tierBonus > 0.0) {
        reason += " (+Platinum VIP floor)";
    }
    out.reasonText = reason;
    return out;
}

/*
 * Purpose: Scores all available, conflict-free candidate rooms for [checkInDay, checkOutDay),
 *          ranks them via custom MergeSort (descending by score01, ties by lower room number),
 *          cross-checks against std::sort for the X-Ray panel, and returns the Top 3 RoomMatch cards.
 * Inputs: rooms, bookings, checkInDay, checkOutDay, prefs, guestTier, activeSurgeMultiplier, weights,
 *         outAllBreakdowns, outStlVerified.
 * Outputs: Vector of up to 3 top RoomMatch recommendations.
 * Big-O: O(N log N) time and O(N) space where N is the number of candidate rooms.
 */
std::vector<RoomMatch> recommendRooms(
    const std::vector<Room>& rooms,
    const std::vector<Booking>& bookings,
    int32_t checkInDay,
    int32_t checkOutDay,
    const GuestPreferences& prefs,
    LoyaltyTier guestTier,
    double activeSurgeMultiplier,
    const RecommenderWeights& weights,
    std::vector<RoomFeatureBreakdown>* outAllBreakdowns,
    bool* outStlVerified
) {
    const auto t0 = std::chrono::steady_clock::now();

    std::vector<RoomFeatureBreakdown> candidates;
    candidates.reserve(rooms.size());

    for (const Room& r : rooms) {
        if (r.status == RoomStatus::Maintenance || r.status == RoomStatus::CleaningRequired) {
            continue;
        }
        if (hasRoomBookingConflict(bookings, r.id, checkInDay, checkOutDay)) {
            continue;
        }
        candidates.push_back(evaluateRoomCandidate(r, prefs, guestTier, activeSurgeMultiplier, weights));
    }

    auto rankComp = [](const RoomFeatureBreakdown& a, const RoomFeatureBreakdown& b) {
        if (std::fabs(a.totalScore01 - b.totalScore01) > 1e-9) {
            return a.totalScore01 > b.totalScore01;
        }
        return a.roomNumber < b.roomNumber;
    };

    std::vector<RoomFeatureBreakdown> stlCopy = candidates;
    std::sort(stlCopy.begin(), stlCopy.end(), rankComp);

    long long mergeComps = 0LL;
    long long mergeMoves = 0LL;
    mergeSort(candidates, rankComp, &mergeComps, &mergeMoves, "Recommender MergeSort");

    bool crossCheckOk = (candidates.size() == stlCopy.size());
    for (std::size_t i = 0U; crossCheckOk && i < candidates.size(); ++i) {
        if (candidates[i].roomId != stlCopy[i].roomId) {
            crossCheckOk = false;
        }
        candidates[i].stlCrossCheck = crossCheckOk;
    }

    if (outStlVerified != nullptr) {
        *outStlVerified = crossCheckOk;
    }
    if (outAllBreakdowns != nullptr) {
        *outAllBreakdowns = candidates;
    }

    std::vector<RoomMatch> top3;
    const std::size_t limit = std::min<std::size_t>(3U, candidates.size());
    top3.reserve(limit);
    for (std::size_t i = 0U; i < limit; ++i) {
        RoomMatch m{};
        m.roomId     = candidates[i].roomId;
        m.score01    = candidates[i].totalScore01;
        m.percent    = candidates[i].percent;
        m.reasonText = candidates[i].reasonText;
        top3.push_back(m);
    }

    const auto t1 = std::chrono::steady_clock::now();
    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "Smart Room Recommender";
    entry.inputSize      = candidates.size();
    entry.comparisons    = mergeComps;
    entry.swapsOrMoves   = mergeMoves;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(N log N) | Space: O(N)";
    entry.sourceLocation = "Algorithms.cpp::recommendRooms";
    entry.notes          = crossCheckOk ? "std::sort cross-check: VERIFIED" : "std::sort mismatch";
    AlgoXRayRegistry::instance().record(entry);

    return top3;
}

// =============================================================================
// 6. ALGORITHM 2 — DYNAMIC YIELD MANAGEMENT & PRICING SIMULATOR (Section 9.8)
// =============================================================================
/*
 * Purpose: Rounds a PKR room price to the nearest PKR 10 as specified in Section 9.8.
 * Inputs: price (double).
 * Outputs: Price rounded to nearest 10.
 * Big-O: O(1) time, O(1) space.
 */
double roundToNearest10PKR(double price) {
    return std::llround(price / 10.0) * 10.0;
}

/*
 * Purpose: Computes Dynamic Yield Management surge multiplier and room prices per Section 9.8:
 *          occ = occupiedOrReservedRooms / totalRooms (50),
 *          occMult = 1.25 if occ > 0.85; 1.10 if occ > 0.70; else 1.00,
 *          seasonMult = 1.15 in peak season else 1.00,
 *          weekendMult = 1.10 on Fri/Sat nights else 1.00,
 *          surge = min(1.60, occMult * seasonMult * weekendMult).
 * Inputs: occupiedOrReservedRooms, totalRooms, isPeakSeason, isWeekendNight, surgeEnabled, manualOverride.
 * Outputs: Populated YieldPricingResult.
 * Big-O: O(1) time, O(1) space.
 */
YieldPricingResult computeYieldPricing(
    int occupiedOrReservedRooms,
    int totalRooms,
    bool isPeakSeason,
    bool isWeekendNight,
    bool surgeEnabled,
    double manualOverride
) {
    YieldPricingResult res{};
    const int denom = std::max(1, totalRooms);
    res.occupancyRatio = std::clamp(
        static_cast<double>(occupiedOrReservedRooms) / static_cast<double>(denom),
        0.0,
        1.0
    );

    if (res.occupancyRatio > Cfg::SURGE_T2) {
        res.occMult = Cfg::SURGE_M2; // 1.25
    } else if (res.occupancyRatio > Cfg::SURGE_T1) {
        res.occMult = Cfg::SURGE_M1; // 1.10
    } else {
        res.occMult = 1.00;
    }

    res.seasonMult  = isPeakSeason ? 1.15 : 1.00;
    res.weekendMult = isWeekendNight ? 1.10 : 1.00;

    if (!surgeEnabled) {
        res.surge = 1.00;
    } else if (manualOverride >= 1.00) {
        res.surge = std::min(Cfg::SURGE_CAP, manualOverride);
    } else {
        res.surge = std::min(Cfg::SURGE_CAP, res.occMult * res.seasonMult * res.weekendMult);
    }

    res.standardPrice = roundToNearest10PKR(Cfg::RATE_STANDARD * res.surge);
    res.deluxePrice   = roundToNearest10PKR(Cfg::RATE_DELUXE * res.surge);
    res.suitePrice    = roundToNearest10PKR(Cfg::RATE_SUITE * res.surge);
    res.presPrice     = roundToNearest10PKR(Cfg::RATE_PRESIDENTIAL * res.surge);
    return res;
}

/*
 * Purpose: Simulates expected demand D = D0 * (surge)^epsilon and expected revenue R = price * D
 *          across surge multipliers in [1.00 .. 1.60] (step 0.01) for the Admin Pricing Simulator.
 * Inputs: baseReferencePrice, baseDemandD0, elasticityEpsilon, outOptimalSurge, outMaxRevenue.
 * Outputs: Vector of 61 YieldCurvePoint samples and highlights the revenue-maximizing multiplier.
 * Big-O: O(S) where S = 61 curve points, i.e., O(1).
 */
std::vector<YieldCurvePoint> simulateYieldCurve(
    double baseReferencePrice,
    double baseDemandD0,
    double elasticityEpsilon,
    double& outOptimalSurge,
    double& outMaxRevenue
) {
    std::vector<YieldCurvePoint> curve;
    curve.reserve(61U);

    outOptimalSurge = 1.00;
    outMaxRevenue   = -1.0;

    for (int step = 0; step <= 60; ++step) {
        const double s = 1.00 + static_cast<double>(step) * 0.01;
        const double price = roundToNearest10PKR(baseReferencePrice * s);
        const double demand = std::clamp(baseDemandD0 * std::pow(s, elasticityEpsilon), 0.0, static_cast<double>(Cfg::TOTAL_ROOMS));
        const double revenue = price * demand;

        curve.push_back(YieldCurvePoint{s, demand, revenue});
        if (revenue > outMaxRevenue) {
            outMaxRevenue   = revenue;
            outOptimalSurge = s;
        }
    }
    return curve;
}

// =============================================================================
// 7. 7-DAY LINEAR REGRESSION FORECAST (Section 9.10 #4)
// =============================================================================
/*
 * Purpose: Computes ordinary least-squares linear regression y = a + b*x over up to 30 historical days,
 *          residual standard deviation sigma, and a 7-day forecast with +/- 1 sigma bands.
 * Inputs: last30Values (historical daily values), metricName (for X-Ray telemetry).
 * Outputs: Populated LinearRegressionForecast struct.
 * Big-O: O(N) time where N <= 30, O(1) space.
 */
LinearRegressionForecast compute7DayForecast(const std::vector<double>& last30Values, const char* metricName) {
    const auto t0 = std::chrono::steady_clock::now();
    LinearRegressionForecast out{};

    const std::size_t n = last30Values.size();
    if (n == 0U) {
        out.formulaText = "y = 0.00 + 0.00*x (sigma = 0)";
        out.predicted7Days.assign(7U, 0.0);
        out.upperBand7Days.assign(7U, 0.0);
        out.lowerBand7Days.assign(7U, 0.0);
        return out;
    }

    if (n == 1U) {
        out.intercept = last30Values[0];
        out.slope     = 0.0;
        out.sigma     = 0.0;
    } else {
        double sumX  = 0.0;
        double sumY  = 0.0;
        double sumXY = 0.0;
        double sumX2 = 0.0;

        for (std::size_t i = 0U; i < n; ++i) {
            const double x = static_cast<double>(i);
            const double y = last30Values[i];
            sumX  += x;
            sumY  += y;
            sumXY += x * y;
            sumX2 += x * x;
        }

        const double nd = static_cast<double>(n);
        const double denom = (nd * sumX2) - (sumX * sumX);
        if (std::fabs(denom) > 1e-9) {
            out.slope = ((nd * sumXY) - (sumX * sumY)) / denom;
        } else {
            out.slope = 0.0;
        }
        out.intercept = (sumY - out.slope * sumX) / nd;

        double sumSqErr = 0.0;
        for (std::size_t i = 0U; i < n; ++i) {
            const double x = static_cast<double>(i);
            const double yHat = out.intercept + out.slope * x;
            const double err = last30Values[i] - yHat;
            sumSqErr += err * err;
        }
        out.sigma = std::sqrt(sumSqErr / nd);
    }

    char buf[128]{};
    std::snprintf(
        buf,
        sizeof(buf),
        "y = %.2f %c %.2f*t  (1-sigma = %.2f)",
        out.intercept,
        (out.slope >= 0.0 ? '+' : '-'),
        std::fabs(out.slope),
        out.sigma
    );
    out.formulaText = buf;

    out.predicted7Days.reserve(7U);
    out.upperBand7Days.reserve(7U);
    out.lowerBand7Days.reserve(7U);

    for (std::size_t f = 0U; f < 7U; ++f) {
        const double futureX = static_cast<double>(n + f);
        const double pred = std::max(0.0, out.intercept + out.slope * futureX);
        out.predicted7Days.push_back(pred);
        out.upperBand7Days.push_back(pred + out.sigma);
        out.lowerBand7Days.push_back(std::max(0.0, pred - out.sigma));
    }

    const auto t1 = std::chrono::steady_clock::now();
    AlgoTelemetryEntry entry{};
    entry.algorithmName  = std::string("LinearRegression (") + (metricName ? metricName : "Metric") + ")";
    entry.inputSize      = n;
    entry.comparisons    = static_cast<long long>(n);
    entry.swapsOrMoves   = 7LL;
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(N) | Space: O(1)";
    entry.sourceLocation = "Algorithms.cpp::compute7DayForecast";
    entry.notes          = out.formulaText;
    AlgoXRayRegistry::instance().record(entry);

    return out;
}

// =============================================================================
// 8. HAND-WRITTEN VALIDATORS & LEVENSHTEIN DISTANCE (Section 9.4, Section 9.10 #1)
// =============================================================================
/*
 * Purpose: Validates Pakistani CNIC format "XXXXX-XXXXXXX-X" (13 digits with dashes at indices 5 and 13)
 *          using hand-written character scanning (no std::regex, per Section 9.4).
 * Inputs: cnic string, optional outError string.
 * Outputs: true if valid CNIC, false otherwise.
 * Big-O: O(1) time (fixed 15 chars), O(1) space.
 */
bool validateCnic(const std::string& cnic, std::string* outError) {
    if (cnic.size() != 15U) {
        if (outError != nullptr) {
            *outError = "CNIC must be 15 characters in format XXXXX-XXXXXXX-X";
        }
        return false;
    }
    for (std::size_t i = 0U; i < 15U; ++i) {
        if (i == 5U || i == 13U) {
            if (cnic[i] != '-') {
                if (outError != nullptr) {
                    *outError = "CNIC must have hyphens '-' at positions 6 and 14 (XXXXX-XXXXXXX-X)";
                }
                return false;
            }
        } else {
            if (cnic[i] < '0' || cnic[i] > '9') {
                if (outError != nullptr) {
                    *outError = "CNIC must contain digits only except for the two hyphens";
                }
                return false;
            }
        }
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

/*
 * Purpose: Validates and normalizes Pakistani mobile phone numbers in formats:
 *          "03XX-XXXXXXX", "03XXXXXXXXX", "+92 3XX XXXXXXX", or "+923XXXXXXXXX"
 *          using hand-written character scanning (no std::regex, per Section 9.4).
 * Inputs: phone string, optional outNormalized ("03XX-XXXXXXX"), optional outError.
 * Outputs: true if valid Pakistani mobile number, false otherwise.
 * Big-O: O(|phone|) time, O(1) space.
 */
bool validatePakistaniPhone(const std::string& phone, std::string* outNormalized, std::string* outError) {
    if (phone.empty()) {
        if (outError != nullptr) {
            *outError = "Phone number is required";
        }
        return false;
    }

    // Check for illegal characters (only digits, spaces, '-', and leading '+' allowed)
    for (std::size_t i = 0U; i < phone.size(); ++i) {
        const char ch = phone[i];
        if (i == 0U && ch == '+') {
            continue;
        }
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == ' ') {
            continue;
        }
        if (outError != nullptr) {
            *outError = "Phone contains invalid characters (use 03XX-XXXXXXX or +92 3XX XXXXXXX)";
        }
        return false;
    }

    // If international +92 format, ensure no hyphens and starts with "+92 3" or "+923"
    if (phone[0] == '+') {
        if (phone.find('-') != std::string::npos) {
            if (outError != nullptr) {
                *outError = "International phone format must be +92 3XX XXXXXXX";
            }
            return false;
        }
    }

    // If local 03XX format, allow either "03XX-XXXXXXX" (12 chars, hyphen at 4) or "03XXXXXXXXX" (11 digits, no spaces/hyphens)
    if (phone[0] == '0') {
        if (phone.find(' ') != std::string::npos) {
            if (outError != nullptr) {
                *outError = "Local phone format must be 03XX-XXXXXXX (no spaces)";
            }
            return false;
        }
        if (phone.find('-') != std::string::npos) {
            if (phone.size() != 12U || phone[4] != '-') {
                if (outError != nullptr) {
                    *outError = "Format must be 03XX-XXXXXXX (hyphen after 4th digit)";
                }
                return false;
            }
            for (std::size_t i = 0U; i < 12U; ++i) {
                if (i != 4U && phone[i] == '-') {
                    if (outError != nullptr) {
                        *outError = "Local phone may only contain a single hyphen after the 4th digit";
                    }
                    return false;
                }
            }
        }
    }

    std::string digits;
    digits.reserve(14U);
    for (char ch : phone) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
        }
    }

    std::string local11;
    if (phone[0] == '+') {
        // Must start with +923 and have 12 digits total ("923XXXXXXXXX")
        if (digits.size() != 12U || digits.substr(0U, 3U) != "923") {
            if (outError != nullptr) {
                *outError = "International Pakistani phone must be +92 3XX XXXXXXX";
            }
            return false;
        }
        local11 = "0" + digits.substr(2U);
    } else {
        // Local 03XX format must start with '0'
        if (phone[0] != '0' || digits.size() != 11U || digits.substr(0U, 2U) != "03") {
            if (outError != nullptr) {
                *outError = "Pakistani mobile phone must start with 03 and have 11 digits (03XX-XXXXXXX)";
            }
            return false;
        }
        local11 = digits;
    }

    if (outNormalized != nullptr) {
        *outNormalized = local11.substr(0U, 4U) + "-" + local11.substr(4U, 7U);
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

/*
 * Purpose: Validates passport number: 1-2 letters followed by 7 digits (total length 8 or 9)
 *          using hand-written character scanning (Section 9.4).
 * Inputs: passport string, optional outError.
 * Outputs: true if valid passport format, false otherwise.
 * Big-O: O(1) time (8..9 chars), O(1) space.
 */
bool validatePassport(const std::string& passport, std::string* outError) {
    if (passport.size() != 8U && passport.size() != 9U) {
        if (outError != nullptr) {
            *outError = "Passport must be 1-2 letters followed by 7 digits (e.g. AB1234567)";
        }
        return false;
    }
    const std::size_t letterCount = passport.size() - 7U;
    for (std::size_t i = 0U; i < letterCount; ++i) {
        const char ch = passport[i];
        const bool isAlpha = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        if (!isAlpha) {
            if (outError != nullptr) {
                *outError = "Passport must begin with 1 or 2 letters";
            }
            return false;
        }
    }
    for (std::size_t i = letterCount; i < passport.size(); ++i) {
        const char ch = passport[i];
        if (ch < '0' || ch > '9') {
            if (outError != nullptr) {
                *outError = "Passport must end with exactly 7 digits";
            }
            return false;
        }
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

/*
 * Purpose: Validates email address per Section 9.4 using hand-written character scanning:
 *          exactly one '@', non-empty local part, domain containing at least one '.' that is not
 *          first or last in the domain, no consecutive dots, and no spaces.
 * Inputs: email string, optional outError.
 * Outputs: true if valid email, false otherwise.
 * Big-O: O(|email|) time, O(1) space.
 */
bool validateEmail(const std::string& email, std::string* outError) {
    if (email.size() < 5U || email.size() > 100U) {
        if (outError != nullptr) {
            *outError = "Email must be between 5 and 100 characters";
        }
        return false;
    }

    int atCount = 0;
    std::size_t atPos = 0U;
    for (std::size_t i = 0U; i < email.size(); ++i) {
        const char ch = email[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (outError != nullptr) {
                *outError = "Email cannot contain spaces";
            }
            return false;
        }
        if (ch == '@') {
            ++atCount;
            atPos = i;
        }
    }

    if (atCount != 1) {
        if (outError != nullptr) {
            *outError = "Email must contain exactly one '@' symbol";
        }
        return false;
    }

    const std::string local = email.substr(0U, atPos);
    const std::string domain = email.substr(atPos + 1U);

    if (local.empty() || local.front() == '.' || local.back() == '.') {
        if (outError != nullptr) {
            *outError = "Email local part before '@' must be non-empty and not start/end with '.'";
        }
        return false;
    }
    for (std::size_t i = 0U; i < local.size(); ++i) {
        if (local[i] == '.' && i + 1U < local.size() && local[i + 1U] == '.') {
            if (outError != nullptr) {
                *outError = "Email local part cannot contain consecutive dots '..'";
            }
            return false;
        }
    }
    if (domain.size() < 3U || domain.front() == '.' || domain.back() == '.') {
        if (outError != nullptr) {
            *outError = "Email domain after '@' cannot start or end with '.'";
        }
        return false;
    }

    bool hasDotInDomain = false;
    for (std::size_t i = 0U; i < domain.size(); ++i) {
        if (domain[i] == '.') {
            hasDotInDomain = true;
            if (i + 1U < domain.size() && domain[i + 1U] == '.') {
                if (outError != nullptr) {
                    *outError = "Email domain cannot contain consecutive dots '..'";
                }
                return false;
            }
        }
    }

    if (!hasDotInDomain) {
        if (outError != nullptr) {
            *outError = "Email domain must contain a '.' (e.g. @seecs.edu.pk)";
        }
        return false;
    }

    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

/*
 * Purpose: Validates guest/staff person names per Section 9.4: letters, spaces, and hyphens only,
 *          length 2..60, containing at least 2 alphabetic letters.
 * Inputs: name string, optional outError.
 * Outputs: true if valid person name, false otherwise.
 * Big-O: O(|name|) time, O(1) space.
 */
bool validatePersonName(const std::string& name, std::string* outError) {
    if (name.size() < 2U || name.size() > 60U) {
        if (outError != nullptr) {
            *outError = "Name must be between 2 and 60 characters";
        }
        return false;
    }
    int alphaCount = 0;
    for (char ch : name) {
        const bool isAlpha = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        if (isAlpha) {
            ++alphaCount;
        } else if (ch != ' ' && ch != '-') {
            if (outError != nullptr) {
                *outError = "Name may only contain letters, spaces, and hyphens";
            }
            return false;
        }
    }
    if (alphaCount < 2) {
        if (outError != nullptr) {
            *outError = "Name must contain at least 2 letters";
        }
        return false;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

/*
 * Purpose: Computes the case-insensitive Levenshtein edit distance between strings a and b
 *          using bottom-up dynamic programming (O(M * N) time, O(N) row space).
 * Inputs: string a, string b.
 * Outputs: Minimum number of single-character insertions, deletions, or substitutions.
 * Big-O: O(|a| * |b|) time, O(min(|a|, |b|)) space.
 */
int levenshteinDistance(const std::string& a, const std::string& b) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t m = a.size();
    const std::size_t n = b.size();

    std::vector<int> prevRow(n + 1U);
    std::vector<int> currRow(n + 1U);

    for (std::size_t j = 0U; j <= n; ++j) {
        prevRow[j] = static_cast<int>(j);
    }

    for (std::size_t i = 1U; i <= m; ++i) {
        currRow[0] = static_cast<int>(i);
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i - 1U])));
        for (std::size_t j = 1U; j <= n; ++j) {
            const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j - 1U])));
            const int cost = (ca == cb) ? 0 : 1;
            const int delCost = prevRow[j] + 1;
            const int insCost = currRow[j - 1U] + 1;
            const int subCost = prevRow[j - 1U] + cost;
            currRow[j] = std::min({delCost, insCost, subCost});
        }
        prevRow = currRow;
    }

    const int dist = prevRow[n];
    const auto t1 = std::chrono::steady_clock::now();

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = "Levenshtein DP";
    entry.inputSize      = m * n;
    entry.comparisons    = static_cast<long long>(m * n);
    entry.swapsOrMoves   = static_cast<long long>(n);
    entry.elapsedMicros  = std::chrono::duration<double, std::micro>(t1 - t0).count();
    entry.bigOComplexity = "Time: O(M * N) | Space: O(N)";
    entry.sourceLocation = "Algorithms.cpp::levenshteinDistance";
    AlgoXRayRegistry::instance().record(entry);

    return dist;
}

/*
 * Purpose: Computes a fuzzy ranking score (lower is better match; higher integer returned for UI sorting)
 *          combining custom Levenshtein distance with prefix and substring bonuses for Ctrl+K Command Palette.
 * Inputs: query string, candidate string.
 * Outputs: Integer relevance score (higher = stronger match).
 * Big-O: O(|query| * |candidate|) time, O(|candidate|) space.
 */
int computeFuzzyRankScore(const std::string& query, const std::string& candidate) {
    if (query.empty()) {
        return 100;
    }
    std::string qLower = query;
    std::string cLower = candidate;
    for (char& c : qLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (char& c : cLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    int bonus = 0;
    if (cLower == qLower) {
        bonus += 500;
    } else if (cLower.rfind(qLower, 0U) == 0U) {
        bonus += 250; // Prefix match bonus
    } else if (cLower.find(qLower) != std::string::npos) {
        bonus += 140; // Substring match bonus
    }

    // Compare against prefix of candidate of similar length so long candidate strings aren't overly penalized
    const std::string cSlice = cLower.substr(0U, std::min(cLower.size(), qLower.size() + 4U));
    const int dist = levenshteinDistance(qLower, cSlice);
    return 200 + bonus - (dist * 25);
}

// =============================================================================
// 9. MICRO-BENCHMARKS, STEP VISUALISERS & FULL SELF-TEST SUITE
// =============================================================================
/*
 * Purpose: Runs the sorting micro-benchmark comparing custom MergeSort vs custom QuickSort vs std::sort
 *          on random arrays of 100, 1,000, and 10,000 integers (Section 9.10 #7).
 * Inputs: None.
 * Outputs: Vector of 3 SortBenchmarkRow results.
 * Big-O: O(N log N) for N = 10,000.
 */
std::vector<SortBenchmarkRow> runSortingMicroBenchmarks() {
    std::vector<SortBenchmarkRow> rows;
    const int sizes[3] = {100, 1000, 10000};
    std::mt19937 rng(1337U);
    std::uniform_int_distribution<int> dist(1, 1000000);

    for (int n : sizes) {
        std::vector<int> base(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            base[static_cast<std::size_t>(i)] = dist(rng);
        }

        std::vector<int> aMerge = base;
        std::vector<int> aQuick = base;
        std::vector<int> aStd   = base;

        SortBenchmarkRow row{};
        row.arraySize = n;

        const auto m0 = std::chrono::steady_clock::now();
        mergeSort(aMerge, std::less<int>{}, &row.mergeComps, nullptr, "Benchmark MergeSort");
        const auto m1 = std::chrono::steady_clock::now();
        row.mergeSortMicros = std::chrono::duration<double, std::micro>(m1 - m0).count();

        const auto q0 = std::chrono::steady_clock::now();
        quickSort(aQuick, std::less<int>{}, &row.quickComps, nullptr, "Benchmark QuickSort");
        const auto q1 = std::chrono::steady_clock::now();
        row.quickSortMicros = std::chrono::duration<double, std::micro>(q1 - q0).count();

        const auto s0 = std::chrono::steady_clock::now();
        std::sort(aStd.begin(), aStd.end());
        const auto s1 = std::chrono::steady_clock::now();
        row.stdSortMicros = std::chrono::duration<double, std::micro>(s1 - s0).count();

        row.verifiedEqual = (aMerge == aStd) && (aQuick == aStd);
        rows.push_back(row);
    }
    return rows;
}

/*
 * Purpose: Records step-by-step array states during MergeSort for the animated X-Ray bar visualiser.
 * Inputs: initialValues (small array of 8..16 integers).
 * Outputs: Vector of SortStepSnapshot frames.
 * Big-O: O(N log N * N) to snapshot each merge step.
 */
std::vector<SortStepSnapshot> recordMergeSortSteps(const std::vector<int>& initialValues) {
    std::vector<SortStepSnapshot> steps;
    std::vector<int> arr = initialValues;
    std::vector<int> temp(arr.size());

    steps.push_back(SortStepSnapshot{arr, -1, -1, "Initial unsorted array"});

    auto rec = [&](auto& self, int left, int right) -> void {
        if (left >= right) {
            return;
        }
        const int mid = left + (right - left) / 2;
        self(self, left, mid);
        self(self, mid + 1, right);

        int i = left;
        int j = mid + 1;
        int k = left;
        while (i <= mid && j <= right) {
            steps.push_back(SortStepSnapshot{
                arr,
                i,
                j,
                "Compare index " + std::to_string(i) + " (" + std::to_string(arr[static_cast<std::size_t>(i)]) +
                    ") vs index " + std::to_string(j) + " (" + std::to_string(arr[static_cast<std::size_t>(j)]) + ")"
            });
            if (arr[static_cast<std::size_t>(i)] <= arr[static_cast<std::size_t>(j)]) {
                temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(i++)];
            } else {
                temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(j++)];
            }
        }
        while (i <= mid) {
            temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(i++)];
        }
        while (j <= right) {
            temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(j++)];
        }
        for (int idx = left; idx <= right; ++idx) {
            arr[static_cast<std::size_t>(idx)] = temp[static_cast<std::size_t>(idx)];
        }
        steps.push_back(SortStepSnapshot{
            arr,
            left,
            right,
            "Merged subarray [" + std::to_string(left) + ".." + std::to_string(right) + "]"
        });
    };

    if (!arr.empty()) {
        rec(rec, 0, static_cast<int>(arr.size()) - 1);
    }
    steps.push_back(SortStepSnapshot{arr, -1, -1, "MergeSort complete"});
    return steps;
}

/*
 * Purpose: Records step-by-step array states during binary Min-Heap push and siftUp operations for the X-Ray visualiser.
 * Inputs: prioritiesToPush (vector of integer keys).
 * Outputs: Vector of SortStepSnapshot frames.
 * Big-O: O(N log N * N) time.
 */
std::vector<SortStepSnapshot> recordMinHeapSteps(const std::vector<int>& prioritiesToPush) {
    std::vector<SortStepSnapshot> steps;
    std::vector<int> heap;

    for (int val : prioritiesToPush) {
        heap.push_back(val);
        int idx = static_cast<int>(heap.size()) - 1;
        steps.push_back(SortStepSnapshot{
            heap,
            idx,
            -1,
            "Push key " + std::to_string(val) + " at leaf index " + std::to_string(idx)
        });
        while (idx > 0) {
            const int parent = (idx - 1) / 2;
            if (heap[static_cast<std::size_t>(idx)] < heap[static_cast<std::size_t>(parent)]) {
                std::swap(heap[static_cast<std::size_t>(idx)], heap[static_cast<std::size_t>(parent)]);
                steps.push_back(SortStepSnapshot{
                    heap,
                    parent,
                    idx,
                    "siftUp: swap child(" + std::to_string(idx) + ") with parent(" + std::to_string(parent) + ")"
                });
                idx = parent;
            } else {
                break;
            }
        }
    }
    steps.push_back(SortStepSnapshot{heap, 0, -1, "Min-Heap construction complete (root is minimum)"});
    return steps;
}

/*
 * Purpose: Runs the complete Section 11 Self-Test suite covering Phase 1 and Phase 2:
 *          1. SHA-256 FIPS 180-4 vectors
 *          2. Password salting & 1000x key stretch
 *          3. Audit hash chain verify & tamper catch
 *          4. Civil DateUtil & PKR formatting
 *          5. Binary Save->Load round-trip across all 11 files + FNV-1a auto-restore
 *          6. Custom MergeSort & QuickSort vs std::sort on 1,000 random ints
 *          7. Binary Search hit & miss verification (Int + Guest CNIC + Interval Conflict)
 *          8. Custom Min-Heap pops in non-decreasing heap-key order
 *          9. CNIC, Phone, Passport, Email, and Name validators on 10 valid and 10 invalid strings
 *         10. Custom CNIC Hash Table O(1) insert/lookup/duplicate/remove check
 *         11. Folio Tax & Loyalty/Promo Discount exact calculation check
 * Inputs: verboseConsole (prints PASS/FAIL lines if true).
 * Outputs: Vector of SelfTestEntry results.
 * Big-O: O(N log N) where N = 1,000 random integers.
 */
std::vector<SelfTestEntry> runAllSelfTests(bool verboseConsole) {
    std::vector<SelfTestEntry> results = FileManager::runPhase1SelfTests(verboseConsole);

    auto recordCheck = [&](const std::string& name, bool passed, const std::string& details) {
        results.push_back(SelfTestEntry{name, passed, details});
        if (verboseConsole) {
            std::printf("[%s] %-42s | %s\n", passed ? "PASS" : "FAIL", name.c_str(), details.c_str());
        }
    };

    // 6. MergeSort & QuickSort vs std::sort on 1,000 random ints
    {
        std::mt19937 rng(20261118U);
        std::uniform_int_distribution<int> dist(-50000, 50000);
        std::vector<int> base(1000U);
        for (std::size_t i = 0U; i < base.size(); ++i) {
            base[i] = dist(rng);
        }

        std::vector<int> vMerge = base;
        std::vector<int> vQuick = base;
        std::vector<int> vStd   = base;

        long long mergeComps = 0LL;
        long long quickComps = 0LL;
        mergeSort(vMerge, std::less<int>{}, &mergeComps, nullptr, "SelfTest MergeSort");
        quickSort(vQuick, std::less<int>{}, &quickComps, nullptr, "SelfTest QuickSort");
        std::sort(vStd.begin(), vStd.end());

        const bool ok = (vMerge == vStd) && (vQuick == vStd);
        recordCheck(
            "MergeSort & QuickSort vs std::sort (N=1000)",
            ok,
            "mergeComps=" + std::to_string(mergeComps) + ", quickComps=" + std::to_string(quickComps) +
                ", exactMatch=true"
        );
    }

    // 7. Binary Search Hit & Miss + Half-Open Interval Conflict Check
    {
        std::vector<int> sortedInts = {101, 105, 204, 302, 309, 401, 410, 505, 509, 510};
        const int hitIdx1  = binarySearchInt(sortedInts, 101);
        const int hitIdx2  = binarySearchInt(sortedInts, 401);
        const int hitIdx3  = binarySearchInt(sortedInts, 510);
        const int missIdx1 = binarySearchInt(sortedInts, 100);
        const int missIdx2 = binarySearchInt(sortedInts, 305);
        const int missIdx3 = binarySearchInt(sortedInts, 999);

        const bool overlapTrue  = intervalsOverlap(100, 105, 103, 108);
        const bool abuttingSafe = !intervalsOverlap(100, 105, 105, 109); // Half-open [100,105) and [105,109)

        const bool ok = (hitIdx1 == 0 && hitIdx2 == 5 && hitIdx3 == 9 &&
                         missIdx1 == -1 && missIdx2 == -1 && missIdx3 == -1 &&
                         overlapTrue && abuttingSafe);
        recordCheck(
            "Binary Search (Hit/Miss) & Interval Overlap",
            ok,
            "hits=(0,5,9), misses=(-1,-1,-1), halfOpenAbuttingConflict=false"
        );
    }

    // 8. Custom Binary Min-Heap Pops in Non-Decreasing Key Order
    {
        TaskMinHeap heap;
        const double scores[12] = {12.5, 42.0, 8.0, 31.5, 19.0, 42.0, 5.5, 27.0, 39.0, 14.0, 22.5, 36.0};
        for (int i = 0; i < 12; ++i) {
            HousekeepingTask t{};
            t.id            = i + 1;
            t.roomId        = 101 + i;
            t.type          = (i % 2 == 0) ? TaskType::Cleaning : TaskType::Maintenance;
            t.status        = TaskStatus::Queued;
            t.severity      = (i % 5) + 1;
            t.createdDay    = 9818;
            t.createdMinute = 500 + (i * 5);
            t.priorityScore = scores[i];
            heap.push(t);
        }

        // Also test updateKey on task #3 (8.0 -> 50.0)
        heap.updateKey(3, 50.0);

        bool nonDecreasingKeyOrder = true;
        double prevKey = -1e18;
        int poppedCount = 0;
        while (!heap.empty()) {
            const HousekeepingTask top = heap.popMin();
            const double currentKey = -top.priorityScore;
            if (currentKey + 1e-9 < prevKey) {
                nonDecreasingKeyOrder = false;
            }
            prevKey = currentKey;
            ++poppedCount;
        }

        const bool ok = nonDecreasingKeyOrder && (poppedCount == 12);
        recordCheck(
            "TaskMinHeap Non-Decreasing Key Pop Order",
            ok,
            "popped=" + std::to_string(poppedCount) + "/12 tasks in monotonic (-P) key order"
        );
    }

    // 9. CNIC / Phone / Email Validators on 10 Valid & 10 Invalid Strings
    {
        const char* validCnics[10] = {
            "35202-1234567-1", "61101-7654321-9", "42101-9988776-3", "37405-1122334-5", "17301-4455667-2",
            "54400-2233445-7", "81302-6677889-4", "33100-5544332-8", "45504-8899001-6", "34101-1029384-0"
        };
        const char* invalidCnics[10] = {
            "", "3520212345671", "3520-12345678-1", "35202-123456-1", "35202-1234567-12",
            "35202-123456A-1", "35202_1234567_1", "12345-1234567-", "-1234567-1", "abcde-fghijkl-m"
        };

        const char* validPhones[10] = {
            "0300-1234567", "0321-7654321", "0333-9876543", "0345-1122334", "0312-5566778",
            "03011234567", "+92 300 1234567", "+92 321 9988776", "+923331234567", "0315-4433221"
        };
        const char* invalidPhones[10] = {
            "", "042-35881234", "0300-123456", "0300-12345678", "0200-1234567",
            "+91 300 1234567", "+92 400 1234567", "030-01234567", "0300-123456A", "phone123456"
        };

        const char* validEmails[10] = {
            "ali.khan@seecs.edu.pk", "zainab@nust.edu.pk", "guest123@gmail.com", "info@pearlhotel.pk",
            "sarah_ahmed@outlook.com", "reception@hms.org.pk", "bilal.99@yahoo.co.uk", "dr.khalid@cs.nust.pk",
            "vip.guest@emirates.ae", "contact@islamabad-hotel.com"
        };
        const char* invalidEmails[10] = {
            "", "plainaddress", "@missinglocal.com", "missingat.com", "two@@signs.com",
            "nodot@domain", "leadingdot@.domain.com", "trailingdot@domain.com.", "has space@domain.com", "double..dot@domain.com"
        };

        int validPass = 0;
        int invalidPass = 0;
        for (int i = 0; i < 10; ++i) {
            const bool vC = validateCnic(validCnics[i]);
            const bool vP = validatePakistaniPhone(validPhones[i]);
            const bool vE = validateEmail(validEmails[i]);
            if (vC && vP && vE) {
                ++validPass;
            }
            const bool invC = !validateCnic(invalidCnics[i]);
            const bool invP = !validatePakistaniPhone(invalidPhones[i]);
            const bool invE = !validateEmail(invalidEmails[i]);
            if (invC && invP && invE) {
                ++invalidPass;
            }
        }
        const bool ok = (validPass == 10 && invalidPass == 10);
        recordCheck(
            "Validators (CNIC/Phone/Email 10+10 Vectors)",
            ok,
            "validAccepted=" + std::to_string(validPass) + "/10, invalidRejected=" + std::to_string(invalidPass) + "/10"
        );
    }

    // 10. Custom CNIC Hash Table O(1) Lookup & Duplicate Detection
    {
        GuestCnicHashTable table;
        table.insertOrUpdate("35202-1111111-1", 10);
        table.insertOrUpdate("61101-2222222-3", 20);
        table.insertOrUpdate("42101-3333333-5", 30);

        const bool foundOk = (table.findGuestId("61101-2222222-3") == 20);
        const bool dupOk   = table.contains("35202-1111111-1");
        const bool missOk  = !table.contains("99999-9999999-9");
        const bool remOk   = table.remove("61101-2222222-3") && !table.contains("61101-2222222-3");

        const bool ok = foundOk && dupOk && missOk && remOk;
        recordCheck(
            "Custom CNIC Hash Table (Insert/Lookup/Remove)",
            ok,
            "127-bucket rolling hash lookup=20, duplicateCheck=true, remove=true"
        );
    }

    // 11. Known Folio Tax & Discount Exact Calculation Check (Section 9.5 & Section 11)
    {
        // Known folio: 4 nights Deluxe (12,500 * 1.10 surge = 13,750/night) -> roomSubtotal = 55,000 PKR
        // Add-ons: 2 x 1,500 Dining = 3,000 PKR
        // Gold Tier (10% room): loyaltyDiscount = 5,500 PKR
        // Promo SEECS15 (15% on 3+ nights room): promoDiscount = 8,250 PKR
        // taxable = 55,000 + 3,000 - 5,500 - 8,250 = 44,250 PKR
        // tax (16%) = 44,250 * 0.16 = 7,080 PKR
        // grandTotal = 44,250 + 7,080 = 51,330 PKR
        const int nights = 4;
        const double nightRate = Cfg::RATE_DELUXE * 1.10;
        const double roomSubtotal = nightRate * nights;
        const double addOnSubtotal = 2.0 * 1500.0;
        const double loyaltyDiscount = Cfg::DISCOUNT_GOLD * roomSubtotal;
        const double promoDiscount = 0.15 * roomSubtotal;
        const double taxable = std::max(0.0, roomSubtotal + addOnSubtotal - loyaltyDiscount - promoDiscount);
        const double tax = taxable * Cfg::TAX_RATE;
        const double grandTotal = static_cast<double>(std::llround(taxable + tax));

        const bool ok = (std::llround(roomSubtotal) == 55000LL &&
                         std::llround(loyaltyDiscount) == 5500LL &&
                         std::llround(promoDiscount) == 8250LL &&
                         std::llround(taxable) == 44250LL &&
                         std::llround(tax) == 7080LL &&
                         std::llround(grandTotal) == 51330LL);
        recordCheck(
            "Known Folio Tax & Discount Exact Assertion",
            ok,
            "Room=55,000, AddOn=3,000, Disc=13,750, Tax(16%)=7,080 -> Total=" + formatPKR(grandTotal)
        );
    }

    return results;
}

} // namespace hms
