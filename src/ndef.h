#pragma once
// NDEF encoding/decoding for NFC Forum Type 2 tags (NTAG, Ultralight).
// Plain C++ with no Arduino dependency, so it is unit tested on the host (pio test -e native).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ndef {

struct Record {
  std::string type;         // "url", "text" or "other"
  std::string value;        // decoded URL / text, or payload as hex for "other"
  std::string lang;         // "text" only
  uint8_t tnf = 1;          // "other" only
  std::string recordType;   // "other" only
};

// Builds an NDEF message TLV (03 <len> <records> FE) from url/text records.
// Returns an empty string on success, or a human-readable error.
std::string encode(const std::vector<Record> &records, std::vector<uint8_t> &out);

// Decodes the records of the first NDEF message TLV found in a tag's user area
// (bytes starting at page 4).
std::vector<Record> decode(const uint8_t *data, size_t len);

// Number of bytes before the NDEF message TLV (Lock / Memory Control TLVs that must be kept
// in place). `head` is the start of the user area; returns 0 for unexpected layouts.
size_t prefixLength(const uint8_t *head, size_t len);

}  // namespace ndef
