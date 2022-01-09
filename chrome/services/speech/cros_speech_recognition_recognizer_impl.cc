// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/speech/cros_speech_recognition_recognizer_impl.h"

#include "base/files/file_path.h"
#include "chrome/services/speech/soda/cros_soda_client.h"
#include "google_apis/google_api_keys.h"
#include "media/base/audio_buffer.h"
#include "media/base/audio_sample_types.h"
#include "media/base/audio_timestamp_helper.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/limits.h"
#include "media/base/media_switches.h"
#include "media/mojo/mojom/media_types.mojom.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace speech {

namespace {
constexpr char kNoClientError[] = "No cros soda client.";

chromeos::machine_learning::mojom::SodaRecognitionMode
GetSodaSpeechRecognitionMode(
    media::mojom::SpeechRecognitionMode recognition_mode) {
  switch (recognition_mode) {
    case media::mojom::SpeechRecognitionMode::kIme:
      return chromeos::machine_learning::mojom::SodaRecognitionMode::kIme;
    case media::mojom::SpeechRecognitionMode::kCaption:
      return chromeos::machine_learning::mojom::SodaRecognitionMode::kCaption;
    case media::mojom::SpeechRecognitionMode::kUnknown:
      // Chrome OS SODA doesn't support unknown recognition type. Default to
      // caption.
      NOTREACHED();
      return chromeos::machine_learning::mojom::SodaRecognitionMode::kCaption;
  }
}
}  // namespace

void CrosSpeechRecognitionRecognizerImpl::Create(
    mojo::PendingReceiver<media::mojom::SpeechRecognitionRecognizer> receiver,
    mojo::PendingRemote<media::mojom::SpeechRecognitionRecognizerClient> remote,
    base::WeakPtr<SpeechRecognitionServiceImpl> speech_recognition_service_impl,
    media::mojom::SpeechRecognitionOptionsPtr options,
    const base::FilePath& binary_path,
    const base::FilePath& config_path) {
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<CrosSpeechRecognitionRecognizerImpl>(
          std::move(remote), std::move(speech_recognition_service_impl),
          std::move(options), binary_path, config_path),
      std::move(receiver));
}
CrosSpeechRecognitionRecognizerImpl::~CrosSpeechRecognitionRecognizerImpl() =
    default;

CrosSpeechRecognitionRecognizerImpl::CrosSpeechRecognitionRecognizerImpl(
    mojo::PendingRemote<media::mojom::SpeechRecognitionRecognizerClient> remote,
    base::WeakPtr<SpeechRecognitionServiceImpl> speech_recognition_service_impl,
    media::mojom::SpeechRecognitionOptionsPtr options,
    const base::FilePath& binary_path,
    const base::FilePath& config_path)
    : SpeechRecognitionRecognizerImpl(
          std::move(remote),
          std::move(speech_recognition_service_impl),
          std::move(options),
          binary_path,
          config_path),
      binary_path_(binary_path),
      languagepack_path_(config_path) {
  recognition_event_callback_ = base::BindRepeating(
      &CrosSpeechRecognitionRecognizerImpl::OnRecognitionEvent,
      weak_factory_.GetWeakPtr());

  cros_soda_client_ = std::make_unique<soda::CrosSodaClient>();
}

void CrosSpeechRecognitionRecognizerImpl::
    SendAudioToSpeechRecognitionServiceInternal(
        media::mojom::AudioDataS16Ptr buffer) {
  // Soda is on, let's send the audio to it.
  int channel_count = buffer->channel_count;
  int sample_rate = buffer->sample_rate;
  size_t buffer_size = 0;
  // Verify and calculate the buffer size.
  if (!base::CheckMul(buffer->data.size(), sizeof(buffer->data[0]))
           .AssignIfValid(&buffer_size)) {
    LOG(DFATAL) << "Size check invalid.";
    return;
  }
  if (cros_soda_client_ == nullptr) {
    LOG(DFATAL) << "No soda client, stopping.";
    mojo::ReportBadMessage(kNoClientError);
    return;
  }

  if (!cros_soda_client_->IsInitialized() ||
      cros_soda_client_->DidAudioPropertyChange(sample_rate, channel_count)) {
    auto config = chromeos::machine_learning::mojom::SodaConfig::New();
    config->channel_count = channel_count;
    config->sample_rate = sample_rate;
    config->api_key = google_apis::GetSodaAPIKey();
    config->language_dlc_path = languagepack_path_.value();
    config->library_dlc_path = binary_path_.value();
    config->recognition_mode =
        GetSodaSpeechRecognitionMode(options_->recognition_mode);
    config->enable_formatting =
        options_->enable_formatting
            ? chromeos::machine_learning::mojom::OptionalBool::kTrue
            : chromeos::machine_learning::mojom::OptionalBool::kFalse;
    cros_soda_client_->Reset(std::move(config), recognition_event_callback_);
  }
  cros_soda_client_->AddAudio(reinterpret_cast<char*>(buffer->data.data()),
                              buffer_size);
}
}  // namespace speech
