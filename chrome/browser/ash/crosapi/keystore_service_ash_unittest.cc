// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/crosapi/keystore_service_ash.h"

#include <initializer_list>

#include "base/base64.h"
#include "base/bind.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/gmock_move_support.h"
#include "chrome/browser/ash/attestation/mock_tpm_challenge_key.h"
#include "chrome/browser/ash/attestation/tpm_challenge_key_result.h"
#include "chrome/browser/ash/platform_keys/key_permissions/mock_key_permissions_service.h"
#include "chrome/browser/ash/platform_keys/mock_platform_keys_service.h"
#include "chrome/browser/platform_keys/platform_keys.h"
#include "chromeos/crosapi/cpp/keystore_service_util.h"
#include "chromeos/crosapi/mojom/keystore_error.mojom.h"
#include "chromeos/crosapi/mojom/keystore_service.mojom.h"
#include "content/public/test/browser_task_environment.h"
#include "net/cert/asn1_util.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/ssl/ssl_info.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "testing/gmock/include/gmock/gmock-actions.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

// The tests in this file mostly focus on verifying that KeystoreService can
// forward messages to and from PlatformKeysService, KeyPermissionsService,
// TpmChallengeKey and correctly re-encode arguments in both directions.

namespace crosapi {
namespace {

using ::ash::platform_keys::MockKeyPermissionsService;
using ::ash::platform_keys::MockPlatformKeysService;
using ::base::test::RunOnceCallback;
using ::chromeos::platform_keys::HashAlgorithm;
using ::chromeos::platform_keys::Status;
using ::chromeos::platform_keys::TokenId;
using ::crosapi::keystore_service_util::MakeEcKeystoreSigningAlgorithm;
using ::crosapi::keystore_service_util::MakeRsaKeystoreSigningAlgorithm;
using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAre;
using ::testing::WithArg;

constexpr char kData[] = "\1\2\3\4\5\6\7";

std::string Base64Decode(const char* input) {
  std::string result;
  CHECK(base::Base64Decode(input, &result));
  return result;
}

std::string GetSubjectPublicKeyInfo(
    const scoped_refptr<net::X509Certificate>& certificate) {
  base::StringPiece spki_der_piece;
  bool ok = net::asn1::ExtractSPKIFromDERCert(
      net::x509_util::CryptoBufferAsStringPiece(certificate->cert_buffer()),
      &spki_der_piece);
  CHECK(ok && !spki_der_piece.empty());
  return std::string(spki_der_piece);
}

// Returns a list with one certificate.
std::unique_ptr<net::CertificateList> GetCertificateList() {
  static base::NoDestructor<net::CertificateList> cert_list;
  if (cert_list->empty()) {
    net::SSLInfo ssl_info = net::SSLInfo();
    ssl_info.cert =
        net::ImportCertFromFile(net::GetTestCertsDirectory(), "ok_cert.pem");
    CHECK(ssl_info.is_valid());
    cert_list->push_back(ssl_info.cert);
  }
  return std::make_unique<net::CertificateList>(*cert_list);
}

const std::string& GetPublicKeyStr() {
  static base::NoDestructor<const std::string> result(
      GetSubjectPublicKeyInfo(*GetCertificateList()->begin()));
  return *result;
}

const std::vector<uint8_t>& GetPublicKeyBin() {
  static base::NoDestructor<const std::vector<uint8_t>> result(
      GetPublicKeyStr().begin(), GetPublicKeyStr().end());
  return *result;
}

const std::string& GetDataStr() {
  static base::NoDestructor<const std::string> result(kData);
  return *result;
}

const std::vector<uint8_t>& GetDataBin() {
  static base::NoDestructor<const std::vector<uint8_t>> result(
      GetDataStr().begin(), GetDataStr().end());
  return *result;
}

std::vector<uint8_t> CertToBlob(
    const scoped_refptr<net::X509Certificate>& cert) {
  const uint8_t* cert_buffer =
      reinterpret_cast<const uint8_t*>(CRYPTO_BUFFER_data(cert->cert_buffer()));
  return std::vector<uint8_t>(
      cert_buffer, cert_buffer + CRYPTO_BUFFER_len(cert->cert_buffer()));
}

std::unique_ptr<std::vector<TokenId>> MakeTokenIds(
    std::initializer_list<TokenId> init_list) {
  return std::make_unique<std::vector<TokenId>>(init_list);
}

void AssertBlobEq(const mojom::KeystoreBinaryResultPtr& result,
                  const std::vector<uint8_t>& expected_blob) {
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_blob());
  EXPECT_EQ(result->get_blob(), expected_blob);
}

void AssertCertListEq(
    const std::vector<std::vector<uint8_t>>& received_cert_list,
    std::unique_ptr<net::CertificateList> expected_cert_list) {
  ASSERT_EQ(received_cert_list.size(), expected_cert_list->size());
  for (size_t i = 0; i < received_cert_list.size(); ++i) {
    const scoped_refptr<net::X509Certificate>& expected_cert =
        (*expected_cert_list)[i];

    const std::vector<uint8_t>& received_binary_cert = received_cert_list[i];
    scoped_refptr<net::X509Certificate> received_cert =
        net::X509Certificate::CreateFromBytes(received_binary_cert);
    ASSERT_TRUE(received_cert);

    EXPECT_TRUE(expected_cert->EqualsIncludingChain(received_cert.get()));
  }
}

template <typename T>
void AssertErrorEq(const T& result, mojom::KeystoreError expected_error) {
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), expected_error);
}

