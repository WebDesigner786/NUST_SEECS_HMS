#ifndef HMS_ALGORITHMS_H
#define HMS_ALGORITHMS_H

#include "Models.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hms {

// =============================================================================
// 1. ALGORITHM X-RAY TELEMETRY & BENCHMARK STRUCTURES (Section 9.10 #7)
// =============================================================================
struct AlgoTelemetryEntry {
    std::string algorithmName  = "";
    std::size_t inputSize      = 0U;
    long long   comparisons    = 0LL;
    long long   swapsOrMoves   = 0LL;
    double      elapsedMicros  = 0.0;
    std::string bigOComplexity = "";
    std::string sourceLocation = "";
    std::string notes          = "";
};

struct SortBenchmarkRow {
    int    arraySize        = 100;
    double mergeSortMicros  = 0.0;
    double quickSortMicros  = 0.0;
    double stdSortMicros    = 0.0;
    long long mergeComps    = 0LL;
    long long quickComps    = 0LL;
    bool   verifiedEqual    = true;
};

struct SortStepSnapshot {
    std::vector<int> values{};
    int              highlightA = -1;
    int              highlightB = -1;
    std::string      description = "";
};

class AlgoXRayRegistry {
public:
    static AlgoXRayRegistry& instance();

    void record(const AlgoTelemetryEntry& entry);
    const std::vector<AlgoTelemetryEntry>& entries() const { return m_entries; }
    void clear() { m_entries.clear(); }

private:
    AlgoXRayRegistry() = default;
    std::vector<AlgoTelemetryEntry> m_entries{};
};

// =============================================================================
// 2. CUSTOM MERGESORT & QUICKSORT (Section 1 Law 4, Section 9.7, Section 11)
// =============================================================================
namespace detail {

/*
 * Purpose: Merges two sorted sub-arrays arr[left..mid] and arr[mid+1..right] using auxiliary buffer temp.
 * Inputs: arr, temp, left, mid, right, comparator comp, comparison/move counters.
 * Outputs: arr[left..right] sorted in-place according to comp.
 * Big-O: O(right - left + 1) time, O(right - left + 1) auxiliary space.
 */
template <typename T, typename Compare>
void mergeSortCombine(
    std::vector<T>& arr,
    std::vector<T>& temp,
    int left,
    int mid,
    int right,
    Compare comp,
    long long& comparisons,
    long long& moves
) {
    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        ++comparisons;
        if (comp(arr[static_cast<std::size_t>(i)], arr[static_cast<std::size_t>(j)]) ||
            !comp(arr[static_cast<std::size_t>(j)], arr[static_cast<std::size_t>(i)])) {
            temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(i++)];
        } else {
            temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(j++)];
        }
        ++moves;
    }

    while (i <= mid) {
        temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(i++)];
        ++moves;
    }

    while (j <= right) {
        temp[static_cast<std::size_t>(k++)] = arr[static_cast<std::size_t>(j++)];
        ++moves;
    }

    for (int idx = left; idx <= right; ++idx) {
        arr[static_cast<std::size_t>(idx)] = temp[static_cast<std::size_t>(idx)];
        ++moves;
    }
}

/*
 * Purpose: Recursive divide-and-conquer helper for custom MergeSort.
 * Inputs: arr, temp, left, right, comparator comp, counters.
 * Outputs: Sorts arr[left..right].
 * Big-O: O(N log N) time, O(N) auxiliary space.
 */
template <typename T, typename Compare>
void mergeSortRecursive(
    std::vector<T>& arr,
    std::vector<T>& temp,
    int left,
    int right,
    Compare comp,
    long long& comparisons,
    long long& moves
) {
    if (left >= right) {
        return;
    }
    const int mid = left + (right - left) / 2;
    mergeSortRecursive(arr, temp, left, mid, comp, comparisons, moves);
    mergeSortRecursive(arr, temp, mid + 1, right, comp, comparisons, moves);
    mergeSortCombine(arr, temp, left, mid, right, comp, comparisons, moves);
}

