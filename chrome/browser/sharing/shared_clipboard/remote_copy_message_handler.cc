// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sharing/shared_clipboard/remote_copy_message_handler.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/guid.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sharing/proto/remote_copy_message.pb.h"
#include "chrome/browser/sharing/proto/sharing_message.pb.h"
#include "chrome/browser/sharing/sharing_metrics.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/image/image.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"
#include "ui/message_center/public/cpp/notifier_id.h"

namespace {

constexpr char kRemoteCopyAllowedOrigin[] = "https://googleusercontent.com";

constexpr size_t kMaxImageDownloadSize = 5 * 1024 * 1024;

// The initial delay for the timer that detects clipboard writes. An exponential
// backoff will double this value whenever the OneShotTimer reschedules.
constexpr base::TimeDelta kInitialDetectionTimerDelay = base::Milliseconds(1);

const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("remote_copy_message_handler",
                                        R"(
          semantics {
            sender: "RemoteCopyMessageHandler"
            description:
              "Fetches an image from a URL specified in an FCM message."
            trigger:
              "The user sent an image to this device from another device that "
              "they control."
            data:
              "An image URL, from a Google storage service like blobstore."
            destination: GOOGLE_OWNED_SERVICE
          }
          policy {
            cookies_allowed: NO
            setting:
              "Users can disable this behavior by signing out of Chrome."
            policy_exception_justification:
              "Can be controlled via Chrome sign-in."
          })");

std::u16string GetTextNotificationTitle(const std::string& device_name) {
  return device_name.empty()
             ? l10n_util::GetStringUTF16(
                   IDS_SHARING_REMOTE_COPY_NOTIFICATION_TITLE_TEXT_CONTENT_UNKNOWN_DEVICE)
             : l10n_util::GetStringFUTF16(
                   IDS_SHARING_REMOTE_COPY_NOTIFICATION_TITLE_TEXT_CONTENT,
                   base::UTF8ToUTF16(device_name));
}

std::u16string GetImageNotificationTitle(const std::string& device_name) {
  return device_name.empty()
             ? l10n_util::GetStringUTF16(
                   IDS_SHARING_REMOTE_COPY_NOTIFICATION_TITLE_IMAGE_CONTENT_UNKNOWN_DEVICE)
             : l10n_util::GetStringFUTF16(
                   IDS_SHARING_REMOTE_COPY_NOTIFICATION_TITLE_IMAGE_CONTENT,
                   base::UTF8ToUTF16(device_name));
}

}  // namespace

RemoteCopyMessageHandler::RemoteCopyMessageHandler(Profile* profile)
    : profile_(profile), allowed_origin_(kRemoteCopyAllowedOrigin) {}

RemoteCopyMessageHandler::~RemoteCopyMessageHandler() = default;

void RemoteCopyMessageHandler::OnMessage(
    chrome_browser_sharing::SharingMessage message,
    DoneCallback done_callback) {
  DCHECK(message.has_remote_copy_message());
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::OnMessage");

  // First cancel any pending async tasks that might otherwise overwrite the
  // results of the more recent message.
  CancelAsyncTasks();

  device_name_ = message.sender_device_name();

  switch (message.remote_copy_message().content_case()) {
    case chrome_browser_sharing::RemoteCopyMessage::kText:
      HandleText(message.remote_copy_message().text());
      break;
    case chrome_browser_sharing::RemoteCopyMessage::kImageUrl:
      HandleImage(message.remote_copy_message().image_url());
      break;
    case chrome_browser_sharing::RemoteCopyMessage::CONTENT_NOT_SET:
      NOTREACHED();
      break;
  }

  std::move(done_callback).Run(/*response=*/nullptr);
}