// Matches a certificate of the type `scoped_refptr<net::X509Certificate>`.
MATCHER_P(CertEq, expected_cert, "Certificates don't match.") {
  return expected_cert && arg && expected_cert->EqualsIncludingChain(arg.get());
}

// Matches strings that start with `expected_prefix`.
MATCHER_P(StrStartsWith, expected_prefix, "Unexpected string.") {
  return base::StartsWith(arg, expected_prefix);
}

class KeystoreServiceAshTest : public testing::Test {
 public:
  KeystoreServiceAshTest()
      : keystore_service_(&platform_keys_service_, &key_permissions_service_) {}
  KeystoreServiceAshTest(const KeystoreServiceAshTest&) = delete;
  auto operator=(const KeystoreServiceAshTest&) = delete;
  ~KeystoreServiceAshTest() override = default;

 protected:
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  StrictMock<MockPlatformKeysService> platform_keys_service_;
  StrictMock<MockKeyPermissionsService> key_permissions_service_;
  KeystoreServiceAsh keystore_service_;
};

// A mock for observing callbacks that return a single result of the type |T|
// and saving it.
template <typename T>
struct CallbackObserver {
  MOCK_METHOD(void, Callback, (T result));

  auto GetCallback() {
    EXPECT_CALL(*this, Callback).WillOnce(MoveArg<0>(&result));
    return base::BindOnce(&CallbackObserver<T>::Callback,
                          base::Unretained(this));
  }

  absl::optional<T> result;
};

// A mock for observing callbacks that return a single result of the type |T| by
// const reference and saving it.
template <typename T>
struct CallbackObserverRef {
  MOCK_METHOD(void, Callback, (const T& result));

  auto GetCallback() {
    EXPECT_CALL(*this, Callback).WillOnce(testing::SaveArg<0>(&result));
    return base::BindOnce(&CallbackObserverRef<T>::Callback,
                          base::Unretained(this));
  }

  absl::optional<T> result;
};

// A mock for observing status results returned via a callback.
struct StatusCallbackObserver {
  MOCK_METHOD(void, Callback, (bool is_error, mojom::KeystoreError error));

  auto GetCallback() {
    EXPECT_CALL(*this, Callback)
        .WillOnce(
            DoAll(MoveArg<0>(&result_is_error), MoveArg<1>(&result_error)));
    return base::BindOnce(&StatusCallbackObserver::Callback,
                          base::Unretained(this));
  }

  bool has_value() const { return result_is_error.has_value(); }

