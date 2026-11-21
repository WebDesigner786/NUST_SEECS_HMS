#ifndef HMS_SHA256_H
#define HMS_SHA256_H

#include "Models.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hms {

// =============================================================================
// FIPS 180-4 SHA-256, Key Stretching & Audit Hash Chain (Section 6)
// =============================================================================

// Computes standard FIPS 180-4 SHA-256 digest of input string as 64 lowercase hex characters
std::string sha256Hex(const std::string& input);

// Generates 16 random bytes (32 hex chars) using std::random_device for password salting
std::string generateSaltHex();

// Educational key-stretch: applies SHA-256 1,000 times iteratively over (saltHex + password)
std::string hashPassword(const std::string& saltHex, const std::string& password);

// Verifies a plaintext password against a stored User's saltHex and passHashHex
bool verifyPassword(const std::string& password, const std::string& saltHex, const std::string& expectedHashHex);

// Returns the 64-zero genesis hash string required for the first audit log entry
std::string genesisAuditHash();

// Computes hash_i = SHA256(prevHash_i + "|" + timestamp + "|" + userId + "|" + action + "|" + details)
std::string computeAuditEntryHash(
    const std::string& prevHashHex,
    long long timestamp,
    int userId,
    const std::string& action,
    const std::string& details
);

// Recomputes every entry in the audit log; returns -1 if valid, or the 0-based index of the first tampered entry
int verifyAuditChain(const std::vector<AuditEntry>& chain);

// Runs FIPS 180-4 SHA-256 known-answer vector self-tests ("" and "abc")
bool runSha256VectorSelfTest(std::string* outDetails = nullptr);

} // namespace hms

#endif // HMS_SHA256_H
