// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "content/browser/media/media_browsertest.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "media/base/media_switches.h"
#include "media/media_buildflags.h"
#include "ui/display/display_switches.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#endif

namespace content {

class MediaCanPlayTypeTest : public MediaBrowserTest {
 public:
  MediaCanPlayTypeTest() = default;

  MediaCanPlayTypeTest(const MediaCanPlayTypeTest&) = delete;
  MediaCanPlayTypeTest& operator=(const MediaCanPlayTypeTest&) = delete;

  void SetUpOnMainThread() override {
    EXPECT_TRUE(
        NavigateToURL(shell(), GetTestUrl("media", "canplaytype_test.html")));
  }

  void ExecuteTest(const std::string& command) {
    EXPECT_EQ(true, EvalJs(shell(), command));
  }
};

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_av1) {
#if BUILDFLAG(ENABLE_AV1_DECODER)
  ExecuteTest("testAv1Variants(true)");
#else
  ExecuteTest("testAv1Variants(false)");
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_wav) {
  ExecuteTest("testWavVariants()");
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_webm) {
  ExecuteTest("testWebmVariants()");
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_ogg) {
#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
  ExecuteTest("testOggVariants(true)");  // has_theora_support=true
#else
  ExecuteTest("testOggVariants(false)");  // has_theora_support=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_flac) {
  ExecuteTest("testFlacVariants()");
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_mp3) {
  ExecuteTest("testMp3Variants()");
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_mp4) {
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
  ExecuteTest("testMp4Variants(true)");  // has_proprietary_codecs=true
#else
  ExecuteTest("testMp4Variants(false)");  // has_proprietary_codecs=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_AvcVariants) {
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
// High 10-bit profile is only available when we can use ffmpeg to decode H.264.
// Even though FFmpeg is used on Android, we only use platform decoders for
// H.264
#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
  ExecuteTest("testAvcVariants(true, true)");  // has_proprietary_codecs=true,
                                               // has_software_avc=true
#else
  ExecuteTest("testAvcVariants(true, false)");  // has_proprietary_codecs=true,
                                                // has_software_avc=false
#endif
#else
  ExecuteTest(
      "testAvcVariants(false, false)");  // has_proprietary_codecs=false,
                                         // has_software_avc=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_AvcLevels) {
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
  ExecuteTest("testAvcLevels(true)");  // has_proprietary_codecs=true
#else
  ExecuteTest("testAvcLevels(false)");   // has_proprietary_codecs=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_Mp4aVariants) {
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
#if defined(OS_ANDROID)
  // xHE-AAC support is currently only available on P+.
  if (base::android::BuildInfo::GetInstance()->sdk_int() >=
      base::android::SDK_VERSION_P) {
    ExecuteTest(
        "testMp4aVariants(true, true)");  // has_proprietary_codecs=true,
                                          // has_xhe_aac_support=true
    return;
  }
#endif
  ExecuteTest("testMp4aVariants(true, false)");  // has_proprietary_codecs=true,
                                                 // has_xhe_aac_support=false
#else
  ExecuteTest(
      "testMp4aVariants(false, false)");    // has_proprietary_codecs=false,
                                            // has_xhe_aac_support=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_HLS) {
#if defined(OS_ANDROID)
  ExecuteTest("testHls(true)");  // has_hls_support=true
#else
  ExecuteTest("testHls(false)");            // has_hls_support=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_AAC_ADTS) {
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
  ExecuteTest("testAacAdts(true)");  // has_proprietary_codecs=true
#else
  ExecuteTest("testAacAdts(false)");        // has_proprietary_codecs=false
#endif
}

IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_Mpeg2Ts) {
// TODO(crbug.com/1091962): This is actually wrong. We don't support mpeg2ts in
// src=, only MSE playbacks, so this should actually indicate no support when
// querying canPlayType().
#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER) && \
    BUILDFLAG(USE_PROPRIETARY_CODECS)
  ExecuteTest("testMp2tsVariants(true)");  // has_mp2ts_support=true
#else
  ExecuteTest("testMp2tsVariants(false)");  // has_mp2ts_support=false
#endif
}

// See more complete codec string testing in media/base/video_codecs_unittest.cc
IN_PROC_BROWSER_TEST_F(MediaCanPlayTypeTest, CodecSupportTest_NewVp9Variants) {
// Profile 2 and 3 support is currently disabled on Android prior to P and MIPS.
#if (defined(ARCH_CPU_ARM_FAMILY) && !defined(OS_WIN) && !defined(OS_MAC)) || \
    defined(ARCH_CPU_MIPS_FAMILY)
#if defined(OS_ANDROID)
  if (base::android::BuildInfo::GetInstance()->sdk_int() >=
      base::android::SDK_VERSION_P) {
    ExecuteTest("testNewVp9Variants(true)");  // has_profile_2_3_support=true
    return;
  }
#endif
  ExecuteTest("testNewVp9Variants(false)");  // has_profile_2_3_support=false
#else
  ExecuteTest("testNewVp9Variants(true)");  // has_profile_2_3_support=true
#endif
}

}  // namespace content
