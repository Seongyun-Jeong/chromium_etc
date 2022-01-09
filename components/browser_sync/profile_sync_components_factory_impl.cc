// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browser_sync/profile_sync_components_factory_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/memory/ref_counted.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/autofill/core/browser/payments/autofill_wallet_model_type_controller.h"
#include "components/autofill/core/browser/webdata/autocomplete_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_profile_model_type_controller.h"
#include "components/autofill/core/browser/webdata/autofill_profile_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_wallet_metadata_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_wallet_offer_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_wallet_sync_bridge.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/browser_sync/active_devices_provider_impl.h"
#include "components/browser_sync/browser_sync_client.h"
#include "components/history/core/browser/sync/history_delete_directives_model_type_controller.h"
#include "components/history/core/browser/sync/typed_url_model_type_controller.h"
#include "components/history/core/common/pref_names.h"
#include "components/password_manager/core/browser/password_store_interface.h"
#include "components/password_manager/core/browser/sync/password_model_type_controller.h"
#include "components/prefs/pref_service.h"
#include "components/reading_list/features/reading_list_switches.h"
#include "components/send_tab_to_self/features.h"
#include "components/send_tab_to_self/send_tab_to_self_model_type_controller.h"
#include "components/send_tab_to_self/send_tab_to_self_sync_service.h"
#include "components/sync/base/legacy_directory_deletion.h"
#include "components/sync/base/report_unrecoverable_error.h"
#include "components/sync/base/sync_base_switches.h"
#include "components/sync/base/sync_prefs.h"
#include "components/sync/driver/data_type_manager_impl.h"
#include "components/sync/driver/glue/sync_engine_impl.h"
#include "components/sync/driver/glue/sync_transport_data_prefs.h"
#include "components/sync/driver/model_type_controller.h"
#include "components/sync/driver/sync_driver_switches.h"
#include "components/sync/driver/syncable_service_based_model_type_controller.h"
#include "components/sync/engine/sync_engine.h"
#include "components/sync/invalidations/sync_invalidations_service.h"
#include "components/sync/model/forwarding_model_type_controller_delegate.h"
#include "components/sync/model/model_type_store_service.h"
#include "components/sync/model/proxy_model_type_controller_delegate.h"
#include "components/sync_bookmarks/bookmark_sync_service.h"
#include "components/sync_device_info/device_info_sync_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/sync_sessions/proxy_tabs_data_type_controller.h"
#include "components/sync_sessions/session_model_type_controller.h"
#include "components/sync_sessions/session_sync_service.h"
#include "components/sync_user_events/user_event_model_type_controller.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#endif

using syncer::DataTypeController;
using syncer::DataTypeManager;
using syncer::DataTypeManagerImpl;
using syncer::DataTypeManagerObserver;
using syncer::ModelTypeController;
using syncer::SyncableServiceBasedModelTypeController;