void RemoteCopyMessageHandler::HandleText(const std::string& text) {
  TRACE_EVENT1("sharing", "RemoteCopyMessageHandler::HandleText", "text_size",
               text.size());

  if (text.empty()) {
    Finish(RemoteCopyHandleMessageResult::kFailureEmptyText);
    return;
  }

  LogRemoteCopyReceivedTextSize(text.size());

  ui::ClipboardSequenceNumberToken old_sequence_number =
      ui::Clipboard::GetForCurrentThread()->GetSequenceNumber(
          ui::ClipboardBuffer::kCopyPaste);
  base::ElapsedTimer write_timer;
  {
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteText(base::UTF8ToUTF16(text));
  }
  LogRemoteCopyWriteTime(write_timer.Elapsed(), /*is_image=*/false);
  // Unretained(this) is safe here because |this| owns |write_detection_timer_|.
  write_detection_timer_.Start(
      FROM_HERE, kInitialDetectionTimerDelay,
      base::BindOnce(&RemoteCopyMessageHandler::DetectWrite,
                     base::Unretained(this), old_sequence_number,
                     base::TimeTicks::Now(), /*is_image=*/false));
  ShowNotification(GetTextNotificationTitle(device_name_), SkBitmap());
  Finish(RemoteCopyHandleMessageResult::kSuccessHandledText);
}

void RemoteCopyMessageHandler::HandleImage(const std::string& image_url) {
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::HandleImage");

  GURL url(image_url);

  if (!network::IsUrlPotentiallyTrustworthy(url)) {
    Finish(RemoteCopyHandleMessageResult::kFailureImageUrlNotTrustworthy);
    return;
  }

  if (!IsImageSourceAllowed(url)) {
    Finish(RemoteCopyHandleMessageResult::kFailureImageOriginNotAllowed);
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  // This request should be unauthenticated (no cookies), and shouldn't be
  // stored in the cache (this URL is only fetched once, ever.)
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  url_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  timer_ = base::ElapsedTimer();
  // Unretained(this) is safe here because |this| owns |url_loader_|.
  url_loader_->DownloadToString(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&RemoteCopyMessageHandler::OnURLLoadComplete,
                     base::Unretained(this)),
      kMaxImageDownloadSize);
}

bool RemoteCopyMessageHandler::IsImageSourceAllowed(const GURL& image_url) {
  // The actual image URL may have a hash in the subdomain. This means we
  // cannot match the entire host - we'll match the domain instead.
  return image_url.SchemeIs(allowed_origin_.scheme_piece()) &&
         image_url.DomainIs(allowed_origin_.host_piece()) &&
         image_url.EffectiveIntPort() == allowed_origin_.EffectiveIntPort();
}

void RemoteCopyMessageHandler::OnURLLoadComplete(
    std::unique_ptr<std::string> content) {
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::OnURLLoadComplete");

  int code;
  if (url_loader_->NetError() != net::OK) {
    code = url_loader_->NetError();
  } else if (!url_loader_->ResponseInfo() ||
             !url_loader_->ResponseInfo()->headers) {
    code = net::OK;
  } else {
    code = url_loader_->ResponseInfo()->headers->response_code();
  }
  LogRemoteCopyLoadImageStatusCode(code);

  url_loader_.reset();
  if (!content || content->empty()) {
    Finish(RemoteCopyHandleMessageResult::kFailureNoImageContentLoaded);
    return;
  }

  LogRemoteCopyLoadImageTime(timer_.Elapsed());
  LogRemoteCopyReceivedImageSizeBeforeDecode(content->size());

  timer_ = base::ElapsedTimer();
  ImageDecoder::Start(this, std::move(*content));
}

void RemoteCopyMessageHandler::OnImageDecoded(const SkBitmap& image) {
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::OnImageDecoded");

  if (image.drawsNothing()) {
    Finish(RemoteCopyHandleMessageResult::kFailureDecodedImageDrawsNothing);
    return;
  }

  LogRemoteCopyDecodeImageTime(timer_.Elapsed());
  LogRemoteCopyReceivedImageSizeAfterDecode(image.computeByteSize());

  WriteImageAndShowNotification(image);
}

void RemoteCopyMessageHandler::OnDecodeImageFailed() {
  Finish(RemoteCopyHandleMessageResult::kFailureDecodeImageFailed);
}

