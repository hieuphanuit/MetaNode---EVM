// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "eEVM/util.h"

#include "eEVM/rlp.h"

#include <iomanip>

using namespace std;

namespace eevm
{
  string strip(const string& s)
  {
    return (s.size() >= 2 && s[1] == 'x') ? s.substr(2) : s;
  }

  uint64_t to_uint64(const std::string& s)
  {
    return strtoull(s.c_str(), nullptr, 16);
  }

  vector<uint8_t> to_bytes(const string& _s)
  {
    auto s = strip(_s);

    const size_t byte_len = (s.size() + 1) / 2; // round up
    vector<uint8_t> v(byte_len);

    // Handle odd-length strings
    size_t n = 0;
    if (s.size() % 2 != 0)
    {
      v[0] = static_cast<uint8_t>(strtoul(s.substr(0, 1).c_str(), nullptr, 16));
      ++n;
    }

    auto x = n;
    for (auto i = n; i < byte_len; ++i, x += 2)
    {
      v[i] = static_cast<uint8_t>(strtoul(s.substr(x, 2).c_str(), nullptr, 16));
    }
    return v;
  }

  Address generate_address(const Address& sender, uint64_t nonce)
  {
    const auto rlp_encoding = rlp::encode(sender, nonce);

    uint8_t buffer[32u];
    keccak_256(
      rlp_encoding.data(),
      static_cast<unsigned int>(rlp_encoding.size()),
      buffer);

    return from_big_endian(buffer + 12u, 20u);
  }

  Address generate_contract_address(const Address& sender, uint256_t sender_last_hash)
  {
    uint8_t byte_data [52u] = {};
    std::memcpy(byte_data, &sender, 20);
    std::memcpy(byte_data+sizeof(&sender), &sender_last_hash, 32);
    
    uint8_t buffer[32u] = {};
    keccak_256(
      (unsigned char* )byte_data,
      static_cast<unsigned int>(sizeof(byte_data)),
      buffer);

    return from_big_endian(buffer + 12u, 20u);
  }

  void print_hex(uint8_t *s, size_t len) {
      for(size_t i = 0; i < len; i++) {
          printf("%02x", s[i]);
      }
      printf("\n");
  }
} // namespace eevm
