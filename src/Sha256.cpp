#include "Sha256.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <random>

namespace hms {

namespace {

/*
 * Purpose: Performs a 32-bit bitwise right rotation on x by n bits.
 * Inputs: x (32-bit unsigned word), n (bit shift count 1..31).
 * Outputs: Rotated 32-bit word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

/*
 * Purpose: Computes the SHA-256 Choose function Ch(x, y, z) = (x & y) ^ (~x & z).
 * Inputs: 32-bit words x, y, z.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t shaCh(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

/*
 * Purpose: Computes the SHA-256 Majority function Maj(x, y, z) = (x & y) ^ (x & z) ^ (y & z).
 * Inputs: 32-bit words x, y, z.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t shaMaj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

/*
 * Purpose: Computes FIPS 180-4 Upper Sigma 0: ROTR^2(x) ^ ROTR^13(x) ^ ROTR^22(x).
 * Inputs: 32-bit word x.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t bigSigma0(uint32_t x) {
    return rotr32(x, 2U) ^ rotr32(x, 13U) ^ rotr32(x, 22U);
}

/*
 * Purpose: Computes FIPS 180-4 Upper Sigma 1: ROTR^6(x) ^ ROTR^11(x) ^ ROTR^25(x).
 * Inputs: 32-bit word x.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t bigSigma1(uint32_t x) {
    return rotr32(x, 6U) ^ rotr32(x, 11U) ^ rotr32(x, 25U);
}

/*
 * Purpose: Computes FIPS 180-4 Lower sigma 0: ROTR^7(x) ^ ROTR^18(x) ^ SHR^3(x).
 * Inputs: 32-bit word x.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t smallSigma0(uint32_t x) {
    return rotr32(x, 7U) ^ rotr32(x, 18U) ^ (x >> 3U);
}

/*
 * Purpose: Computes FIPS 180-4 Lower sigma 1: ROTR^17(x) ^ ROTR^19(x) ^ SHR^10(x).
 * Inputs: 32-bit word x.
 * Outputs: 32-bit result word.
 * Big-O: O(1) time, O(1) space.
 */
inline uint32_t smallSigma1(uint32_t x) {
    return rotr32(x, 17U) ^ rotr32(x, 19U) ^ (x >> 10U);
}

/*
 * Purpose: Derives the first 64 prime numbers for FIPS 180-4 constant generation.
 * Inputs: None.
 * Outputs: std::array<int, 64> containing the first 64 primes (2 .. 311).
 * Big-O: O(1) time (bounded constant 64 primes), O(1) space.
 */
std::array<int, 64> getFirst64Primes() {
    std::array<int, 64> primes{};
    int count = 0;
    int candidate = 2;
    while (count < 64) {
        bool isPrime = true;
        for (int d = 2; d * d <= candidate; ++d) {
            if (candidate % d == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) {
            primes[static_cast<size_t>(count)] = candidate;
            ++count;
        }
        ++candidate;
    }
    return primes;
}

/*
 * Purpose: Computes the 8 initial hash values H0..H7 from the fractional parts of square roots of the first 8 primes.
 *          Matches FIPS 180-4 Section 5.3.3 exactly.
 * Inputs: None.
 * Outputs: std::array<uint32_t, 8> initial state vector.
 * Big-O: O(1) time, O(1) space.
 */
const std::array<uint32_t, 8>& getInitialHashValues() {
    static const std::array<uint32_t, 8> kInitH = []() {
        std::array<uint32_t, 8> h{};
        const std::array<int, 64> primes = getFirst64Primes();
        for (size_t i = 0; i < 8; ++i) {
            const double root = std::sqrt(static_cast<double>(primes[i]));
            const double frac = root - std::floor(root);
            h[i] = static_cast<uint32_t>(frac * 4294967296.0);
        }
        return h;
    }();
    return kInitH;
}

/*
 * Purpose: Computes the 64 round constants K[0..63] from the fractional parts of cube roots of the first 64 primes.
 *          Matches FIPS 180-4 Section 4.2.2 exactly.
 * Inputs: None.
 * Outputs: std::array<uint32_t, 64> round constants.
 * Big-O: O(1) time, O(1) space.
 */
const std::array<uint32_t, 64>& getRoundConstants() {
    static const std::array<uint32_t, 64> kRoundK = []() {
        std::array<uint32_t, 64> k{};
        const std::array<int, 64> primes = getFirst64Primes();
        for (size_t i = 0; i < 64; ++i) {
            const double root = std::cbrt(static_cast<double>(primes[i]));
            const double frac = root - std::floor(root);
            k[i] = static_cast<uint32_t>(frac * 4294967296.0);
        }
        return k;
    }();
    return kRoundK;
}

} // namespace

/*
 * Purpose: Computes the FIPS 180-4 SHA-256 message digest of an arbitrary input string.
 * Inputs: input (byte string to hash).
 * Outputs: 64-character lowercase hexadecimal digest string.
 * Big-O: O(N) time and O(N) space where N is input.size() bytes.
 */
std::string sha256Hex(const std::string& input) {
    const std::array<uint32_t, 64>& K = getRoundConstants();
    std::array<uint32_t, 8> H = getInitialHashValues();

    // Message padding: append 0x80, then 0x00 bytes until length ≡ 56 (mod 64), then 64-bit big-endian bit length
    const uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8ULL;
    std::vector<uint8_t> padded(input.begin(), input.end());
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0x00U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<uint8_t>((bitLength >> static_cast<uint64_t>(shift)) & 0xFFULL));
    }

