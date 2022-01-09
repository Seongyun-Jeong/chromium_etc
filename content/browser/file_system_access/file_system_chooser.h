// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_CHOOSER_H_
#define CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_CHOOSER_H_

#include "base/callback_helpers.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/task/task_runner.h"
#include "content/common/content_export.h"
#include "content/public/browser/file_system_access_entry_factory.h"
#include "storage/browser/file_system/isolated_context.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace content {

class WebContents;

// This is a ui::SelectFileDialog::Listener implementation that grants access to
// the selected files to a specific renderer process on success, and then calls
// a callback on a specific task runner. Furthermore the listener will delete
// itself when any of its listener methods are called.
// All of this class has to be called on the UI thread.
class CONTENT_EXPORT FileSystemChooser : public ui::SelectFileDialog::Listener {
 public:
  using PathType = FileSystemAccessEntryFactory::PathType;
  struct ResultEntry {
    PathType type;
    base::FilePath path;
  };

  using ResultCallback =
      base::OnceCallback<void(blink::mojom::FileSystemAccessErrorPtr,
                              std::vector<ResultEntry>)>;

  class CONTENT_EXPORT Options {
   public:
    Options(ui::SelectFileDialog::Type type,
            blink::mojom::AcceptsTypesInfoPtr accepts_types_info,
            base::FilePath default_directory,
            base::FilePath suggested_name);
    Options(const Options&) = default;
    Options& operator=(const Options&) = default;

    ui::SelectFileDialog::Type type() const { return type_; }
    const ui::SelectFileDialog::FileTypeInfo& file_type_info() const {
      return file_types_;
    }
    const base::FilePath& default_path() const { return default_path_; }
    int default_file_type_index() const { return default_file_type_index_; }

   private:
    base::FilePath ResolveSuggestedNameExtension(
        base::FilePath suggested_name,
        ui::SelectFileDialog::FileTypeInfo& file_types);

    ui::SelectFileDialog::Type type_;
    ui::SelectFileDialog::FileTypeInfo file_types_;
    int default_file_type_index_ = 0;
    base::FilePath default_path_;
  };

  static void CreateAndShow(WebContents* web_contents,
                            const Options& options,
                            ResultCallback callback,
                            base::ScopedClosureRunner fullscreen_block);

  FileSystemChooser(ui::SelectFileDialog::Type type,
                    ResultCallback callback,
                    base::ScopedClosureRunner fullscreen_block);

 private:
  ~FileSystemChooser() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const base::FilePath& path,
                    int index,
                    void* params) override;
  void MultiFilesSelected(const std::vector<base::FilePath>& files,
                          void* params) override;
  void FileSelectedWithExtraInfo(const ui::SelectedFileInfo& file,
                                 int index,
                                 void* params) override;
  void MultiFilesSelectedWithExtraInfo(
      const std::vector<ui::SelectedFileInfo>& files,
      void* params) override;
  void FileSelectionCanceled(void* params) override;

  ResultCallback callback_;
  ui::SelectFileDialog::Type type_;
  base::ScopedClosureRunner fullscreen_block_;

  scoped_refptr<ui::SelectFileDialog> dialog_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_CHOOSER_H_