/*
 * Purpose: Partitions arr[low..high] using median-of-three pivot selection for custom QuickSort.
 * Inputs: arr, low, high, comparator comp, comparison/swap counters.
 * Outputs: Returns final pivot index.
 * Big-O: O(high - low + 1) time, O(1) space.
 */
template <typename T, typename Compare>
int quickSortPartition(
    std::vector<T>& arr,
    int low,
    int high,
    Compare comp,
    long long& comparisons,
    long long& swaps
) {
    const int mid = low + (high - low) / 2;
    if (comp(arr[static_cast<std::size_t>(mid)], arr[static_cast<std::size_t>(low)])) {
        std::swap(arr[static_cast<std::size_t>(low)], arr[static_cast<std::size_t>(mid)]);
        ++swaps;
    }
    if (comp(arr[static_cast<std::size_t>(high)], arr[static_cast<std::size_t>(low)])) {
        std::swap(arr[static_cast<std::size_t>(low)], arr[static_cast<std::size_t>(high)]);
        ++swaps;
    }
    if (comp(arr[static_cast<std::size_t>(mid)], arr[static_cast<std::size_t>(high)])) {
        std::swap(arr[static_cast<std::size_t>(mid)], arr[static_cast<std::size_t>(high)]);
        ++swaps;
    }

    const T pivot = arr[static_cast<std::size_t>(high)];
    int i = low - 1;
    for (int j = low; j < high; ++j) {
        ++comparisons;
        if (comp(arr[static_cast<std::size_t>(j)], pivot)) {
            ++i;
            if (i != j) {
                std::swap(arr[static_cast<std::size_t>(i)], arr[static_cast<std::size_t>(j)]);
                ++swaps;
            }
        }
    }
    if (i + 1 != high) {
        std::swap(arr[static_cast<std::size_t>(i + 1)], arr[static_cast<std::size_t>(high)]);
        ++swaps;
    }
    return i + 1;
}

/*
 * Purpose: Recursive helper for custom QuickSort.
 * Inputs: arr, low, high, comparator comp, comparison/swap counters.
 * Outputs: Sorts arr[low..high] in-place.
 * Big-O: O(N log N) average time, O(log N) stack space.
 */
template <typename T, typename Compare>
void quickSortRecursive(
    std::vector<T>& arr,
    int low,
    int high,
    Compare comp,
    long long& comparisons,
    long long& swaps
) {
    if (low < high) {
        const int pivotIdx = quickSortPartition(arr, low, high, comp, comparisons, swaps);
        quickSortRecursive(arr, low, pivotIdx - 1, comp, comparisons, swaps);
        quickSortRecursive(arr, pivotIdx + 1, high, comp, comparisons, swaps);
    }
}

} // namespace detail

/*
 * Purpose: Sorts a std::vector<T> using hand-written stable MergeSort and logs telemetry to AlgoXRayRegistry.
 * Inputs: arr (vector to sort), comp (strict weak ordering comparator), optional outComparisons/outMoves.
 * Outputs: Sorts arr in-place.
 * Big-O: O(N log N) worst/average/best time, O(N) auxiliary space.
 */
