// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TRANSLATE_CHROME_TRANSLATE_CLIENT_H_
#define CHROME_BROWSER_TRANSLATE_CHROME_TRANSLATE_CLIENT_H_

#include <memory>
#include <string>

#include "base/feature_list.h"
#include "base/gtest_prod_util.h"
#include "build/build_config.h"
#include "chrome/browser/ui/translate/translate_bubble_model.h"
#include "components/autofill_assistant/browser/public/runtime_observer.h"
#include "components/language/core/browser/url_language_histogram.h"
#include "components/translate/content/browser/content_translate_driver.h"
#include "components/translate/content/browser/per_frame_content_translate_driver.h"
#include "components/translate/core/browser/translate_client.h"
#include "components/translate/core/browser/translate_step.h"
#include "components/translate/core/common/translate_errors.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

class PrefService;

namespace translate {
class LanguageState;
class TranslateAcceptLanguages;
class TranslatePrefs;
class TranslateManager;

struct LanguageDetectionDetails;
}  // namespace translate

enum class ShowTranslateBubbleResult;

class ChromeTranslateClient
    : public translate::TranslateClient,
      public translate::TranslateDriver::LanguageDetectionObserver,
      public content::WebContentsObserver,
      public content::WebContentsUserData<ChromeTranslateClient>,
      public autofill_assistant::RuntimeObserver {
 public:
  ChromeTranslateClient(const ChromeTranslateClient&) = delete;
  ChromeTranslateClient& operator=(const ChromeTranslateClient&) = delete;

  ~ChromeTranslateClient() override;

  // Gets the LanguageState associated with the page.
  const translate::LanguageState& GetLanguageState();

  // Returns the ContentTranslateDriver instance associated with this
  // WebContents.
  translate::ContentTranslateDriver* translate_driver();

  // Returns the PerFrameContentTranslateDriver instance, if any, associated
  // with this WebContents.
  translate::PerFrameContentTranslateDriver* per_frame_translate_driver();

  // Helper method to return a new TranslatePrefs instance.
  static std::unique_ptr<translate::TranslatePrefs> CreateTranslatePrefs(
      PrefService* prefs);

  // Helper method to return the TranslateAcceptLanguages instance associated
  // with |browser_context|.
  static translate::TranslateAcceptLanguages* GetTranslateAcceptLanguages(
      content::BrowserContext* browser_context);

  // Helper method to return the TranslateManager instance associated with
  // |web_contents|, or NULL if there is no such associated instance.
  static translate::TranslateManager* GetManagerFromWebContents(
      content::WebContents* web_contents);

  // Gets |source| and |target| language for translation.
  void GetTranslateLanguages(content::WebContents* web_contents,
                             std::string* source,
                             std::string* target);

  // Gets the associated TranslateManager.
  translate::TranslateManager* GetTranslateManager();

  // TranslateClient implementation.
  translate::TranslateDriver* GetTranslateDriver() override;
  PrefService* GetPrefs() override;
  std::unique_ptr<translate::TranslatePrefs> GetTranslatePrefs() override;
  translate::TranslateAcceptLanguages* GetTranslateAcceptLanguages() override;
#if defined(OS_ANDROID)
  std::unique_ptr<infobars::InfoBar> CreateInfoBar(
      std::unique_ptr<translate::TranslateInfoBarDelegate> delegate)
      const override;
  int GetInfobarIconID() const override;

  // Trigger a manual translation when the necessary state (e.g. source
  // language) is ready.
  void ManualTranslateWhenReady();
#endif
  void SetPredefinedTargetLanguage(const std::string& translate_language_code);

  bool ShowTranslateUI(translate::TranslateStep step,
                       const std::string& source_language,
                       const std::string& target_language,
                       translate::TranslateErrors::Type error_type,
                       bool triggered_from_menu) override;
  bool IsTranslatableURL(const GURL& url) override;
  bool IsAutofillAssistantRunning() const override;

  // TranslateDriver::LanguageDetectionObserver implementation.
  void OnLanguageDetermined(
      const translate::LanguageDetectionDetails& details) override;

  // autofill_assistant::RuntimeObserver implementation.
  void OnStateChanged(autofill_assistant::UIState state) override;

 private:
  explicit ChromeTranslateClient(content::WebContents* web_contents);
  friend class content::WebContentsUserData<ChromeTranslateClient>;
  FRIEND_TEST_ALL_PREFIXES(ChromeTranslateClientTest,
                           LanguageEventShouldRecord);
  FRIEND_TEST_ALL_PREFIXES(ChromeTranslateClientTest,
                           LanguageEventShouldNotRecord);
  FRIEND_TEST_ALL_PREFIXES(ChromeTranslateClientTest,
                           TranslationEventShouldRecord);
  FRIEND_TEST_ALL_PREFIXES(ChromeTranslateClientTest,
                           TranslationEventShouldNotRecord);

  // content::WebContentsObserver implementation.
  void WebContentsDestroyed() override;

#if !defined(OS_ANDROID)
  // Shows the translate bubble.
  ShowTranslateBubbleResult ShowBubble(
      translate::TranslateStep step,
      const std::string& source_language,
      const std::string& target_language,
      translate::TranslateErrors::Type error_type,
      bool is_user_gesture);
#endif

  std::unique_ptr<translate::ContentTranslateDriver> translate_driver_;
  std::unique_ptr<translate::PerFrameContentTranslateDriver>
      per_frame_translate_driver_;
  std::unique_ptr<translate::TranslateManager> translate_manager_;

#if defined(OS_ANDROID)
  // Whether to trigger a manual translation when ready.
  // See ChromeTranslateClient::ManualTranslateOnReady
  bool manual_translate_on_ready_ = false;
#endif

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_TRANSLATE_CHROME_TRANSLATE_CLIENT_H_