  absl::optional<bool> result_is_error;
  mojom::KeystoreError result_error = mojom::KeystoreError::kUnknown;
};

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, GenerateUserRsaKeySuccess) {
  const unsigned int modulus_length = 2048;

  EXPECT_CALL(
      platform_keys_service_,
      GenerateRSAKey(TokenId::kUser, modulus_length, /*sw_backed=*/false,
                     /*callback=*/_))
      .WillOnce(RunOnceCallback<3>(GetPublicKeyStr(), Status::kSuccess));
  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.GenerateKey(
      mojom::KeystoreType::kUser,
      MakeRsaKeystoreSigningAlgorithm(modulus_length, /*sw_backed=*/false),
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertBlobEq(observer.result.value(), GetPublicKeyBin());
}

TEST_F(KeystoreServiceAshTest, GenerateDeviceEcKeySuccess) {
  const std::string named_curve = "test_named_curve";

  EXPECT_CALL(platform_keys_service_,
              GenerateECKey(TokenId::kSystem, named_curve, /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(GetPublicKeyStr(), Status::kSuccess));

  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.GenerateKey(mojom::KeystoreType::kDevice,
                                MakeEcKeystoreSigningAlgorithm(named_curve),
                                observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertBlobEq(observer.result.value(), GetPublicKeyBin());
}

TEST_F(KeystoreServiceAshTest, GenerateKeyFail) {
  EXPECT_CALL(platform_keys_service_, GenerateECKey)
      .WillOnce(RunOnceCallback<2>("", Status::kErrorInternal));

  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.GenerateKey(mojom::KeystoreType::kUser,
                                MakeEcKeystoreSigningAlgorithm("named_curve_1"),
                                observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(), mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, SignRsaSuccess) {
  // Accepted and returned data are the same. This is not realistic, but doesn't
  // matter here.
  EXPECT_CALL(platform_keys_service_,
              SignRSAPKCS1Digest(absl::optional<TokenId>(TokenId::kUser),
                                 GetDataStr(), GetPublicKeyStr(),
                                 HashAlgorithm::HASH_ALGORITHM_SHA256,
                                 /*callback=*/_))
      .WillOnce(RunOnceCallback<4>(GetDataStr(), Status::kSuccess));

  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.Sign(
      /*is_keystore_provided=*/true, mojom::KeystoreType::kUser,
      GetPublicKeyBin(), mojom::KeystoreSigningScheme::kRsassaPkcs1V15Sha256,
      GetDataBin(), observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertBlobEq(observer.result.value(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, SignEcSuccess) {
  // Accepted and returned data are the same. This is not realistic, but doesn't
  // matter here.
  EXPECT_CALL(
      platform_keys_service_,
      SignECDSADigest(absl::optional<TokenId>(TokenId::kSystem), GetDataStr(),
                      GetPublicKeyStr(), HashAlgorithm::HASH_ALGORITHM_SHA512,
                      /*callback=*/_))
      .WillOnce(RunOnceCallback<4>(GetDataStr(), Status::kSuccess));

  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.Sign(
      /*is_keystore_provided=*/true, mojom::KeystoreType::kDevice,
      GetPublicKeyBin(), mojom::KeystoreSigningScheme::kEcdsaSha512,
      GetDataBin(), observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertBlobEq(observer.result.value(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, SignFail) {
  EXPECT_CALL(platform_keys_service_, SignECDSADigest)
      .WillOnce(RunOnceCallback<4>("", Status::kErrorKeyNotAllowedForSigning));

  CallbackObserver<mojom::KeystoreBinaryResultPtr> observer;
  keystore_service_.Sign(
      /*is_keystore_provided=*/true, mojom::KeystoreType::kDevice,
      GetPublicKeyBin(), mojom::KeystoreSigningScheme::kEcdsaSha512,
      GetDataBin(), observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(),
                mojom::KeystoreError::kKeyNotAllowedForSigning);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, RemoveKeySuccess) {
  EXPECT_CALL(platform_keys_service_,
              RemoveKey(TokenId::kSystem, GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kSuccess));

  StatusCallbackObserver observer;
  keystore_service_.RemoveKey(mojom::KeystoreType::kDevice, GetPublicKeyBin(),
                              observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, false);
}

TEST_F(KeystoreServiceAshTest, RemoveKeyFail) {
  EXPECT_CALL(platform_keys_service_,
              RemoveKey(TokenId::kSystem, GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kErrorKeyNotFound));

  StatusCallbackObserver observer;
  keystore_service_.RemoveKey(mojom::KeystoreType::kDevice, GetPublicKeyBin(),
                              observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, true);
  EXPECT_EQ(observer.result_error, mojom::KeystoreError::kKeyNotFound);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, SelectClientCertificatesSuccess) {
  std::vector<std::vector<uint8_t>> cert_authorities_bin = {
      {1, 2, 3}, {2, 3, 4}, {3, 4, 5}};
  std::vector<std::string> cert_authorities_str = {"\1\2\3", "\2\3\4",
                                                   "\3\4\5"};

  EXPECT_CALL(platform_keys_service_,
              SelectClientCertificates(cert_authorities_str,
                                       /*callback=*/_))
      .WillOnce(WithArg<1>([](auto callback) {
        std::move(callback).Run(GetCertificateList(), Status::kSuccess);
      }));

  CallbackObserver<mojom::KeystoreSelectClientCertificatesResultPtr> observer;
  keystore_service_.SelectClientCertificates(cert_authorities_bin,
                                             observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_certificates());
  AssertCertListEq(observer.result.value()->get_certificates(),
                   GetCertificateList());
}

TEST_F(KeystoreServiceAshTest, SelectClientCertificatesFail) {
  EXPECT_CALL(platform_keys_service_, SelectClientCertificates)
      .WillOnce(WithArg<1>([](auto callback) {
        std::move(callback).Run({}, Status::kErrorInternal);
      }));

  CallbackObserver<mojom::KeystoreSelectClientCertificatesResultPtr> observer;
  keystore_service_.SelectClientCertificates({}, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(), mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, GetKeyTagsSuccess) {
  EXPECT_CALL(key_permissions_service_,
              IsCorporateKey(GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(
          RunOnceCallback<1>(absl::optional<bool>(true), Status::kSuccess));

  CallbackObserver<mojom::GetKeyTagsResultPtr> observer;
  keystore_service_.GetKeyTags(GetPublicKeyBin(), observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_tags());
  EXPECT_EQ(observer.result.value()->get_tags(),
            static_cast<uint64_t>(mojom::KeyTag::kCorporate));
}

TEST_F(KeystoreServiceAshTest, GetKeyTagsFail) {
  EXPECT_CALL(key_permissions_service_, IsCorporateKey)
      .WillOnce(RunOnceCallback<1>(absl::nullopt, Status::kErrorInternal));

  CallbackObserver<mojom::GetKeyTagsResultPtr> observer;
  keystore_service_.GetKeyTags(GetPublicKeyBin(), observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(), mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, AddKeyTagsSuccess) {
  const uint64_t tags = static_cast<uint64_t>(mojom::KeyTag::kCorporate);

  EXPECT_CALL(key_permissions_service_,
              SetCorporateKey(GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(Status::kSuccess));

  StatusCallbackObserver observer;
  keystore_service_.AddKeyTags(GetPublicKeyBin(), tags, observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, false);
}

TEST_F(KeystoreServiceAshTest, AddKeyTagsFail) {
  const uint64_t tags = static_cast<uint64_t>(mojom::KeyTag::kCorporate);

  EXPECT_CALL(key_permissions_service_,
              SetCorporateKey(GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(Status::kErrorInternal));

  StatusCallbackObserver observer;
  keystore_service_.AddKeyTags(GetPublicKeyBin(), tags, observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, true);
  EXPECT_EQ(observer.result_error, mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, CanUserGrantPermissionForKey) {
  EXPECT_CALL(key_permissions_service_,
              CanUserGrantPermissionForKey(GetPublicKeyStr(), /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(false));

  CallbackObserver<bool> observer;
  keystore_service_.CanUserGrantPermissionForKey(GetPublicKeyBin(),
                                                 observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value());
  EXPECT_EQ(observer.result, false);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, GetPublicKeySuccess) {
  const std::vector<uint8_t> cert_bin =
      CertToBlob(GetCertificateList()->front());

  CallbackObserver<mojom::GetPublicKeyResultPtr> observer;
  keystore_service_.GetPublicKey(
      cert_bin, mojom::KeystoreSigningAlgorithmName::kRsassaPkcs115,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());

  ASSERT_TRUE(observer.result.value()->is_success_result());
  const mojom::GetPublicKeySuccessResultPtr& success_result =
      observer.result.value()->get_success_result();
  ASSERT_EQ(success_result->public_key, GetPublicKeyBin());

  ASSERT_TRUE(success_result->algorithm_properties->is_pkcs115());
  const mojom::KeystorePKCS115ParamsPtr& params =
      success_result->algorithm_properties->get_pkcs115();
  EXPECT_EQ(params->modulus_length, 2048);
  EXPECT_EQ(params->public_exponent, (std::vector<uint8_t>{1, 0, 1}));
}

TEST_F(KeystoreServiceAshTest, GetPublicKeyFail) {
  const std::vector<uint8_t> cert_bin =
      CertToBlob(GetCertificateList()->front());

  CallbackObserver<mojom::GetPublicKeyResultPtr> observer;
  keystore_service_.GetPublicKey(cert_bin,
                                 mojom::KeystoreSigningAlgorithmName::kUnknown,
                                 observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(),
                mojom::KeystoreError::kAlgorithmNotPermittedByCertificate);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, GetKeyStoresEmptySuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({}), Status::kSuccess));

  CallbackObserver<mojom::GetKeyStoresResultPtr> observer;
  keystore_service_.GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_TRUE(observer.result.value()->get_key_stores().empty());
}

TEST_F(KeystoreServiceAshTest, GetKeyStoresUserSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(
          RunOnceCallback<0>(MakeTokenIds({TokenId::kUser}), Status::kSuccess));

  CallbackObserver<mojom::GetKeyStoresResultPtr> observer;
  keystore_service_.GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              ElementsAre(crosapi::mojom::KeystoreType::kUser));
}

TEST_F(KeystoreServiceAshTest, GetKeyStoresDeviceSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({TokenId::kSystem}),
                                   Status::kSuccess));

  CallbackObserver<mojom::GetKeyStoresResultPtr> observer;
  keystore_service_.GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              ElementsAre(crosapi::mojom::KeystoreType::kDevice));
}

TEST_F(KeystoreServiceAshTest, GetKeyStoresDeviceUserSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(
          MakeTokenIds({TokenId::kUser, TokenId::kSystem}), Status::kSuccess));

  CallbackObserver<mojom::GetKeyStoresResultPtr> observer;
  keystore_service_.GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              UnorderedElementsAre(crosapi::mojom::KeystoreType::kUser,
                                   crosapi::mojom::KeystoreType::kDevice));
}

TEST_F(KeystoreServiceAshTest, GetKeyStoresFail) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({}), Status::kErrorInternal));

  CallbackObserver<mojom::GetKeyStoresResultPtr> observer;
  keystore_service_.GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(), mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, GetCertificatesSuccess) {
  EXPECT_CALL(platform_keys_service_,
              GetCertificates(TokenId::kUser, /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(GetCertificateList(), Status::kSuccess));

  CallbackObserver<mojom::GetCertificatesResultPtr> observer;
  keystore_service_.GetCertificates(mojom::KeystoreType::kUser,
                                    observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_certificates());
  AssertCertListEq(observer.result.value()->get_certificates(),
                   GetCertificateList());
}

TEST_F(KeystoreServiceAshTest, GetCertificatesFail) {
  EXPECT_CALL(platform_keys_service_,
              GetCertificates(TokenId::kUser, /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(std::make_unique<net::CertificateList>(),
                                   Status::kErrorInternal));

  CallbackObserver<mojom::GetCertificatesResultPtr> observer;
  keystore_service_.GetCertificates(mojom::KeystoreType::kUser,
                                    observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  AssertErrorEq(observer.result.value(), mojom::KeystoreError::kInternal);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, AddCertificateSuccess) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              ImportCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kSuccess));

  StatusCallbackObserver observer;
  keystore_service_.AddCertificate(mojom::KeystoreType::kDevice,
                                   CertToBlob(cert_list->front()),
                                   observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, false);
}

TEST_F(KeystoreServiceAshTest, AddCertificateFail) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              ImportCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kErrorCertificateInvalid));

  StatusCallbackObserver observer;
  keystore_service_.AddCertificate(mojom::KeystoreType::kDevice,
                                   CertToBlob(cert_list->front()),
                                   observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, true);
  EXPECT_EQ(observer.result_error, mojom::KeystoreError::kCertificateInvalid);
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, RemoveCertificateSuccess) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              RemoveCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kSuccess));

  StatusCallbackObserver observer;
  keystore_service_.RemoveCertificate(mojom::KeystoreType::kDevice,
                                      CertToBlob(cert_list->front()),
                                      observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, false);
}

TEST_F(KeystoreServiceAshTest, RemoveCertificateFail) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              RemoveCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kErrorCertificateInvalid));

  StatusCallbackObserver observer;
  keystore_service_.RemoveCertificate(mojom::KeystoreType::kDevice,
                                      CertToBlob(cert_list->front()),
                                      observer.GetCallback());

  ASSERT_TRUE(observer.has_value());
  EXPECT_EQ(observer.result_is_error, true);
  EXPECT_EQ(observer.result_error, mojom::KeystoreError::kCertificateInvalid);
}