namespace browser_sync {

namespace {

// These helper functions only wrap the factory functions of the bridges. This
// way, it simplifies life for the compiler which cannot directly cast
// "WeakPtr<ModelTypeSyncBridge> (AutofillWebDataService*)" to
// "WeakPtr<ModelTypeControllerDelegate> (AutofillWebDataService*)".
base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutocompleteDelegateFromDataService(autofill::AutofillWebDataService* service) {
  return autofill::AutocompleteSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillProfileDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillProfileSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillWalletDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillWalletSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillWalletMetadataDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillWalletMetadataSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

base::WeakPtr<syncer::ModelTypeControllerDelegate>
AutofillWalletOfferDelegateFromDataService(
    autofill::AutofillWebDataService* service) {
  return autofill::AutofillWalletOfferSyncBridge::FromWebDataService(service)
      ->change_processor()
      ->GetControllerDelegate();
}

// Helper function that deals will null (e.g. tests, iOS webview).
base::WeakPtr<syncer::SyncableService> SyncableServiceForPrefs(
    sync_preferences::PrefServiceSyncable* prefs_service,
    syncer::ModelType type) {
  return prefs_service ? prefs_service->GetSyncableService(type)->AsWeakPtr()
                       : nullptr;
}

}  // namespace

ProfileSyncComponentsFactoryImpl::ProfileSyncComponentsFactoryImpl(
    browser_sync::BrowserSyncClient* sync_client,
    version_info::Channel channel,
    const scoped_refptr<base::SequencedTaskRunner>& ui_thread,
    const scoped_refptr<base::SequencedTaskRunner>& db_thread,
    const scoped_refptr<autofill::AutofillWebDataService>&
        web_data_service_on_disk,
    const scoped_refptr<autofill::AutofillWebDataService>&
        web_data_service_in_memory,
    const scoped_refptr<password_manager::PasswordStoreInterface>&
        profile_password_store,
    const scoped_refptr<password_manager::PasswordStoreInterface>&
        account_password_store,
    sync_bookmarks::BookmarkSyncService* bookmark_sync_service)
    : sync_client_(sync_client),
      channel_(channel),
      ui_thread_(ui_thread),
      db_thread_(db_thread),
      engines_and_directory_deletion_thread_(
          base::ThreadPool::CreateSequencedTaskRunner(
              {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
               base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      web_data_service_on_disk_(web_data_service_on_disk),
      web_data_service_in_memory_(web_data_service_in_memory),
      profile_password_store_(profile_password_store),
      account_password_store_(account_password_store),
      bookmark_sync_service_(bookmark_sync_service) {
  DCHECK(sync_client_);
}

ProfileSyncComponentsFactoryImpl::~ProfileSyncComponentsFactoryImpl() = default;

syncer::DataTypeController::TypeVector
ProfileSyncComponentsFactoryImpl::CreateCommonDataTypeControllers(
    syncer::ModelTypeSet disabled_types,
    syncer::SyncService* sync_service) {
  syncer::DataTypeController::TypeVector controllers;

  const base::RepeatingClosure dump_stack =
      base::BindRepeating(&syncer::ReportUnrecoverableError, channel_);

  syncer::ModelTypeStoreService* model_type_store_service =
      sync_client_->GetModelTypeStoreService();
  DCHECK(model_type_store_service);
  syncer::RepeatingModelTypeStoreFactory model_type_store_factory =
      model_type_store_service->GetStoreFactory();

  // TODO(crbug.com/1005651): Consider using a separate delegate for
  // transport-only.
  controllers.push_back(std::make_unique<ModelTypeController>(
      syncer::DEVICE_INFO,
      /*delegate_for_full_sync_mode=*/
      std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
          sync_client_->GetDeviceInfoSyncService()
              ->GetControllerDelegate()
              .get()),
      /*delegate_for_transport_mode=*/
      std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
          sync_client_->GetDeviceInfoSyncService()
              ->GetControllerDelegate()
              .get())));

  // These features are enabled only if there's a DB thread to post tasks to.
  if (db_thread_) {
    // Autocomplete sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL)) {
      controllers.push_back(std::make_unique<ModelTypeController>(
          syncer::AUTOFILL,
          std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
              db_thread_, base::BindRepeating(
                              &AutocompleteDelegateFromDataService,
                              base::RetainedRef(web_data_service_on_disk_)))));
    }

    // Autofill sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_PROFILE)) {
      controllers.push_back(
          std::make_unique<AutofillProfileModelTypeController>(
              std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
                  db_thread_,
                  base::BindRepeating(
                      &AutofillProfileDelegateFromDataService,
                      base::RetainedRef(web_data_service_on_disk_))),
              sync_client_->GetPrefService(), sync_service));
    }

