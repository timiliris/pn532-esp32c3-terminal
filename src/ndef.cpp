#include "ndef.h"

namespace ndef {

// URI identifier codes (NFC Forum URI RTD), most specific prefix first wins
static const char *const URI_PREFIXES[] = {
  "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
};
static const size_t URI_COUNT = sizeof URI_PREFIXES / sizeof URI_PREFIXES[0];

static bool startsWith(const std::string &s, const char *p) {
  return s.compare(0, std::char_traits<char>::length(p), p) == 0;
}

static std::string toHex(const uint8_t *b, size_t n) {
  static const char *H = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 0xF]; }
  return s;
}

std::string encode(const std::vector<Record> &records, std::vector<uint8_t> &out) {
  if (records.empty()) return "records: at least one record is required";
  std::vector<uint8_t> msg;
  for (size_t i = 0; i < records.size(); i++) {
    const Record &r = records[i];
    std::string where = "records[" + std::to_string(i) + "]";
    if (r.value.empty()) return where + ".value is empty";

    std::vector<uint8_t> payload;
    char type;
    if (r.type == "url") {
      type = 'U';
      size_t code = 0;
      for (size_t k = 1; k < URI_COUNT; k++) {
        if (startsWith(r.value, URI_PREFIXES[k]) &&
            std::char_traits<char>::length(URI_PREFIXES[k]) > std::char_traits<char>::length(URI_PREFIXES[code])) {
          code = k;
        }
      }
      payload.push_back((uint8_t)code);
      payload.insert(payload.end(), r.value.begin() + std::char_traits<char>::length(URI_PREFIXES[code]), r.value.end());
    } else if (r.type == "text") {
      type = 'T';
      std::string lang = r.lang.empty() ? "en" : r.lang;
      if (lang.size() > 63) return where + ".lang is too long";
      payload.push_back((uint8_t)lang.size());  // status byte: UTF-8, language length
      payload.insert(payload.end(), lang.begin(), lang.end());
      payload.insert(payload.end(), r.value.begin(), r.value.end());
    } else {
      return where + ".type must be \"url\" or \"text\"";
    }

    bool shortRecord = payload.size() < 256;
    uint8_t header = 0x01;                          // TNF: well-known type
    if (i == 0) header |= 0x80;                     // MB
    if (i == records.size() - 1) header |= 0x40;    // ME
    if (shortRecord) header |= 0x10;                // SR
    msg.push_back(header);
    msg.push_back(1);                               // type length
    if (shortRecord) {
      msg.push_back((uint8_t)payload.size());
    } else {
      for (int k = 3; k >= 0; k--) msg.push_back((payload.size() >> (8 * k)) & 0xFF);
    }
    msg.push_back((uint8_t)type);
    msg.insert(msg.end(), payload.begin(), payload.end());
  }

  out.clear();
  out.push_back(0x03);
  if (msg.size() < 0xFF) {
    out.push_back((uint8_t)msg.size());
  } else {
    out.push_back(0xFF);
    out.push_back(msg.size() >> 8);
    out.push_back(msg.size() & 0xFF);
  }
  out.insert(out.end(), msg.begin(), msg.end());
  out.push_back(0xFE);
  if (out.size() > 888) return "message too long (" + std::to_string(out.size()) + " bytes, 888 max)";
  return "";
}

std::vector<Record> decode(const uint8_t *b, size_t len) {
  std::vector<Record> out;
  size_t i = 0;
  while (i < len) {
    uint8_t t = b[i];
    if (t == 0x00) { i++; continue; }          // NULL TLV
    if (t == 0xFE || i + 1 >= len) break;      // terminator
    size_t vlen = b[i + 1], o = i + 2;
    if (vlen == 0xFF) {
      if (i + 3 >= len) break;
      vlen = (size_t)b[i + 2] << 8 | b[i + 3];
      o = i + 4;
    }
    if (t != 0x03) { i = o + vlen; continue; } // other TLV: skip

    size_t end = o + vlen < len ? o + vlen : len, p = o;
    while (p + 2 < end) {
      uint8_t hd = b[p++];
      uint8_t tl = b[p++];
      size_t pl;
      if (hd & 0x10) {
        pl = b[p++];
      } else {
        if (p + 4 > end) return out;
        pl = (size_t)b[p] << 24 | (size_t)b[p + 1] << 16 | (size_t)b[p + 2] << 8 | b[p + 3];
        p += 4;
      }
      uint8_t il = (hd & 0x08) ? b[p++] : 0;
      if (p + tl + il + pl > end) return out;
      std::string rtype(b + p, b + p + tl);
      p += tl + il;

      Record rec;
      uint8_t tnf = hd & 0x07;
      if (tnf == 1 && rtype == "U" && pl >= 1) {
        rec.type = "url";
        rec.value = b[p] < URI_COUNT ? URI_PREFIXES[b[p]] : "";
        rec.value.append(b + p + 1, b + p + pl);
      } else if (tnf == 1 && rtype == "T" && pl >= 1) {
        size_t ll = b[p] & 0x3F;
        if (1 + ll > pl) ll = pl - 1;
        rec.type = "text";
        rec.lang.assign(b + p + 1, b + p + 1 + ll);
        rec.value.assign(b + p + 1 + ll, b + p + pl);
      } else {
        rec.type = "other";
        rec.tnf = tnf;
        rec.recordType = rtype;
        rec.value = toHex(b + p, pl < 64 ? pl : 64);
      }
      out.push_back(rec);
      p += pl;
      if (hd & 0x40) break;  // ME: last record
    }
    break;  // only the first NDEF message
  }
  return out;
}

size_t prefixLength(const uint8_t *head, size_t len) {
  size_t off = 0;
  while (off < len) {
    uint8_t t = head[off];
    if (t == 0x00) { off++; continue; }
    if ((t == 0x01 || t == 0x02) && off + 1 < len) { off += 2 + head[off + 1]; continue; }
    break;
  }
  return off < len ? off : 0;
}

}  // namespace ndef