//------------------------------------------------------------------------------

ash::attestation::MockTpmChallengeKey* InjectMockChallengeKey() {
  auto mock_challenge_key =
      std::make_unique<ash::attestation::MockTpmChallengeKey>();
  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      mock_challenge_key.get();
  ash::attestation::TpmChallengeKeyFactory::SetForTesting(
      std::move(mock_challenge_key));
  return challenge_key_ptr;
}

TEST_F(KeystoreServiceAshTest, ChallengeUserKeyNoMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::ChallengeAttestationOnlyKeystoreResultPtr> observer;
  keystore_service_.ChallengeAttestationOnlyKeystore(
      mojom::KeystoreType::kUser, /*challenge=*/GetDataBin(), /*migrate=*/false,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, ChallengeUserKeyMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/true,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::ChallengeAttestationOnlyKeystoreResultPtr> observer;
  keystore_service_.ChallengeAttestationOnlyKeystore(
      mojom::KeystoreType::kUser, /*challenge=*/GetDataBin(), /*migrate=*/true,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, ChallengeDeviceKeyNoMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_DEVICE,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::ChallengeAttestationOnlyKeystoreResultPtr> observer;
  keystore_service_.ChallengeAttestationOnlyKeystore(
      mojom::KeystoreType::kDevice, /*challenge=*/GetDataBin(),
      /*migrate=*/false, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, ChallengeDeviceKeyMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(
          chromeos::attestation::AttestationKeyType::KEY_DEVICE,
          /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
          /*register_key=*/true,
          /*key_name_for_spkac=*/StrStartsWith("attest-ent-machine-keystore-"),
          /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::ChallengeAttestationOnlyKeystoreResultPtr> observer;
  keystore_service_.ChallengeAttestationOnlyKeystore(
      mojom::KeystoreType::kDevice, /*challenge=*/GetDataBin(),
      /*migrate=*/true, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataBin());
}

TEST_F(KeystoreServiceAshTest, ChallengeKeyFail) {
  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  auto challenge_result = ash::attestation::TpmChallengeKeyResult::MakeError(
      ash::attestation::TpmChallengeKeyResultCode::kDbusError);

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(challenge_result));

  CallbackObserver<mojom::ChallengeAttestationOnlyKeystoreResultPtr> observer;
  keystore_service_.ChallengeAttestationOnlyKeystore(
      mojom::KeystoreType::kUser, /*challenge=*/GetDataBin(),
      /*migrate=*/false, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_error_message());
  EXPECT_EQ(observer.result.value()->get_error_message(),
            challenge_result.GetErrorMessage());
}

//------------------------------------------------------------------------------

// Tests for deprecated methods.

TEST_F(KeystoreServiceAshTest, DeprecatedGetPublicKeySuccess) {
  const std::vector<uint8_t> cert_bin =
      CertToBlob(GetCertificateList()->front());

  CallbackObserver<mojom::DEPRECATED_GetPublicKeyResultPtr> observer;
  keystore_service_.DEPRECATED_GetPublicKey(
      cert_bin, mojom::KeystoreSigningAlgorithmName::kRsassaPkcs115,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());

  ASSERT_TRUE(observer.result.value()->is_success_result());
  const mojom::GetPublicKeySuccessResultPtr& success_result =
      observer.result.value()->get_success_result();
  ASSERT_EQ(success_result->public_key, GetPublicKeyBin());

  ASSERT_TRUE(success_result->algorithm_properties->is_pkcs115());
  const mojom::KeystorePKCS115ParamsPtr& params =
      success_result->algorithm_properties->get_pkcs115();
  EXPECT_EQ(params->modulus_length, 2048);
  EXPECT_EQ(params->public_exponent, (std::vector<uint8_t>{1, 0, 1}));
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetPublicKeyFail) {
  const std::vector<uint8_t> cert_bin =
      CertToBlob(GetCertificateList()->front());

  CallbackObserver<mojom::DEPRECATED_GetPublicKeyResultPtr> observer;
  keystore_service_.DEPRECATED_GetPublicKey(
      cert_bin, mojom::KeystoreSigningAlgorithmName::kUnknown,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_error_message());
  EXPECT_EQ(observer.result.value()->get_error_message(),
            chromeos::platform_keys::KeystoreErrorToString(
                mojom::KeystoreError::kAlgorithmNotPermittedByCertificate));
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, DeprecatedGetKeyStoresEmptySuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({}), Status::kSuccess));

  CallbackObserver<mojom::DEPRECATED_GetKeyStoresResultPtr> observer;
  keystore_service_.DEPRECATED_GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_TRUE(observer.result.value()->get_key_stores().empty());
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetKeyStoresUserSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(
          RunOnceCallback<0>(MakeTokenIds({TokenId::kUser}), Status::kSuccess));

  CallbackObserver<mojom::DEPRECATED_GetKeyStoresResultPtr> observer;
  keystore_service_.DEPRECATED_GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              ElementsAre(crosapi::mojom::KeystoreType::kUser));
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetKeyStoresDeviceSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({TokenId::kSystem}),
                                   Status::kSuccess));

  CallbackObserver<mojom::DEPRECATED_GetKeyStoresResultPtr> observer;
  keystore_service_.DEPRECATED_GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              ElementsAre(crosapi::mojom::KeystoreType::kDevice));
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetKeyStoresDeviceUserSuccess) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(
          MakeTokenIds({TokenId::kUser, TokenId::kSystem}), Status::kSuccess));

  CallbackObserver<mojom::DEPRECATED_GetKeyStoresResultPtr> observer;
  keystore_service_.DEPRECATED_GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_key_stores());
  EXPECT_THAT(observer.result.value()->get_key_stores(),
              UnorderedElementsAre(crosapi::mojom::KeystoreType::kUser,
                                   crosapi::mojom::KeystoreType::kDevice));
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetKeyStoresFail) {
  EXPECT_CALL(platform_keys_service_, GetTokens)
      .WillOnce(RunOnceCallback<0>(MakeTokenIds({}), Status::kErrorInternal));

  CallbackObserver<mojom::DEPRECATED_GetKeyStoresResultPtr> observer;
  keystore_service_.DEPRECATED_GetKeyStores(observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_error_message());
  EXPECT_EQ(observer.result.value()->get_error_message(),
            chromeos::platform_keys::KeystoreErrorToString(
                mojom::KeystoreError::kInternal));
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, DeprecatedGetCertificatesSuccess) {
  EXPECT_CALL(platform_keys_service_,
              GetCertificates(TokenId::kUser, /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(GetCertificateList(), Status::kSuccess));

  CallbackObserver<mojom::DEPRECATED_GetCertificatesResultPtr> observer;
  keystore_service_.DEPRECATED_GetCertificates(mojom::KeystoreType::kUser,
                                               observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_certificates());
  AssertCertListEq(observer.result.value()->get_certificates(),
                   GetCertificateList());
}

TEST_F(KeystoreServiceAshTest, DeprecatedGetCertificatesFail) {
  EXPECT_CALL(platform_keys_service_,
              GetCertificates(TokenId::kUser, /*callback=*/_))
      .WillOnce(RunOnceCallback<1>(std::make_unique<net::CertificateList>(),
                                   Status::kErrorInternal));

  CallbackObserver<mojom::DEPRECATED_GetCertificatesResultPtr> observer;
  keystore_service_.DEPRECATED_GetCertificates(mojom::KeystoreType::kUser,
                                               observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_error_message());
  EXPECT_EQ(observer.result.value()->get_error_message(),
            chromeos::platform_keys::KeystoreErrorToString(
                mojom::KeystoreError::kInternal));
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, DeprecatedAddCertificateSuccess) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              ImportCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kSuccess));

  CallbackObserverRef<std::string> observer;
  keystore_service_.DEPRECATED_AddCertificate(mojom::KeystoreType::kDevice,
                                              CertToBlob(cert_list->front()),
                                              observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value());
  EXPECT_TRUE(observer.result.value().empty());
}

