// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_APP_RESTORE_RESTORE_DATA_H_
#define COMPONENTS_APP_RESTORE_RESTORE_DATA_H_

#include <map>
#include <memory>

#include "base/component_export.h"
#include "components/app_restore/app_restore_data.h"

namespace base {
class Value;
}

namespace app_restore {

struct AppLaunchInfo;
struct WindowInfo;

// This class is responsible for saving all app launch and app windows
// information. It can be converted to JSON format to be written to the
// FullRestoreData file.
class COMPONENT_EXPORT(APP_RESTORE) RestoreData {
 public:
  // Map from a window id to AppRestoreData.
  using LaunchList = std::map<int, std::unique_ptr<AppRestoreData>>;

  // Map from an app id to LaunchList.
  using AppIdToLaunchList = std::map<std::string, LaunchList>;

  RestoreData();
  explicit RestoreData(std::unique_ptr<base::Value> restore_data_value);
  RestoreData(const RestoreData&) = delete;
  RestoreData& operator=(const RestoreData&) = delete;
  ~RestoreData();

  std::unique_ptr<RestoreData> Clone() const;

  // Converts |app_id_to_launch_list_| to base::Value, e.g.:
  // {
  //   "odknhmnlageboeamepcngndbggdpaobj":    // app_id
  //     {
  //       "403":                             // window_id
  //         {
  //           "container": 0,
  //           "disposition": 1,
  //           "display_id": "22000000",
  //           "index": 3,
  //           "desk_id": 1,
  //           "restored_bounds": { 0, 100, 200, 300 },
  //           "current_bounds": { 100, 200, 200, 300 },
  //           "window_state_type": 256,
  //         },
  //     },
  //   "pjibgclleladliembfgfagdaldikeohf":    // app_id
  //     {
  //       "413":                             // window_id
  //         {
  //           "container": 0,
  //           "disposition": 3,
  //           "display_id": "22000000",
  //           ...
  //         },
  //       "415":                             // window_id
  //         {
  //           ...
  //         },
  //     },
  // }
  base::Value ConvertToValue() const;

  // Returns true if there are app type browsers. Otherwise, returns false.
  bool HasAppTypeBrowser() const;

  // Returns true if there are normal browsers. Otherwise, returns false.
  bool HasBrowser() const;

  // Returns true if there is a AppRestoreData for the given |app_id| and
  // |window_id|. Otherwise, returns false.
  bool HasAppRestoreData(const std::string& app_id, int32_t window_id);

  // Adds |app_launch_info| to |app_id_to_launch_list_|.
  void AddAppLaunchInfo(std::unique_ptr<AppLaunchInfo> app_launch_info);

  // Modify the window id for |app_id| from |old_window_id| to |new_window_id|.
  // This function is used for ARC ghost window only, to switch the window id
  // from the session id to the task id.
  void ModifyWindowId(const std::string& app_id,
                      int32_t old_window_id,
                      int32_t new_window_id);

  // Modifies the window's information based on |window_info| for the window
  // with |window_id| of the app with |app_id|.
  void ModifyWindowInfo(const std::string& app_id,
                        int32_t window_id,
                        const WindowInfo& window_info);

  // Modifies the window's theme colors for the window with |window_id| of the
  // app with |app_id|,
  void ModifyThemeColor(const std::string& app_id,
                        int32_t window_id,
                        uint32_t primary_color,
                        uint32_t status_bar_color);

  // Modifies |chrome_app_id_to_current_window_id_| to set the next restore
  // window id for the given |app_id|.
  //
  // If there is only 1 window for |app_id|, its window id is set as the
  // restore window id to restore window properties when there is a window
  // created for |app_id|.
  //
  // If there is more than 1 window for |app_id|, we can't know which window is
  // for which launching, so activation_index for all windows are set as
  // INT32_MIN to send all windows to the background. The first record in
  // LaunchList is set as the restore window id for |app_id|.
  void SetNextRestoreWindowIdForChromeApp(const std::string& app_id);

  // Removes a AppRestoreData with |window_id| for |app_id|.
  void RemoveAppRestoreData(const std::string& app_id, int window_id);

  // Sends the window for |app_id| and |window_id| to background.
  void SendWindowToBackground(const std::string& app_id, int window_id);

  // Removes the launch list for |app_id|.
  void RemoveApp(const std::string& app_id);

  // Gets the app launch information with `window_id` for `app_id`.
  std::unique_ptr<AppLaunchInfo> GetAppLaunchInfo(const std::string& app_id,
                                                  int window_id);

  // Gets the window information with |window_id| for |app_id|.
  std::unique_ptr<WindowInfo> GetWindowInfo(const std::string& app_id,
                                            int window_id);

  // Fetches the restore window id from the restore data for the given chrome
  // app |app_id|. |app_id| should be a Chrome app id.
  //
  // If there is only 1 window for |app_id|, return its window id, and remove
  // the record of |app_id| in |chrome_app_id_to_current_window_id_|, so that
  // when there are more windows created for |app_id|, FetchRestoreWindowId
  // returns 0, and we know they are not the restored window, but launched by
  // the user.
  //
  // If there is more than 1 window for |app_id|, returns the window id saved in
  // |chrome_app_id_to_current_window_id_|, then modify
  // |chrome_app_id_to_current_window_id_| to set the next restore window id.
  //
  // For example,
  // app_id: 'aa' {window id: 1};
  // app_id: 'bb' {window id: 11, 12, 13};
  // chrome_app_id_to_current_window_id_: 'aa': 1 'bb': 11
  //
  // FetchRestoreWindowId('aa') return 1.
  // Then chrome_app_id_to_current_window_id_: 'bb': 11
  // FetchRestoreWindowId('aa') return 0.
  //
  // FetchRestoreWindowId('bb') return 11.
  // Then chrome_app_id_to_current_window_id_: 'bb': 12
  // FetchRestoreWindowId('bb') return 12.
  // Then chrome_app_id_to_current_window_id_: 'bb': 13
  // FetchRestoreWindowId('bb') return 13.
  // Then chrome_app_id_to_current_window_id_ is empty.
  // FetchRestoreWindowId('bb') return 0.
  int32_t FetchRestoreWindowId(const std::string& app_id);

  const AppRestoreData* GetAppRestoreData(const std::string& app_id,
                                          int window_id) const;

  const AppIdToLaunchList& app_id_to_launch_list() const {
    return app_id_to_launch_list_;
  }

 private:
  // Returns the pointer to AppRestoreData for the given |app_id| and
  // |window_id|. Returns null if there is no AppRestoreData.
  AppRestoreData* GetAppRestoreDataMutable(const std::string& app_id,
                                           int window_id);

  AppIdToLaunchList app_id_to_launch_list_;

  // Saves the next restore window_id to be handled for each chrome app.
  std::map<std::string, int> chrome_app_id_to_current_window_id_;
};

}  // namespace app_restore

#endif  // COMPONENTS_APP_RESTORE_RESTORE_DATA_H_