    // Process each 512-bit (64-byte) block
    std::array<uint32_t, 64> W{};
    for (size_t offset = 0; offset < padded.size(); offset += 64U) {
        for (size_t t = 0; t < 16U; ++t) {
            const size_t idx = offset + t * 4U;
            W[t] = (static_cast<uint32_t>(padded[idx]) << 24U) |
                   (static_cast<uint32_t>(padded[idx + 1U]) << 16U) |
                   (static_cast<uint32_t>(padded[idx + 2U]) << 8U) |
                   (static_cast<uint32_t>(padded[idx + 3U]));
        }
        for (size_t t = 16U; t < 64U; ++t) {
            W[t] = smallSigma1(W[t - 2U]) + W[t - 7U] + smallSigma0(W[t - 15U]) + W[t - 16U];
        }

        uint32_t a = H[0];
        uint32_t b = H[1];
        uint32_t c = H[2];
        uint32_t d = H[3];
        uint32_t e = H[4];
        uint32_t f = H[5];
        uint32_t g = H[6];
        uint32_t h = H[7];

        for (size_t t = 0; t < 64U; ++t) {
            const uint32_t t1 = h + bigSigma1(e) + shaCh(e, f, g) + K[t] + W[t];
            const uint32_t t2 = bigSigma0(a) + shaMaj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    }

    char hexBuf[65]{};
    for (size_t i = 0; i < 8U; ++i) {
        std::snprintf(hexBuf + (i * 8U), 9U, "%08x", H[i]);
    }
    return std::string(hexBuf, 64U);
}

/*
 * Purpose: Generates a 16-byte (32 hex character) random cryptographic salt using std::random_device.
 * Inputs: None.
 * Outputs: 32-character lowercase hexadecimal salt string.
 * Big-O: O(1) time (16 bytes), O(1) space.
 */
std::string generateSaltHex() {
    std::random_device rd;
    static const char kHexDigits[] = "0123456789abcdef";
    std::string salt;
    salt.reserve(32U);
    for (int i = 0; i < 16; ++i) {
        const uint32_t byteVal = rd() & 0xFFU;
        salt.push_back(kHexDigits[(byteVal >> 4U) & 0x0FU]);
        salt.push_back(kHexDigits[byteVal & 0x0FU]);
    }
    return salt;
}

/*
 * Purpose: Applies SHA-256 iteratively 1,000 times over (saltHex + password).
 *          Note: This is an educational key-stretching demonstration for a 1st-semester PF course,
 *          not a production replacement for Argon2/PBKDF2.
 * Inputs: saltHex (32-char hex salt), password (plaintext user password).
 * Outputs: 64-character lowercase hex stretched password hash.
 * Big-O: O(1000 * |password|) = O(|password|) time, O(1) space.
 */
std::string hashPassword(const std::string& saltHex, const std::string& password) {
    std::string current = saltHex + password;
    for (int round = 0; round < 1000; ++round) {
        current = sha256Hex(current);
    }
    return current;
}

/*
 * Purpose: Verifies whether a candidate plaintext password hashes to expectedHashHex with saltHex.
 * Inputs: password, saltHex, expectedHashHex.
 * Outputs: true if hashes match identically, false otherwise.
 * Big-O: O(|password|) time, O(1) space.
 */
bool verifyPassword(const std::string& password, const std::string& saltHex, const std::string& expectedHashHex) {
    const std::string computed = hashPassword(saltHex, password);
    if (computed.size() != expectedHashHex.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < computed.size(); ++i) {
        diff |= static_cast<unsigned char>(computed[i] ^ expectedHashHex[i]);
    }
    return (diff == 0);
}

/*
 * Purpose: Returns the 64-zero genesis previous-hash string used by AuditEntry #0.
 * Inputs: None.
 * Outputs: std::string of 64 '0' characters.
 * Big-O: O(1) time, O(1) space.
 */
std::string genesisAuditHash() {
    return std::string(64U, '0');
}

/*
 * Purpose: Computes the chained SHA-256 hash for an AuditEntry per Section 6:
 *          SHA256(prevHash_i + "|" + timestamp + "|" + userId + "|" + action + "|" + details).
 * Inputs: prevHashHex, timestamp, userId, action, details.
 * Outputs: 64-char hex digest.
 * Big-O: O(|action| + |details|) time, O(|action| + |details|) space.
 */
std::string computeAuditEntryHash(
    const std::string& prevHashHex,
    long long timestamp,
    int userId,
    const std::string& action,
    const std::string& details
) {
    std::string payload;
    payload.reserve(prevHashHex.size() + action.size() + details.size() + 48U);
    payload.append(prevHashHex);
    payload.push_back('|');
    payload.append(std::to_string(timestamp));
    payload.push_back('|');
    payload.append(std::to_string(userId));
    payload.push_back('|');
    payload.append(action);
    payload.push_back('|');
    payload.append(details);
    return sha256Hex(payload);
}

/*
 * Purpose: Recomputes every entry in the audit hash chain to detect tampering.
 * Inputs: chain (vector of AuditEntry records in chronological order).
 * Outputs: -1 if all entries are valid; otherwise the 0-based index of the first tampered entry.
 * Big-O: O(N * L) time where N is number of entries and L is average entry length; O(1) auxiliary space.
 */
int verifyAuditChain(const std::vector<AuditEntry>& chain) {
    std::string expectedPrev = genesisAuditHash();
    for (size_t i = 0; i < chain.size(); ++i) {
        const AuditEntry& entry = chain[i];
        if (entry.prevHashHex != expectedPrev) {
            return static_cast<int>(i);
        }
        const std::string recomputed = computeAuditEntryHash(
            entry.prevHashHex,
            entry.timestamp,
            entry.userId,
            entry.action,
            entry.details
        );
        if (entry.hashHex != recomputed) {
            return static_cast<int>(i);
        }
        expectedPrev = entry.hashHex;
    }
    return -1;
}

/*
 * Purpose: Verifies the SHA-256 implementation against official FIPS 180-4 test vectors ("" and "abc").
 * Inputs: outDetails (optional pointer to receive diagnostic string).
 * Outputs: true if both vectors match exact FIPS 180-4 digests, false otherwise.
 * Big-O: O(1) time, O(1) space.
 */
bool runSha256VectorSelfTest(std::string* outDetails) {
    const std::string emptyDigest = sha256Hex("");
    const std::string abcDigest   = sha256Hex("abc");

    const std::string expectedEmpty =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const std::string expectedAbc =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    const bool okEmpty = (emptyDigest == expectedEmpty);
    const bool okAbc   = (abcDigest == expectedAbc);

    if (outDetails != nullptr) {
        if (okEmpty && okAbc) {
            *outDetails = "SHA-256('')=" + emptyDigest.substr(0, 12) + "...b855, SHA-256('abc')=" +
                          abcDigest.substr(0, 12) + "...15ad";
        } else {
            *outDetails = "Vector mismatch: empty=" + emptyDigest + ", abc=" + abcDigest;
        }
    }
    return okEmpty && okAbc;
}

} // namespace hms