    // Wallet data sync is enabled by default. Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_WALLET_DATA)) {
      controllers.push_back(CreateWalletModelTypeControllerWithInMemorySupport(
          syncer::AUTOFILL_WALLET_DATA,
          base::BindRepeating(&AutofillWalletDelegateFromDataService),
          sync_service));
    }

    // Wallet metadata sync depends on Wallet data sync. Register if neither
    // Wallet data nor Wallet metadata sync is explicitly disabled.
    if (!disabled_types.Has(syncer::AUTOFILL_WALLET_DATA) &&
        !disabled_types.Has(syncer::AUTOFILL_WALLET_METADATA)) {
      controllers.push_back(CreateWalletModelTypeController(
          syncer::AUTOFILL_WALLET_METADATA,
          base::BindRepeating(&AutofillWalletMetadataDelegateFromDataService),
          sync_service));
    }

    // Wallet offer data is enabled by default. Register unless explicitly
    // disabled.
    // TODO(crbug.com/1112095): Currently the offer data depends on Wallet data
    // sync, but revisit after other offer types are implemented.
    if (base::FeatureList::IsEnabled(switches::kSyncAutofillWalletOfferData) &&
        !disabled_types.Has(syncer::AUTOFILL_WALLET_DATA) &&
        !disabled_types.Has(syncer::AUTOFILL_WALLET_OFFER)) {
      controllers.push_back(CreateWalletModelTypeController(
          syncer::AUTOFILL_WALLET_OFFER,
          base::BindRepeating(&AutofillWalletOfferDelegateFromDataService),
          sync_service));
    }
  }

  // Bookmark sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!disabled_types.Has(syncer::BOOKMARKS)) {
    favicon::FaviconService* favicon_service =
        sync_client_->GetFaviconService();
    // Services can be null in tests.
    if (bookmark_sync_service_ && favicon_service) {
      controllers.push_back(std::make_unique<ModelTypeController>(
          syncer::BOOKMARKS,
          std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
              bookmark_sync_service_
                  ->GetBookmarkSyncControllerDelegate(favicon_service)
                  .get())));
    }
  }

  // These features are enabled only if history is not disabled.
  if (!sync_client_->GetPrefService()->GetBoolean(
          prefs::kSavingBrowserHistoryDisabled)) {
    // TypedUrl sync is enabled by default.  Register unless explicitly
    // disabled.
    if (!disabled_types.Has(syncer::TYPED_URLS)) {
      // TypedURLModelTypeController uses a proxy delegate internally, as
      // provided by HistoryService.
      controllers.push_back(
          std::make_unique<history::TypedURLModelTypeController>(
              sync_client_->GetHistoryService(),
              sync_client_->GetPrefService()));
    }

    // Delete directive sync is enabled by default.
    if (!disabled_types.Has(syncer::HISTORY_DELETE_DIRECTIVES)) {
      controllers.push_back(
          std::make_unique<history::HistoryDeleteDirectivesModelTypeController>(
              dump_stack, sync_service,
              sync_client_->GetModelTypeStoreService(),
              sync_client_->GetHistoryService()));
    }

    // Session sync is enabled by default.  This is disabled if history is
    // disabled because the tab sync data is added to the web history on the
    // server.
    if (!disabled_types.Has(syncer::PROXY_TABS)) {
      controllers.push_back(
          std::make_unique<sync_sessions::ProxyTabsDataTypeController>(
              base::BindRepeating(
                  &sync_sessions::SessionSyncService::ProxyTabsStateChanged,
                  base::Unretained(sync_client_->GetSessionSyncService()))));
      controllers.push_back(
          std::make_unique<sync_sessions::SessionModelTypeController>(
              sync_service, sync_client_->GetPrefService(),
              std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
                  sync_client_->GetSessionSyncService()
                      ->GetControllerDelegate()
                      .get())));
    }
  }

  // Password sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!disabled_types.Has(syncer::PASSWORDS)) {
    if (profile_password_store_) {
      // |profile_password_store_| can be null in tests.
      controllers.push_back(
          std::make_unique<password_manager::PasswordModelTypeController>(
              profile_password_store_->CreateSyncControllerDelegate(),
              account_password_store_
                  ? account_password_store_->CreateSyncControllerDelegate()
                  : nullptr,
              account_password_store_, sync_client_->GetPrefService(),
              sync_client_->GetIdentityManager(), sync_service));
    }
  }

  if (!disabled_types.Has(syncer::PREFERENCES)) {
    controllers.push_back(
        std::make_unique<SyncableServiceBasedModelTypeController>(
            syncer::PREFERENCES,
            sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
            SyncableServiceForPrefs(sync_client_->GetPrefServiceSyncable(),
                                    syncer::PREFERENCES),
            dump_stack));
  }

  if (!disabled_types.Has(syncer::PRIORITY_PREFERENCES)) {
    controllers.push_back(
        std::make_unique<SyncableServiceBasedModelTypeController>(
            syncer::PRIORITY_PREFERENCES,
            sync_client_->GetModelTypeStoreService()->GetStoreFactory(),
            SyncableServiceForPrefs(sync_client_->GetPrefServiceSyncable(),
                                    syncer::PRIORITY_PREFERENCES),
            dump_stack));
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // When SyncSettingsCategorization is enabled the controller is created in
  // ChromeSyncClient.
  if (!disabled_types.Has(syncer::PRINTERS) &&
      !chromeos::features::IsSyncSettingsCategorizationEnabled()) {
    controllers.push_back(
        CreateModelTypeControllerForModelRunningOnUIThread(syncer::PRINTERS));
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Reading list sync is enabled by default only on iOS. Register unless
  // Reading List or Reading List Sync is explicitly disabled.
  if (!disabled_types.Has(syncer::READING_LIST) &&
      reading_list::switches::IsReadingListEnabled()) {
    controllers.push_back(CreateModelTypeControllerForModelRunningOnUIThread(
        syncer::READING_LIST));
  }

  if (!disabled_types.Has(syncer::USER_EVENTS)) {
    controllers.push_back(
        std::make_unique<syncer::UserEventModelTypeController>(
            sync_service,
            CreateForwardingControllerDelegate(syncer::USER_EVENTS)));
  }

  if (!disabled_types.Has(syncer::SEND_TAB_TO_SELF)) {
    syncer::ModelTypeControllerDelegate* delegate =
        sync_client_->GetSendTabToSelfSyncService()
            ->GetControllerDelegate()
            .get();
    controllers.push_back(
        std::make_unique<send_tab_to_self::SendTabToSelfModelTypeController>(
            /*delegate_for_full_sync_mode=*/
            std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
                delegate),
            /*delegate_for_transport_mode=*/
            base::FeatureList::IsEnabled(
                send_tab_to_self::kSendTabToSelfWhenSignedIn)
                ? std::make_unique<
                      syncer::ForwardingModelTypeControllerDelegate>(delegate)
                : nullptr));
  }

  if (!disabled_types.Has(syncer::USER_CONSENTS)) {
    // Forward both full-sync and transport-only modes to the same delegate,
    // since behavior for USER_CONSENTS does not differ (they are always
    // persisted).
    controllers.push_back(std::make_unique<ModelTypeController>(
        syncer::USER_CONSENTS,
        /*delegate_for_full_sync_mode=*/
        CreateForwardingControllerDelegate(syncer::USER_CONSENTS),
        /*delegate_for_transport_mode=*/
        CreateForwardingControllerDelegate(syncer::USER_CONSENTS)));
  }

  return controllers;
}

