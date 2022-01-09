// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/autocomplete/tab_matcher_android.h"

#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "content/public/browser/web_contents_user_data.h"

#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/android/tab_android_user_data.h"
#include "chrome/browser/flags/android/chrome_session_state.h"
#include "chrome/browser/ui/android/omnibox/jni_headers/ChromeAutocompleteProviderClient_jni.h"
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_jni_bridge.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"

namespace {
class AutocompleteClientTabAndroidUserData
    : public TabAndroidUserData<AutocompleteClientTabAndroidUserData>,
      public TabAndroid::Observer {
 public:
  ~AutocompleteClientTabAndroidUserData() override {
    tab_->RemoveObserver(this);
  }

  const GURL& GetStrippedURL() const { return stripped_url_; }

  bool IsInitialized() const { return initialized_; }

  void UpdateStrippedURL(const GURL& url,
                         const TemplateURLService* template_url_service) {
    initialized_ = true;
    if (url.is_valid()) {
      stripped_url_ = AutocompleteMatch::GURLToStrippedGURL(
          url, AutocompleteInput(), template_url_service, std::u16string());
    }
  }

  // TabAndroid::Observer implementation
  void OnInitWebContents(TabAndroid* tab) override {
    tab->RemoveUserData(UserDataKey());
  }

 private:
  explicit AutocompleteClientTabAndroidUserData(TabAndroid* tab) : tab_(tab) {
    DCHECK(tab);
    tab->AddObserver(this);
  }
  friend class TabAndroidUserData<AutocompleteClientTabAndroidUserData>;

  raw_ptr<TabAndroid> tab_;
  bool initialized_ = false;
  GURL stripped_url_;

  TAB_ANDROID_USER_DATA_KEY_DECL();
};
TAB_ANDROID_USER_DATA_KEY_IMPL(AutocompleteClientTabAndroidUserData)
}  // namespace

bool TabMatcherAndroid::IsTabOpenWithURL(const GURL& url,
                                         const AutocompleteInput* input) const {
  DCHECK(input);
  const AutocompleteInput empty_input;
  if (!input)
    input = &empty_input;
  const GURL stripped_url = AutocompleteMatch::GURLToStrippedGURL(
      url, *input, client_.GetTemplateURLService(), std::u16string());
  const auto all_tabs = GetAllHiddenAndNonCCTTabInfos();
  return all_tabs.find(stripped_url) != all_tabs.end();
}

void TabMatcherAndroid::FindMatchingTabs(GURLToTabInfoMap* map,
                                         const AutocompleteInput* input) const {
  DCHECK(map);
  DCHECK(input);
  const AutocompleteInput empty_input;
  if (!input)
    input = &empty_input;
  auto all_tabs = GetAllHiddenAndNonCCTTabInfos();

  for (auto& gurl_to_tab_info : *map) {
    const GURL stripped_url = AutocompleteMatch::GURLToStrippedGURL(
        gurl_to_tab_info.first, *input, client_.GetTemplateURLService(),
        std::u16string());
    auto found_tab = all_tabs.find(stripped_url);
    if (found_tab != all_tabs.end()) {
      gurl_to_tab_info.second = found_tab->second;
    }
  }
}

TabMatcher::GURLToTabInfoMap TabMatcherAndroid::GetAllHiddenAndNonCCTTabInfos()
    const {
  using chrome::android::ActivityType;
  GURLToTabInfoMap tab_infos;

  // Collect tab models that host tabs eligible for SwitchToTab.
  // Ignore:
  // - tab models for not matching profile (eg. incognito vs non-incognito)
  // - custom and trusted tabs.
  std::vector<TabModel*> tab_models;
  for (TabModel* model : TabModelList::models()) {
    if (profile_ != model->GetProfile())
      continue;

    auto type = model->activity_type();
    if (type == ActivityType::kCustomTab ||
        type == ActivityType::kTrustedWebActivity) {
      continue;
    }

    tab_models.push_back(model);
  }

  // Short circuit in the event we have no tab models hosting eligible tabs.
  if (tab_models.size() == 0)
    return tab_infos;

  // Create and populate an array of Java TabModels.
  // The most expensive series of calls that reach to Java for every single tab
  // at least once start here and span until the end of this method.
  JNIEnv* env = base::android::AttachCurrentThread();
  jclass tab_model_clazz = TabModelJniBridge::GetClazz(env);
  base::android::ScopedJavaLocalRef<jobjectArray> j_tab_model_array(
      env, env->NewObjectArray(tab_models.size(), tab_model_clazz, nullptr));
  // Get all the hidden and non CCT tabs. Filter the tabs in CCT tabmodel first.
  for (size_t i = 0; i < tab_models.size(); ++i) {
    env->SetObjectArrayElement(j_tab_model_array.obj(), i,
                               tab_models[i]->GetJavaObject().obj());
  }

  // Retrieve all Tabs associated with previously built TabModels array.
  base::android::ScopedJavaLocalRef<jobjectArray> j_tabs =
      Java_ChromeAutocompleteProviderClient_getAllHiddenTabs(env,
                                                             j_tab_model_array);
  if (j_tabs.is_null())
    return tab_infos;

  // Create a map of tab URLs to their corresponding tab infos.
  auto all_tabs = TabAndroid::GetAllNativeTabs(env, j_tabs);
  for (TabAndroid* tab : all_tabs) {
    // Browser did not load the tab yet after Chrome started. To avoid
    // reloading WebContents, we just compare URLs.
    AutocompleteClientTabAndroidUserData::CreateForTabAndroid(tab);
    AutocompleteClientTabAndroidUserData* user_data =
        AutocompleteClientTabAndroidUserData::FromTabAndroid(tab);
    DCHECK(user_data);
    if (!user_data->IsInitialized()) {
      user_data->UpdateStrippedURL(tab->GetURL(),
                                   client_.GetTemplateURLService());
    }

    const GURL& tab_stripped_url = user_data->GetStrippedURL();
    TabInfo info;
    info.has_matching_tab = true;
    info.android_tab = JavaObjectWeakGlobalRef(env, tab->GetJavaObject());
    tab_infos[tab_stripped_url] = info;
  }

  return tab_infos;
}
