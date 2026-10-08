#include <unity.h>
#include "ndef.h"

using ndef::Record;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> enc(const std::vector<Record> &r) {
  std::vector<uint8_t> out;
  std::string err = ndef::encode(r, out);
  TEST_ASSERT_EQUAL_STRING("", err.c_str());
  return out;
}

static void test_url_uses_shortest_encoding() {
  auto out = enc({{"url", "https://example.com"}});
  // 03 len | D1 01 len 'U' 04 example.com | FE
  const uint8_t expected[] = {0x03, 0x10, 0xD1, 0x01, 0x0C, 'U', 0x04,
                              'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 0xFE};
  TEST_ASSERT_EQUAL_UINT(sizeof expected, out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data(), sizeof expected);
}

static void test_https_www_prefix_wins_over_https() {
  auto out = enc({{"url", "https://www.a.b"}});
  TEST_ASSERT_EQUAL_HEX8(0x02, out[6]);
}

static void test_round_trip_multiple_records() {
  Record text{"text", "Bonjour é", "fr"};
  auto out = enc({{"url", "tel:+33123"}, text});
  // MB on the first record only, ME on the last only
  TEST_ASSERT_EQUAL_HEX8(0x91, out[2]);
  auto recs = ndef::decode(out.data(), out.size());
  TEST_ASSERT_EQUAL(2, recs.size());
  TEST_ASSERT_EQUAL_STRING("url", recs[0].type.c_str());
  TEST_ASSERT_EQUAL_STRING("tel:+33123", recs[0].value.c_str());
  TEST_ASSERT_EQUAL_STRING("text", recs[1].type.c_str());
  TEST_ASSERT_EQUAL_STRING("fr", recs[1].lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Bonjour é", recs[1].value.c_str());
}

static void test_long_record_and_three_byte_tlv_length() {
  std::string big(300, 'x');
  auto out = enc({{"text", big}});
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);           // 3-byte TLV length
  TEST_ASSERT_EQUAL_HEX8(0xC1, out[4]);           // MB|ME, not short
  auto recs = ndef::decode(out.data(), out.size());
  TEST_ASSERT_EQUAL(1, recs.size());
  TEST_ASSERT_EQUAL(300, recs[0].value.size());
}

static void test_rejects_invalid_input() {
  std::vector<uint8_t> out;
  TEST_ASSERT_TRUE(ndef::encode({}, out).size() > 0);
  TEST_ASSERT_TRUE(ndef::encode({{"mime", "x"}}, out).size() > 0);
  TEST_ASSERT_TRUE(ndef::encode({{"url", ""}}, out).size() > 0);
  TEST_ASSERT_TRUE(ndef::encode({{"text", std::string(900, 'x')}}, out).size() > 0);
}

static void test_factory_ntag213_layout() {
  // Lock Control TLV, then an empty NDEF message: what a new NTAG213 holds at page 4
  const uint8_t user[] = {0x01, 0x03, 0xA0, 0x0C, 0x34, 0x03, 0x00, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL(5, ndef::prefixLength(user, sizeof user));
  TEST_ASSERT_EQUAL(0, ndef::decode(user, sizeof user).size());
}

static void test_prefix_of_blank_area_is_zero() {
  const uint8_t user[16] = {0};
  TEST_ASSERT_EQUAL(0, ndef::prefixLength(user, sizeof user));
}

static void test_decode_ignores_truncated_data() {
  auto out = enc({{"url", "https://example.com/a/long/path"}});
  auto recs = ndef::decode(out.data(), out.size() - 10);
  TEST_ASSERT_EQUAL(0, recs.size());
}

static void test_unknown_record_is_reported_as_other() {
  const uint8_t msg[] = {0x03, 0x08, 0xD2, 0x03, 0x02, 'a', '/', 'b', 0xCA, 0xFE, 0xFE};
  auto recs = ndef::decode(msg, sizeof msg);
  TEST_ASSERT_EQUAL(1, recs.size());
  TEST_ASSERT_EQUAL_STRING("other", recs[0].type.c_str());
  TEST_ASSERT_EQUAL(2, recs[0].tnf);
  TEST_ASSERT_EQUAL_STRING("a/b", recs[0].recordType.c_str());
  TEST_ASSERT_EQUAL_STRING("CAFE", recs[0].value.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_url_uses_shortest_encoding);
  RUN_TEST(test_https_www_prefix_wins_over_https);
  RUN_TEST(test_round_trip_multiple_records);
  RUN_TEST(test_long_record_and_three_byte_tlv_length);
  RUN_TEST(test_rejects_invalid_input);
  RUN_TEST(test_factory_ntag213_layout);
  RUN_TEST(test_prefix_of_blank_area_is_zero);
  RUN_TEST(test_decode_ignores_truncated_data);
  RUN_TEST(test_unknown_record_is_reported_as_other);
  return UNITY_END();
}