TEST_F(KeystoreServiceAshTest, DeprecatedAddCertificateFail) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              ImportCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kErrorCertificateInvalid));

  CallbackObserverRef<std::string> observer;
  keystore_service_.DEPRECATED_AddCertificate(mojom::KeystoreType::kDevice,
                                              CertToBlob(cert_list->front()),
                                              observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value());
  EXPECT_EQ(observer.result.value(),
            chromeos::platform_keys::KeystoreErrorToString(
                mojom::KeystoreError::kCertificateInvalid));
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, DeprecatedRemoveCertificateSuccess) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              RemoveCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kSuccess));

  CallbackObserverRef<std::string> observer;
  keystore_service_.DEPRECATED_RemoveCertificate(mojom::KeystoreType::kDevice,
                                                 CertToBlob(cert_list->front()),
                                                 observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value());
  EXPECT_TRUE(observer.result.value().empty());
}

TEST_F(KeystoreServiceAshTest, DeprecatedRemoveCertificateFail) {
  auto cert_list = GetCertificateList();

  EXPECT_CALL(platform_keys_service_,
              RemoveCertificate(TokenId::kSystem, CertEq(cert_list->front()),
                                /*callback=*/_))
      .WillOnce(RunOnceCallback<2>(Status::kErrorCertificateInvalid));

  CallbackObserverRef<std::string> observer;
  keystore_service_.DEPRECATED_RemoveCertificate(mojom::KeystoreType::kDevice,
                                                 CertToBlob(cert_list->front()),
                                                 observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value());
  EXPECT_EQ(observer.result.value(),
            chromeos::platform_keys::KeystoreErrorToString(
                mojom::KeystoreError::kCertificateInvalid));
}