void RemoteCopyMessageHandler::WriteImageAndShowNotification(
    const SkBitmap& image) {
  TRACE_EVENT1("sharing",
               "RemoteCopyMessageHandler::WriteImageAndShowNotification",
               "bytes", image.computeByteSize());

  ui::ClipboardSequenceNumberToken old_sequence_number =
      ui::Clipboard::GetForCurrentThread()->GetSequenceNumber(
          ui::ClipboardBuffer::kCopyPaste);
  base::ElapsedTimer write_timer;
  {
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteImage(image);
  }
  LogRemoteCopyWriteTime(write_timer.Elapsed(), /*is_image=*/true);
  // Unretained(this) is safe here because |this| owns |write_detection_timer_|.
  write_detection_timer_.Start(
      FROM_HERE, kInitialDetectionTimerDelay,
      base::BindOnce(&RemoteCopyMessageHandler::DetectWrite,
                     base::Unretained(this), old_sequence_number,
                     base::TimeTicks::Now(), /*is_image=*/true));

  ShowNotification(GetImageNotificationTitle(device_name_), image);
  Finish(RemoteCopyHandleMessageResult::kSuccessHandledImage);
}

void RemoteCopyMessageHandler::ShowNotification(const std::u16string& title,
                                                const SkBitmap& image) {
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::ShowNotification");

  gfx::Image icon;
  message_center::RichNotificationData rich_notification_data;
  rich_notification_data.vector_small_image = &kSendTabToSelfIcon;
  rich_notification_data.renotify = true;

  ui::Accelerator paste_accelerator(ui::VKEY_V, ui::EF_PLATFORM_ACCELERATOR);

  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, base::GenerateGUID(), title,
      l10n_util::GetStringFUTF16(
          IDS_SHARING_REMOTE_COPY_NOTIFICATION_DESCRIPTION,
          paste_accelerator.GetShortcutText()),
      icon,
      /*display_source=*/std::u16string(),
      /*origin_url=*/GURL(), message_center::NotifierId(),
      rich_notification_data,
      /*delegate=*/nullptr);

  NotificationDisplayServiceFactory::GetForProfile(profile_)->Display(
      NotificationHandler::Type::SHARING, notification, /*metadata=*/nullptr);
}

void RemoteCopyMessageHandler::DetectWrite(
    const ui::ClipboardSequenceNumberToken& old_sequence_number,
    base::TimeTicks start_ticks,
    bool is_image) {
  TRACE_EVENT0("sharing", "RemoteCopyMessageHandler::DetectWrite");

  ui::ClipboardSequenceNumberToken current_sequence_number =
      ui::Clipboard::GetForCurrentThread()->GetSequenceNumber(
          ui::ClipboardBuffer::kCopyPaste);
  base::TimeDelta elapsed = base::TimeTicks::Now() - start_ticks;
  if (current_sequence_number != old_sequence_number) {
    LogRemoteCopyWriteDetectionTime(elapsed, is_image);
    return;
  }

  if (elapsed > base::Seconds(10))
    return;

  // Unretained(this) is safe here because |this| owns |write_detection_timer_|.
  base::TimeDelta backoff_delay = write_detection_timer_.GetCurrentDelay() * 2;
  write_detection_timer_.Start(
      FROM_HERE, backoff_delay,
      base::BindOnce(&RemoteCopyMessageHandler::DetectWrite,
                     base::Unretained(this), old_sequence_number, start_ticks,
                     is_image));
}

void RemoteCopyMessageHandler::Finish(RemoteCopyHandleMessageResult result) {
  TRACE_EVENT1("sharing", "RemoteCopyMessageHandler::Finish", "result", result);
  LogRemoteCopyHandleMessageResult(result);
  device_name_.clear();
}

void RemoteCopyMessageHandler::CancelAsyncTasks() {
  url_loader_.reset();
  ImageDecoder::Cancel(this);
  write_detection_timer_.AbandonAndStop();
}
