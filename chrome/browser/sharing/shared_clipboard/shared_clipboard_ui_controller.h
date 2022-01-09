// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SHARING_SHARED_CLIPBOARD_SHARED_CLIPBOARD_UI_CONTROLLER_H_
#define CHROME_BROWSER_SHARING_SHARED_CLIPBOARD_SHARED_CLIPBOARD_UI_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/sharing/sharing_ui_controller.h"
#include "chrome/browser/ui/page_action/page_action_icon_type.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
}  // namespace content

class SharedClipboardUiController
    : public SharingUiController,
      public content::WebContentsUserData<SharedClipboardUiController> {
 public:
  static SharedClipboardUiController* GetOrCreateFromWebContents(
      content::WebContents* web_contents);

  SharedClipboardUiController(const SharedClipboardUiController&) = delete;
  SharedClipboardUiController& operator=(const SharedClipboardUiController&) =
      delete;

  ~SharedClipboardUiController() override;

  void OnDeviceSelected(const std::u16string& text,
                        const syncer::DeviceInfo& device);

  // Overridden from SharingUiController:
  std::u16string GetTitle(SharingDialogType dialog_type) override;
  PageActionIconType GetIconType() override;
  sync_pb::SharingSpecificFields::EnabledFeatures GetRequiredFeature()
      const override;
  void OnDeviceChosen(const syncer::DeviceInfo& device) override;
  void OnAppChosen(const SharingApp& app) override;
  std::u16string GetContentType() const override;
  std::u16string GetErrorDialogText() const override;
  const gfx::VectorIcon& GetVectorIcon() const override;
  std::u16string GetTextForTooltipAndAccessibleName() const override;
  SharingFeatureName GetFeatureMetricsPrefix() const override;

 protected:
  explicit SharedClipboardUiController(content::WebContents* web_contents);

  // Overridden from SharingUiController:
  void DoUpdateApps(UpdateAppsCallback callback) override;

 private:
  friend class content::WebContentsUserData<SharedClipboardUiController>;

  std::u16string text_;

  base::WeakPtrFactory<SharedClipboardUiController> weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_SHARING_SHARED_CLIPBOARD_SHARED_CLIPBOARD_UI_CONTROLLER_H_
