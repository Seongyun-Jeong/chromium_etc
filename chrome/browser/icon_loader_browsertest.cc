// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "chrome/browser/icon_loader.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/image/image.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "ui/display/win/dpi.h"
#endif

using IconLoaderBrowserTest = InProcessBrowserTest;

class TestIconLoader {
 public:
  explicit TestIconLoader(base::OnceClosure quit_closure)
      : quit_closure_(std::move(quit_closure)) {}

  TestIconLoader(const TestIconLoader&) = delete;
  TestIconLoader& operator=(const TestIconLoader&) = delete;

  ~TestIconLoader() {
    if (!quit_closure_.is_null()) {
      std::move(quit_closure_).Run();
    }
  }

  bool load_succeeded() const { return load_succeeded_; }

  bool TryLoadIcon(const base::FilePath& file_path,
                   IconLoader::IconSize size,
                   float scale) {
    // |loader| is self deleting. |this| will live as long as the
    // test.
    auto* loader = IconLoader::Create(
        file_path, size, scale,
        base::BindOnce(&TestIconLoader::OnIconLoaded, base::Unretained(this)));
    loader->Start();
    return true;
  }

 private:
  void OnIconLoaded(gfx::Image img, const IconLoader::IconGroup& group) {
    if (!img.IsEmpty())
      load_succeeded_ = true;
    Quit();
  }

  void Quit() {
    EXPECT_FALSE(quit_closure_.is_null());
    std::move(quit_closure_).Run();
  }

  bool load_succeeded_ = false;
  base::OnceClosure quit_closure_;
};

#if !((defined(OS_LINUX) || defined(OS_CHROMEOS)) && defined(MEMORY_SANITIZER))
const base::FilePath::CharType kGroupOnlyFilename[] =
    FILE_PATH_LITERAL("unlikely-to-exist-file.txt");

// Under GTK, the icon providing functions do not return icons.
IN_PROC_BROWSER_TEST_F(IconLoaderBrowserTest, LoadGroup) {
  float scale = 1.0;
#if defined(OS_WIN)
  scale = display::win::GetDPIScale();

  // This test times out on Win7. Return early to avoid disabling test on
  // all of Windows.
  if (base::win::GetVersion() <= base::win::Version::WIN7)
    return;
#endif

  // Test that an icon for a file type (group) can be loaded even
  // where a file does not exist. Should work cross platform.
  base::RunLoop runner;
  TestIconLoader test_loader(runner.QuitClosure());
  test_loader.TryLoadIcon(base::FilePath(kGroupOnlyFilename),
                          IconLoader::NORMAL, scale);

  runner.Run();
  EXPECT_TRUE(test_loader.load_succeeded());
}
#endif  // !((defined(OS_LINUX) || defined(OS_CHROMEOS)) &&
        // defined(MEMORY_SANITIZER))

#if defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(IconLoaderBrowserTest, LoadExeIcon) {
  float scale = display::win::GetDPIScale();
  base::RunLoop runner;

  TestIconLoader test_loader(runner.QuitClosure());

  base::FilePath exe_path;
  base::PathService::Get(base::FILE_EXE, &exe_path);
  test_loader.TryLoadIcon(exe_path, IconLoader::NORMAL, scale);

  runner.Run();
  EXPECT_TRUE(test_loader.load_succeeded());
}

const base::FilePath::CharType kNotExistingExeFile[] =
    FILE_PATH_LITERAL("unlikely-to-exist-file.exe");

IN_PROC_BROWSER_TEST_F(IconLoaderBrowserTest, LoadDefaultExeIcon) {
  float scale = display::win::GetDPIScale();

  base::RunLoop runner;

  TestIconLoader test_loader(runner.QuitClosure());

  test_loader.TryLoadIcon(base::FilePath(kNotExistingExeFile),
                          IconLoader::NORMAL, scale);

  runner.Run();
  EXPECT_TRUE(test_loader.load_succeeded());
}
#endif  // OS_WIN
