// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/plugin_vm/plugin_vm_util.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_drive_image_download_service.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_features.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_installer_factory.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_manager.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_manager_factory.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_pref_names.h"
#include "chrome/browser/ash/settings/cros_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/shelf/chrome_shelf_controller.h"
#include "chromeos/dbus/dlcservice/dlcservice_client.h"
#include "components/exo/shell_surface_util.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "net/base/url_util.h"

namespace plugin_vm {

const char kPluginVmShelfAppId[] = "lgjpclljbbmphhnalkeplcmnjpfmmaek";
const char kPluginVmName[] = "PvmDefault";
const char kChromeOSBaseDirectoryDisplayText[] = "Network \u203a ChromeOS";

namespace {

static std::string& MutableFakeLicenseKey() {
  static base::NoDestructor<std::string> license_key;
  return *license_key;
}

base::RepeatingClosureList& GetFakeLicenseKeyListeners() {
  static base::NoDestructor<base::RepeatingClosureList> instance;
  return *instance;
}

}  // namespace

bool IsPluginVmRunning(Profile* profile) {
  return plugin_vm::PluginVmManagerFactory::GetForProfile(profile)
                 ->vm_state() ==
             vm_tools::plugin_dispatcher::VmState::VM_STATE_RUNNING &&
         ChromeShelfController::instance()->IsOpen(
             ash::ShelfID(kPluginVmShelfAppId));
}

bool IsPluginVmAppWindow(const aura::Window* window) {
  const std::string* app_id = exo::GetShellApplicationId(window);
  if (!app_id)
    return false;
  return *app_id == "org.chromium.plugin_vm_ui";
}

std::string GetPluginVmUserIdForProfile(const Profile* profile) {
  DCHECK(profile);
  return profile->GetPrefs()->GetString(plugin_vm::prefs::kPluginVmUserId);
}

void SetFakePluginVmPolicy(Profile* profile,
                           const std::string& image_url,
                           const std::string& image_hash,
                           const std::string& license_key) {
  DictionaryPrefUpdate update(profile->GetPrefs(),
                              plugin_vm::prefs::kPluginVmImage);
  base::DictionaryValue* dict = update.Get();
  dict->SetPath(prefs::kPluginVmImageUrlKeyName, base::Value(image_url));
  dict->SetPath(prefs::kPluginVmImageHashKeyName, base::Value(image_hash));
  plugin_vm::PluginVmInstallerFactory::GetForProfile(profile)
      ->SkipLicenseCheckForTesting();  // IN-TEST
  MutableFakeLicenseKey() = license_key;
  GetFakeLicenseKeyListeners().Notify();
}

std::string GetFakeLicenseKey() {
  return MutableFakeLicenseKey();
}

bool FakeLicenseKeyIsSet() {
  return !MutableFakeLicenseKey().empty();
}

void RemoveDriveDownloadDirectoryIfExists() {
  auto log_file_deletion_if_failed = [](bool success) {
    if (!success) {
      LOG(ERROR) << "PluginVM failed to delete download directory";
    }
  };

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&base::DeletePathRecursively,
                     base::FilePath(kPluginVmDriveDownloadDirectory)),
      base::BindOnce(std::move(log_file_deletion_if_failed)));
}

absl::optional<std::string> GetIdFromDriveUrl(const GURL& url) {
  const std::string& spec = url.spec();

  const std::string kOpenUrlBase = "https://drive.google.com/open?";
  if (base::StartsWith(spec, kOpenUrlBase,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    // e.g. https://drive.google.com/open?id=[ID]
    std::string id;
    if (!net::GetValueForKeyInQuery(url, "id", &id))
      return absl::nullopt;
    return id;
  }

  // These will match some invalid URLs, which is fine.
  const std::string kViewUrlPatternWithDomain =
      "https://drive.google.com/a/*/file/d/*/view*";
  const std::string kViewUrlPatternWithoutDomain =
      "https://drive.google.com/file/d/*/view*";
  if (base::MatchPattern(spec, kViewUrlPatternWithDomain) ||
      base::MatchPattern(spec, kViewUrlPatternWithoutDomain)) {
    // e.g. https://drive.google.com/a/example.org/file/d/[ID]/view?usp=sharing
    // or https://drive.google.com/file/d/[ID]/view?usp=sharing
    size_t id_end = spec.find("/view");
    size_t id_start = spec.rfind('/', id_end - 1) + 1;
    return spec.substr(id_start, id_end - id_start);
  }

  return absl::nullopt;
}

PluginVmPolicySubscription::PluginVmPolicySubscription(
    Profile* profile,
    base::RepeatingCallback<void(bool)> callback)
    : profile_(profile), callback_(callback) {
  DCHECK(ash::CrosSettings::IsInitialized());
  ash::CrosSettings* cros_settings = ash::CrosSettings::Get();
  // Subscriptions are automatically removed when this object is destroyed.
  pref_change_registrar_ = std::make_unique<PrefChangeRegistrar>();
  pref_change_registrar_->Init(profile->GetPrefs());
  pref_change_registrar_->Add(
      plugin_vm::prefs::kPluginVmAllowed,
      base::BindRepeating(&PluginVmPolicySubscription::OnPolicyChanged,
                          base::Unretained(this)));
  pref_change_registrar_->Add(
      plugin_vm::prefs::kPluginVmUserId,
      base::BindRepeating(&PluginVmPolicySubscription::OnPolicyChanged,
                          base::Unretained(this)));
  device_allowed_subscription_ = cros_settings->AddSettingsObserver(
      ash::kPluginVmAllowed,
      base::BindRepeating(&PluginVmPolicySubscription::OnPolicyChanged,
                          base::Unretained(this)));
  fake_license_subscription_ = GetFakeLicenseKeyListeners().Add(
      base::BindRepeating(&PluginVmPolicySubscription::OnPolicyChanged,
                          base::Unretained(this)));

  is_allowed_ = PluginVmFeatures::Get()->IsAllowed(profile);
}

void PluginVmPolicySubscription::OnPolicyChanged() {
  bool allowed = PluginVmFeatures::Get()->IsAllowed(profile_);
  if (allowed != is_allowed_) {
    is_allowed_ = allowed;
    callback_.Run(allowed);
  }
}

PluginVmPolicySubscription::~PluginVmPolicySubscription() = default;

}  // namespace plugin_vm
