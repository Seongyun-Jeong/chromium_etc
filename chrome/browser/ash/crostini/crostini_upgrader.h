// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_CROSTINI_CROSTINI_UPGRADER_H_
#define CHROME_BROWSER_ASH_CROSTINI_CROSTINI_UPGRADER_H_

#include "base/callback_forward.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ash/crostini/crostini_export_import.h"
#include "chrome/browser/ash/crostini/crostini_export_import_status_tracker.h"
#include "chrome/browser/ash/crostini/crostini_manager.h"
#include "chrome/browser/ash/crostini/crostini_upgrader_ui_delegate.h"
#include "chromeos/dbus/power/power_manager_client.h"
#include "components/keyed_service/core/keyed_service.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class Profile;

namespace crostini {

class CrostiniUpgrader : public KeyedService,
                         public UpgradeContainerProgressObserver,
                         public chromeos::PowerManagerClient::Observer,
                         public CrostiniUpgraderUIDelegate {
 public:
  static CrostiniUpgrader* GetForProfile(Profile* profile);

  explicit CrostiniUpgrader(Profile* profile);
  CrostiniUpgrader(const CrostiniUpgrader&) = delete;
  CrostiniUpgrader& operator=(const CrostiniUpgrader&) = delete;

  ~CrostiniUpgrader() override;

  // KeyedService:
  void Shutdown() override;

  // CrostiniUpgraderUIDelegate:
  void AddObserver(CrostiniUpgraderUIObserver* observer) override;
  void RemoveObserver(CrostiniUpgraderUIObserver* observer) override;
  void Backup(const ContainerId& container_id,
              bool show_file_chooser,
              content::WebContents* web_contents) override;
  void StartPrechecks() override;
  void Upgrade(const ContainerId& container_id) override;
  void Restore(const ContainerId& container_id,
               content::WebContents* web_contents) override;
  void Cancel() override;
  void CancelBeforeStart() override;

  // CrostiniManager::UpgradeContainerProgressObserver:
  void OnUpgradeContainerProgress(
      const ContainerId& container_id,
      UpgradeContainerProgressStatus status,
      const std::vector<std::string>& messages) override;

  // chromeos::PowerManagerClient::Observer:
  void PowerChanged(const power_manager::PowerSupplyProperties& proto) override;

  // Return true if internal state allows starting upgrade.
  bool CanUpgrade();

  // Require at least 1 GiB of free space. Experiments on an unmodified
  // container suggest this is a bare minimum, anyone with a substantial amount
  // of programs installed will likely require more.
  static constexpr int64_t kDiskRequired = 1 << 30;

 private:
  // Write a vector of log messages to `current_log_file_` on the
  // `log_sequence_`, which allows blocking operations.
  void WriteLogMessages(std::vector<std::string> messages);

  void OnBackupPathChecked(const ContainerId& container_id,
                           content::WebContents* web_contents,
                           base::FilePath path,
                           bool path_exists);
  // Called when backup completes. If backup was completed successfully (which
  // is different from if |result|==SUCCESS) the |backup_path| will contain a
  // path to the backup tarball.
  void OnBackup(CrostiniResult result,
                absl::optional<base::FilePath> backup_path);
  void OnCancel(CrostiniResult result);
  void OnBackupProgress(int progress_percent);
  void OnUpgrade(CrostiniResult result);
  void OnAvailableDiskSpace(int64_t bytes);
  void DoPrechecks();
  void OnRestorePathChecked(const ContainerId& container_id,
                            content::WebContents* web_contents,
                            base::FilePath path,
                            bool path_exists);
  void OnRestore(CrostiniResult result);
  void OnRestoreProgress(int progress_percent);
  CrostiniExportImport::OnceTrackerFactory MakeFactory();

  class StatusTracker : public CrostiniExportImportStatusTracker {
   public:
    explicit StatusTracker(base::WeakPtr<CrostiniUpgrader> upgrader,
                           ExportImportType type,
                           base::FilePath path);
    ~StatusTracker() override;

    // CrostiniExportImportStatusTracker:
    void SetStatusRunningUI(int progress_percent) override;
    void SetStatusCancellingUI() override {}
    void SetStatusDoneUI() override;
    void SetStatusCancelledUI() override;
    void SetStatusFailedWithMessageUI(Status status,
                                      const std::u16string& message) override;

   private:
    bool has_notified_start_ = false;
    base::WeakPtr<CrostiniUpgrader> upgrader_;
  };
  friend class StatusTracker;

  Profile* profile_;
  ContainerId container_id_;
  base::ObserverList<CrostiniUpgraderUIObserver>::Unchecked upgrader_observers_;

  base::RepeatingClosure prechecks_callback_;
  bool power_status_good_ = false;
  int64_t free_disk_space_ = -1;

  // A sequence for writing upgrade logs to the file system.
  scoped_refptr<base::SequencedTaskRunner> log_sequence_;
  // Path to the current log file. Generating the path is a blocking operation,
  // so we set it to absl::nullopt until we get a response.
  absl::optional<base::FilePath> current_log_file_;
  // Buffer for storing log messages that arrive while the log file is being
  // created.
  std::vector<std::string> log_buffer_;

  base::ScopedObservation<chromeos::PowerManagerClient,
                          chromeos::PowerManagerClient::Observer>
      pmc_observation_{this};

  // When restoring after a failed upgrade, if the user successfully completed a
  // backup, we will auto-restore from that (if the file still exists),
  // otherwise |backup_path_|==nullopt and restore will bring up a file-chooser.
  absl::optional<base::FilePath> backup_path_;

  base::WeakPtrFactory<CrostiniUpgrader> weak_ptr_factory_{this};
};

}  // namespace crostini

#endif  // CHROME_BROWSER_ASH_CROSTINI_CROSTINI_UPGRADER_H_
