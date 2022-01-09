// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FEED_ANDROID_FEED_STREAM_H_
#define CHROME_BROWSER_FEED_ANDROID_FEED_STREAM_H_

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/feed/android/feed_reliability_logging_bridge.h"
#include "components/feed/core/v2/public/feed_api.h"
#include "components/feed/core/v2/public/feed_stream_surface.h"

namespace feedui {
class StreamUpdate;
}

namespace feed {
namespace android {

// Native access to |FeedStream| in Java.
// Created once for each NTP/start surface.
class FeedStream : public ::feed::FeedStreamSurface {
 public:
  explicit FeedStream(const base::android::JavaRef<jobject>& j_this,
                      jboolean is_for_you_stream,
                      FeedReliabilityLoggingBridge* reliability_logging_bridge);
  FeedStream(const FeedStream&) = delete;
  FeedStream& operator=(const FeedStream&) = delete;

  ~FeedStream() override;

  // FeedStream implementation.
  void StreamUpdate(const feedui::StreamUpdate& update) override;
  void ReplaceDataStoreEntry(base::StringPiece key,
                             base::StringPiece data) override;
  void RemoveDataStoreEntry(base::StringPiece key) override;

  ReliabilityLoggingBridge& GetReliabilityLoggingBridge() override;

  void OnStreamUpdated(const feedui::StreamUpdate& stream_update);

  void LoadMore(JNIEnv* env,
                const base::android::JavaParamRef<jobject>& obj,
                const base::android::JavaParamRef<jobject>& callback_obj);

  void ManualRefresh(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj,
                     const base::android::JavaParamRef<jobject>& callback_obj);

  void ProcessThereAndBackAgain(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jbyteArray>& data);

  int ExecuteEphemeralChange(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jbyteArray>& data);

  void CommitEphemeralChange(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj,
                             int change_id);

  void DiscardEphemeralChange(JNIEnv* env,
                              const base::android::JavaParamRef<jobject>& obj,
                              int change_id);

  void SurfaceOpened(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj);

  void SurfaceClosed(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj);

  // Is activity logging enabled (ephemeral).
  bool IsActivityLoggingEnabled(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);

  // Event reporting functions. See |FeedApi| for definitions.
  void ReportSliceViewed(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& obj,
                         const base::android::JavaParamRef<jstring>& slice_id);
  void ReportFeedViewed(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj);
  void ReportOpenAction(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj,
                        const base::android::JavaParamRef<jobject>& j_url,
                        const base::android::JavaParamRef<jstring>& slice_id);
  void ReportOpenInNewTabAction(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& j_url,
      const base::android::JavaParamRef<jstring>& slice_id);
  void ReportOpenInNewIncognitoTabAction(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  void ReportSendFeedbackAction(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  void ReportPageLoaded(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj,
                        jboolean in_new_tab);
  void ReportStreamScrolled(JNIEnv* env,
                            const base::android::JavaParamRef<jobject>& obj,
                            int distance_dp);
  void ReportStreamScrollStart(JNIEnv* env,
                               const base::android::JavaParamRef<jobject>& obj);
  void ReportOtherUserAction(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj,
                             int action_type);
  int GetSurfaceId(JNIEnv* env,
                   const base::android::JavaParamRef<jobject>& obj);

  jlong GetLastFetchTimeMs(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj);

  void ReportNoticeCreated(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           const base::android::JavaParamRef<jstring>& key);
  void ReportNoticeViewed(JNIEnv* env,
                          const base::android::JavaParamRef<jobject>& obj,
                          const base::android::JavaParamRef<jstring>& key);
  void ReportNoticeOpenAction(JNIEnv* env,
                              const base::android::JavaParamRef<jobject>& obj,
                              const base::android::JavaParamRef<jstring>& key);

  void ReportNoticeDismissed(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj,
                             const base::android::JavaParamRef<jstring>& key);

 private:
  base::android::ScopedJavaGlobalRef<jobject> java_ref_;
  raw_ptr<FeedApi> feed_stream_api_;
  bool attached_ = false;
  raw_ptr<FeedReliabilityLoggingBridge> reliability_logging_bridge_ = nullptr;
};

}  // namespace android
}  // namespace feed

#endif  // CHROME_BROWSER_FEED_ANDROID_FEED_STREAM_H_