template <typename T, typename Compare>
void mergeSort(
    std::vector<T>& arr,
    Compare comp,
    long long* outComparisons = nullptr,
    long long* outMoves = nullptr,
    const char* label = "MergeSort"
) {
    const auto t0 = std::chrono::steady_clock::now();
    long long comps = 0LL;
    long long moves = 0LL;

    if (arr.size() > 1U) {
        std::vector<T> temp(arr.size());
        detail::mergeSortRecursive(arr, temp, 0, static_cast<int>(arr.size()) - 1, comp, comps, moves);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double micros = std::chrono::duration<double, std::micro>(t1 - t0).count();

    if (outComparisons != nullptr) {
        *outComparisons = comps;
    }
    if (outMoves != nullptr) {
        *outMoves = moves;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = label ? label : "MergeSort";
    entry.inputSize      = arr.size();
    entry.comparisons    = comps;
    entry.swapsOrMoves   = moves;
    entry.elapsedMicros  = micros;
    entry.bigOComplexity = "Time: O(N log N) | Space: O(N)";
    entry.sourceLocation = "Algorithms.h::mergeSort";
    AlgoXRayRegistry::instance().record(entry);
}

/*
 * Purpose: Sorts a std::vector<T> using hand-written QuickSort (median-of-three pivot) and logs X-Ray telemetry.
 * Inputs: arr (vector to sort), comp (strict weak ordering comparator), optional outComparisons/outSwaps.
 * Outputs: Sorts arr in-place.
 * Big-O: O(N log N) average time (O(N^2) worst case), O(log N) stack space.
 */
template <typename T, typename Compare>
void quickSort(
    std::vector<T>& arr,
    Compare comp,
    long long* outComparisons = nullptr,
    long long* outSwaps = nullptr,
    const char* label = "QuickSort"
) {
    const auto t0 = std::chrono::steady_clock::now();
    long long comps = 0LL;
    long long swaps = 0LL;

    if (arr.size() > 1U) {
        detail::quickSortRecursive(arr, 0, static_cast<int>(arr.size()) - 1, comp, comps, swaps);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double micros = std::chrono::duration<double, std::micro>(t1 - t0).count();

    if (outComparisons != nullptr) {
        *outComparisons = comps;
    }
    if (outSwaps != nullptr) {
        *outSwaps = swaps;
    }

    AlgoTelemetryEntry entry{};
    entry.algorithmName  = label ? label : "QuickSort";
    entry.inputSize      = arr.size();
    entry.comparisons    = comps;
    entry.swapsOrMoves   = swaps;
    entry.elapsedMicros  = micros;
    entry.bigOComplexity = "Time: O(N log N) avg | Space: O(log N)";
    entry.sourceLocation = "Algorithms.h::quickSort";
    AlgoXRayRegistry::instance().record(entry);
}

// =============================================================================
// 3. BINARY SEARCH & INTERVAL OVERLAP (Section 9.3, Section 9.6)
// =============================================================================

// Binary search on a sorted int vector; returns index in [0..N-1] or -1 if not found
int binarySearchInt(const std::vector<int>& sortedArr, int target, long long* outComparisons = nullptr);

// Binary search on a CNIC-sorted vector of Guest records; returns index in [0..N-1] or -1 if not found
int binarySearchGuestByCnic(
    const std::vector<Guest>& sortedGuests,
    const std::string& targetCnic,
    long long* outComparisons = nullptr
);

// Half-open interval [aStart, aEnd) vs [bStart, bEnd) overlap check: returns (aStart < bEnd && bStart < aEnd)
bool intervalsOverlap(int32_t aStart, int32_t aEnd, int32_t bStart, int32_t bEnd);

// Checks if [reqCheckIn, reqCheckOut) conflicts with any active booking in a sorted (by checkInDay) room booking vector
// using binary search to locate the first candidate interval
bool hasRoomBookingConflictSorted(
    const std::vector<Booking>& sortedRoomBookings,
    int32_t reqCheckIn,
    int32_t reqCheckOut,
    int ignoreBookingId = -1,
    long long* outComparisons = nullptr
);

// Convenience check across an unsorted full bookings list for a specific roomId
bool hasRoomBookingConflict(
    const std::vector<Booking>& allBookings,
    int roomId,
    int32_t reqCheckIn,
    int32_t reqCheckOut,
    int ignoreBookingId = -1
);

// =============================================================================
// 4. CUSTOM HASH TABLE FOR GUEST CNIC LOOKUP (Section 1 Law 4, Section 9.4)
// =============================================================================
class GuestCnicHashTable {
public:
    static constexpr std::size_t kBucketCount = 127U; // Prime bucket count for uniform distribution

    struct BucketNode {
        std::string cnic    = "";
        int         guestId = -1;
    };

    GuestCnicHashTable();

    void clear();
    void rebuild(const std::vector<Guest>& guests);
    bool insertOrUpdate(const std::string& cnic, int guestId);
    bool remove(const std::string& cnic);
    int  findGuestId(const std::string& cnic, long long* outProbes = nullptr) const;
    bool contains(const std::string& cnic) const;

    std::size_t size() const { return m_count; }
    static std::size_t hashCnic(const std::string& cnic);

private:
    std::vector<std::vector<BucketNode>> m_buckets{};
    std::size_t                          m_count = 0U;
};

// =============================================================================
// 5. CUSTOM BINARY MIN-HEAP FOR HOUSEKEEPING DISPATCH (Section 9.9)
// =============================================================================
class TaskMinHeap {
public:
    TaskMinHeap() = default;

    bool empty() const { return m_heap.empty(); }
    std::size_t size() const { return m_heap.size(); }
    void clear() { m_heap.clear(); }

    void push(const HousekeepingTask& task);
    const HousekeepingTask& peekMin() const;
    HousekeepingTask popMin();
    bool updateKey(int taskId, double newPriorityScore);
    bool removeById(int taskId);

    // Returns a copy of all tasks popped in exact min-heap priority order (highest P first)
    std::vector<HousekeepingTask> getTasksInDispatchOrder() const;
    const std::vector<HousekeepingTask>& rawData() const { return m_heap; }

    // Computes priority P = 3U + V + E + 2*sev per Section 9.9
    static double computePriorityScore(
        TaskType type,
        int severity,
        double hoursUntilNextCheckIn,
        bool hasUpcomingCheckIn,
        LoyaltyTier arrivingGuestTier,
        double hoursSinceCheckout
    );

private:
    // Returns true if task a has strictly smaller heap key (-P) than task b (ties broken by earlier created time)
    static bool hasHigherPriority(const HousekeepingTask& a, const HousekeepingTask& b);
    void siftUp(std::size_t idx, long long& swaps);
    void siftDown(std::size_t idx, long long& swaps);

    std::vector<HousekeepingTask> m_heap{};
};

// =============================================================================
// 6. ALGORITHM 1 — MULTI-CRITERIA SMART ROOM RECOMMENDER (Section 9.7)
// =============================================================================
struct RecommenderWeights {
    double budget   = 0.30;
    double floor    = 0.15;
    double view     = 0.20;
    double elevator = 0.10;
    double noise    = 0.10;
    double bed      = 0.15;

    void normalize();
};

struct RoomFeatureBreakdown {
    int         roomId        = 0;
    int         roomNumber    = 101;
    double      fBudget       = 0.0;
    double      fFloor        = 0.0;
    double      fView         = 0.0;
    double      fElevator     = 0.0;
    double      fNoise        = 0.0;
    double      fBed          = 0.0;
    double      tierBonus     = 0.0;
    double      totalScore01  = 0.0;
    int         percent       = 0;
    std::string reasonText    = "";
    bool        stlCrossCheck = true;
};

// Computes feature breakdown for a single candidate room
RoomFeatureBreakdown evaluateRoomCandidate(
    const Room& room,
    const GuestPreferences& prefs,
    LoyaltyTier guestTier,
    double activeSurgeMultiplier,
    const RecommenderWeights& weights = RecommenderWeights{}
);

// Ranks all available, conflict-free candidate rooms using custom MergeSort (cross-checked against std::sort)
std::vector<RoomMatch> recommendRooms(
    const std::vector<Room>& rooms,
    const std::vector<Booking>& bookings,
    int32_t checkInDay,
    int32_t checkOutDay,
    const GuestPreferences& prefs,
    LoyaltyTier guestTier,
    double activeSurgeMultiplier,
    const RecommenderWeights& weights = RecommenderWeights{},
    std::vector<RoomFeatureBreakdown>* outAllBreakdowns = nullptr,
    bool* outStlVerified = nullptr
);

// =============================================================================
// 7. ALGORITHM 2 — DYNAMIC YIELD MANAGEMENT & PRICING SIMULATOR (Section 9.8)
// =============================================================================
struct YieldPricingResult {
    double occupancyRatio = 0.0;
    double occMult        = 1.00;
    double seasonMult     = 1.00;
    double weekendMult    = 1.00;
    double surge          = 1.00;
    double standardPrice  = Cfg::RATE_STANDARD;
    double deluxePrice    = Cfg::RATE_DELUXE;
    double suitePrice     = Cfg::RATE_SUITE;
    double presPrice      = Cfg::RATE_PRESIDENTIAL;
};

struct YieldCurvePoint {
    double surgeMultiplier = 1.0;
    double expectedDemand  = 0.0;
    double expectedRevenue = 0.0;
};

double roundToNearest10PKR(double price);

YieldPricingResult computeYieldPricing(
    int occupiedOrReservedRooms,
    int totalRooms,
    bool isPeakSeason,
    bool isWeekendNight,
    bool surgeEnabled = true,
    double manualOverride = 0.0
);

// Generates revenue-vs-surge curve (1.00 to 1.60) for elasticity epsilon (default -1.2) and finds optimal surge
// Latent (unconstrained) nightly room demand at the current price for a given occupancy ratio 0..1:
// D0 = occupancy * TOTAL_ROOMS * 1.35 (guests who would book if rooms were available)
inline double latentDemandForOccupancy(double occupancy01) {
    return occupancy01 * static_cast<double>(Cfg::TOTAL_ROOMS) * 1.35;
}

std::vector<YieldCurvePoint> simulateYieldCurve(
    double baseReferencePrice,
    double baseDemandD0,
    double elasticityEpsilon,
    double& outOptimalSurge,
    double& outMaxRevenue
);

// =============================================================================
// 8. 7-DAY LINEAR REGRESSION FORECAST (Section 9.10 #4)
// =============================================================================
struct LinearRegressionForecast {
    double              slope          = 0.0;
    double              intercept      = 0.0;
    double              sigma          = 0.0;
    std::string         formulaText    = "";
    std::vector<double> predicted7Days = {};
    std::vector<double> upperBand7Days = {};
    std::vector<double> lowerBand7Days = {};
};

LinearRegressionForecast compute7DayForecast(const std::vector<double>& last30Values, const char* metricName = "Revenue");

// =============================================================================
// 9. HAND-WRITTEN VALIDATORS & LEVENSHTEIN DISTANCE (Section 9.4, Section 9.10 #1)
// =============================================================================
// Hand-written character scanning validators (no std::regex)
bool validateCnic(const std::string& cnic, std::string* outError = nullptr);
bool validatePakistaniPhone(const std::string& phone, std::string* outNormalized = nullptr, std::string* outError = nullptr);
bool validatePassport(const std::string& passport, std::string* outError = nullptr);
bool validateEmail(const std::string& email, std::string* outError = nullptr);
bool validatePersonName(const std::string& name, std::string* outError = nullptr);

// Dynamic Programming Levenshtein edit distance (case-insensitive) + fuzzy match score with prefix/substring bonus
int levenshteinDistance(const std::string& a, const std::string& b);
int computeFuzzyRankScore(const std::string& query, const std::string& candidate);

// =============================================================================
// 10. BENCHMARKS, STEP VISUALISERS & SELF-TEST SUITE (Section 9.10 #7, Section 11)
// =============================================================================
std::vector<SortBenchmarkRow> runSortingMicroBenchmarks();
std::vector<SortStepSnapshot> recordMergeSortSteps(const std::vector<int>& initialValues);
std::vector<SortStepSnapshot> recordMinHeapSteps(const std::vector<int>& prioritiesToPush);

// Runs all Phase 1 + Phase 2 self-tests (SHA-256, Audit Chain, MergeSort/QuickSort vs std::sort on 1000 ints,
// Binary Search hit/miss, Min-Heap non-decreasing pop order, CNIC/Phone/Email validators on 10 valid + 10 invalid,
// Folio tax/discount exact assertion, and Binary Save/Load round-trip)
std::vector<SelfTestEntry> runAllSelfTests(bool verboseConsole = true);

} // namespace hms

#endif // HMS_ALGORITHMS_H
