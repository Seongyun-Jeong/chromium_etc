// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/mock_encrypter.h"

#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"

using quic::DiversificationNonce;
using quic::Perspective;
using quic::QuicPacketNumber;

namespace net {

namespace {

const size_t kPaddingSize = 12;

}  // namespace

MockEncrypter::MockEncrypter(Perspective perspective) {}

bool MockEncrypter::SetKey(absl::string_view key) {
  return key.empty();
}

bool MockEncrypter::SetNoncePrefix(absl::string_view nonce_prefix) {
  return nonce_prefix.empty();
}

bool MockEncrypter::SetIV(absl::string_view iv) {
  return iv.empty();
}

bool MockEncrypter::EncryptPacket(uint64_t /*packet_number*/,
                                  absl::string_view associated_data,
                                  absl::string_view plaintext,
                                  char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  size_t ciphertext_size = plaintext.size() + kPaddingSize;
  if (max_output_length < ciphertext_size) {
    return false;
  }
  memcpy(output, plaintext.data(), ciphertext_size);
  *output_length = ciphertext_size;
  return true;
}

bool MockEncrypter::SetHeaderProtectionKey(absl::string_view key) {
  return key.empty();
}

std::string MockEncrypter::GenerateHeaderProtectionMask(
    absl::string_view sample) {
  return std::string(5, 0);
}

size_t MockEncrypter::GetKeySize() const {
  return 0;
}

size_t MockEncrypter::GetNoncePrefixSize() const {
  return 0;
}

size_t MockEncrypter::GetIVSize() const {
  return 0;
}

size_t MockEncrypter::GetMaxPlaintextSize(size_t ciphertext_size) const {
  return ciphertext_size - kPaddingSize;
}

size_t MockEncrypter::GetCiphertextSize(size_t plaintext_size) const {
  return plaintext_size + kPaddingSize;
}

quic::QuicPacketCount MockEncrypter::GetConfidentialityLimit() const {
  return std::numeric_limits<quic::QuicPacketCount>::max();
}

absl::string_view MockEncrypter::GetKey() const {
  return absl::string_view();
}

absl::string_view MockEncrypter::GetNoncePrefix() const {
  return absl::string_view();
}

}  // namespace net