std::unique_ptr<DataTypeManager>
ProfileSyncComponentsFactoryImpl::CreateDataTypeManager(
    const syncer::WeakHandle<syncer::DataTypeDebugInfoListener>&
        debug_info_listener,
    const DataTypeController::TypeMap* controllers,
    const syncer::DataTypeEncryptionHandler* encryption_handler,
    syncer::ModelTypeConfigurer* configurer,
    DataTypeManagerObserver* observer) {
  return std::make_unique<DataTypeManagerImpl>(debug_info_listener, controllers,
                                               encryption_handler, configurer,
                                               observer);
}

std::unique_ptr<syncer::SyncEngine>
ProfileSyncComponentsFactoryImpl::CreateSyncEngine(
    const std::string& name,
    invalidation::InvalidationService* invalidator,
    syncer::SyncInvalidationsService* sync_invalidation_service) {
  return std::make_unique<syncer::SyncEngineImpl>(
      name, invalidator, sync_invalidation_service,
      std::make_unique<browser_sync::ActiveDevicesProviderImpl>(
          sync_client_->GetDeviceInfoSyncService()->GetDeviceInfoTracker(),
          base::DefaultClock::GetInstance()),
      std::make_unique<syncer::SyncTransportDataPrefs>(
          sync_client_->GetPrefService()),
      sync_client_->GetModelTypeStoreService()->GetSyncDataPath(),
      engines_and_directory_deletion_thread_,
      base::BindRepeating(&syncer::SyncClient::OnLocalSyncTransportDataCleared,
                          base::Unretained(sync_client_)));
}