//------------------------------------------------------------------------------

TEST_F(KeystoreServiceAshTest, DeprecatedChallengeUserKeyNoMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::DEPRECATED_KeystoreStringResultPtr> observer;
  keystore_service_.DEPRECATED_ChallengeAttestationOnlyKeystore(
      /*challenge=*/GetDataStr(), mojom::KeystoreType::kUser, /*migrate=*/false,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataStr());
}

TEST_F(KeystoreServiceAshTest, DeprecatedChallengeUserKeyMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/true,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::DEPRECATED_KeystoreStringResultPtr> observer;
  keystore_service_.DEPRECATED_ChallengeAttestationOnlyKeystore(
      /*challenge=*/GetDataStr(), mojom::KeystoreType::kUser, /*migrate=*/true,
      observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataStr());
}

TEST_F(KeystoreServiceAshTest, DeprecatedChallengeDeviceKeyNoMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_DEVICE,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::DEPRECATED_KeystoreStringResultPtr> observer;
  keystore_service_.DEPRECATED_ChallengeAttestationOnlyKeystore(
      /*challenge=*/GetDataStr(), mojom::KeystoreType::kDevice,
      /*migrate=*/false, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataStr());
}

TEST_F(KeystoreServiceAshTest, DeprecatedChallengeDeviceKeyMigrateSuccess) {
  // Incoming challenge and outgoing challenge response are imitated with the
  // same data blob. It is not realistic, but good enough for this test.

  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(
          chromeos::attestation::AttestationKeyType::KEY_DEVICE,
          /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
          /*register_key=*/true,
          /*key_name_for_spkac=*/StrStartsWith("attest-ent-machine-lacros-"),
          /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(
          ash::attestation::TpmChallengeKeyResult::MakeChallengeResponse(
              GetDataStr())));

  CallbackObserver<mojom::DEPRECATED_KeystoreStringResultPtr> observer;
  keystore_service_.DEPRECATED_ChallengeAttestationOnlyKeystore(
      /*challenge=*/GetDataStr(), mojom::KeystoreType::kDevice,
      /*migrate=*/true, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_challenge_response());
  EXPECT_EQ(observer.result.value()->get_challenge_response(), GetDataStr());
}

