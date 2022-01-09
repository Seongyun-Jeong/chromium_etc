// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_PAGE_CAPTURE_PAGE_CAPTURE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_PAGE_CAPTURE_PAGE_CAPTURE_API_H_

#include <stdint.h>

#include <string>

#include "base/memory/ref_counted.h"
#include "build/chromeos_buildflags.h"
#include "chrome/common/extensions/api/page_capture.h"
#include "extensions/browser/extension_function.h"
#include "storage/browser/blob/shareable_file_reference.h"

namespace base {
class FilePath;
}

namespace content {
class WebContents;
}

namespace extensions {

#if BUILDFLAG(IS_CHROMEOS_ASH)
class PermissionIDSet;
#endif

class PageCaptureSaveAsMHTMLFunction : public ExtensionFunction {
 public:
  PageCaptureSaveAsMHTMLFunction();

  // Test specific delegate used to test that the temporary file gets deleted.
  class TestDelegate {
   public:
    // Called on the IO thread when the temporary file that contains the
    // generated data has been created.
    virtual void OnTemporaryFileCreated(
        scoped_refptr<storage::ShareableFileReference> temp_file) = 0;
  };
  static void SetTestDelegate(TestDelegate* delegate);

  // ExtensionFunction:
  void OnServiceWorkerAck() override;

 private:
  ~PageCaptureSaveAsMHTMLFunction() override;
  ResponseAction Run() override;
  bool OnMessageReceived(const IPC::Message& message) override;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Resolves the API permission request in Public Sessions.
  void ResolvePermissionRequest(const PermissionIDSet& allowed_permissions);
#endif

  // Returns whether or not the extension has permission to capture the current
  // page. Sets |*error| to an error value on failure.
  bool CanCaptureCurrentPage(std::string* error);

  // Called on the file thread.
  void CreateTemporaryFile();

  void TemporaryFileCreatedOnIO(bool success);
  void TemporaryFileCreatedOnUI(bool success);

  // Called on the UI thread.
  void ReturnFailure(const std::string& error);
  void ReturnSuccess(int64_t file_size);

  // Callback called once the MHTML generation is done.
  void MHTMLGenerated(int64_t mhtml_file_size);

  // Returns the WebContents we are associated with, NULL if it's been closed.
  content::WebContents* GetWebContents();

  std::unique_ptr<extensions::api::page_capture::SaveAsMHTML::Params> params_;

  // The path to the temporary file containing the MHTML data.
  base::FilePath mhtml_path_;

  // The file containing the MHTML.
  scoped_refptr<storage::ShareableFileReference> mhtml_file_;

  DECLARE_EXTENSION_FUNCTION("pageCapture.saveAsMHTML", PAGECAPTURE_SAVEASMHTML)
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_PAGE_CAPTURE_PAGE_CAPTURE_API_H_