void ProfileSyncComponentsFactoryImpl::ClearAllTransportData() {
  syncer::SyncTransportDataPrefs sync_transport_data_prefs(
      sync_client_->GetPrefService());

  // Clearing the Directory via DeleteLegacyDirectoryFilesAndNigoriStorage()
  // means there's IO involved which may be considerable overhead if
  // triggered consistently upon browser startup (which is the case for
  // certain codepaths such as the user being signed out). To avoid that, prefs
  // are used to determine whether it's worth it.
  if (!sync_transport_data_prefs.GetCacheGuid().empty()) {
    engines_and_directory_deletion_thread_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &syncer::DeleteLegacyDirectoryFilesAndNigoriStorage,
            sync_client_->GetModelTypeStoreService()->GetSyncDataPath()));
  }

  sync_transport_data_prefs.ClearAll();
  sync_client_->OnLocalSyncTransportDataCleared();
}

std::unique_ptr<syncer::ModelTypeControllerDelegate>
ProfileSyncComponentsFactoryImpl::CreateForwardingControllerDelegate(
    syncer::ModelType type) {
  return std::make_unique<syncer::ForwardingModelTypeControllerDelegate>(
      sync_client_->GetControllerDelegateForModelType(type).get());
}

std::unique_ptr<ModelTypeController> ProfileSyncComponentsFactoryImpl::
    CreateModelTypeControllerForModelRunningOnUIThread(syncer::ModelType type) {
  return std::make_unique<ModelTypeController>(
      type, CreateForwardingControllerDelegate(type));
}

std::unique_ptr<ModelTypeController>
ProfileSyncComponentsFactoryImpl::CreateWalletModelTypeController(
    syncer::ModelType type,
    const base::RepeatingCallback<
        base::WeakPtr<syncer::ModelTypeControllerDelegate>(
            autofill::AutofillWebDataService*)>& delegate_from_web_data,
    syncer::SyncService* sync_service) {
  return std::make_unique<AutofillWalletModelTypeController>(
      type,
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_on_disk_))),
      sync_client_->GetPrefService(), sync_service);
}

std::unique_ptr<ModelTypeController> ProfileSyncComponentsFactoryImpl::
    CreateWalletModelTypeControllerWithInMemorySupport(
        syncer::ModelType type,
        const base::RepeatingCallback<
            base::WeakPtr<syncer::ModelTypeControllerDelegate>(
                autofill::AutofillWebDataService*)>& delegate_from_web_data,
        syncer::SyncService* sync_service) {
  return std::make_unique<AutofillWalletModelTypeController>(
      type, /*delegate_for_full_sync_mode=*/
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_on_disk_))),
      /*delegate_for_transport_mode=*/
      std::make_unique<syncer::ProxyModelTypeControllerDelegate>(
          db_thread_,
          base::BindRepeating(delegate_from_web_data,
                              base::RetainedRef(web_data_service_in_memory_))),
      sync_client_->GetPrefService(), sync_service);
}

}  // namespace browser_sync