TEST_F(KeystoreServiceAshTest, DeprecatedChallengeKeyFail) {
  ash::attestation::MockTpmChallengeKey* challenge_key_ptr =
      InjectMockChallengeKey();

  auto challenge_result = ash::attestation::TpmChallengeKeyResult::MakeError(
      ash::attestation::TpmChallengeKeyResultCode::kDbusError);

  EXPECT_CALL(
      *challenge_key_ptr,
      BuildResponse(chromeos::attestation::AttestationKeyType::KEY_USER,
                    /*profile=*/_, /*callback=*/_, /*challenge=*/GetDataStr(),
                    /*register_key=*/false,
                    /*key_name_for_spkac=*/std::string(),
                    /*signals=*/_))
      .WillOnce(RunOnceCallback<2>(challenge_result));

  CallbackObserver<mojom::DEPRECATED_KeystoreStringResultPtr> observer;
  keystore_service_.DEPRECATED_ChallengeAttestationOnlyKeystore(
      /*challenge=*/GetDataStr(), mojom::KeystoreType::kUser,
      /*migrate=*/false, observer.GetCallback());

  ASSERT_TRUE(observer.result.has_value() && observer.result.value());
  ASSERT_TRUE(observer.result.value()->is_error_message());
  EXPECT_EQ(observer.result.value()->get_error_message(),
            challenge_result.GetErrorMessage());
}

}  // namespace
}  // namespace crosapi
