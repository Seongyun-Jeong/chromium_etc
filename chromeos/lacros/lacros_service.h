// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_LACROS_LACROS_SERVICE_H_
#define CHROMEOS_LACROS_LACROS_SERVICE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/component_export.h"
#include "base/containers/contains.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list_threadsafe.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/token.h"
#include "chromeos/components/sensors/mojom/cros_sensor_service.mojom.h"
#include "chromeos/crosapi/mojom/account_manager.mojom.h"
#include "chromeos/crosapi/mojom/crosapi.mojom.h"
#include "chromeos/crosapi/mojom/device_attributes.mojom.h"
#include "chromeos/crosapi/mojom/structured_metrics_service.mojom.h"
#include "chromeos/crosapi/mojom/video_capture.mojom.h"
#include "chromeos/lacros/lacros_service_never_blocking_state.h"
#include "chromeos/services/machine_learning/public/mojom/machine_learning_service.mojom.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace chromeos {

class NativeThemeCache;
class SystemIdleCache;

// Forward declaration for class defined in .cc file that holds most of the
// business logic of this class.
class LacrosServiceNeverBlockingState;

// This class is responsible for receiving and routing mojo messages from
// ash-chrome via the mojo::Receiver |sequenced_state_.receiver_|. This class is
// responsible for sending and routing messages to ash-chrome via the
// mojo::Remote |sequenced_state_.crosapi_|. Messages are sent and
// received on a dedicated, never-blocking sequence to avoid deadlocks.
//
// This object is constructed, destroyed, and mostly used on an "affine
// sequence". For most intents and purposes, this is the main/UI thread.
//
// This class is a singleton but is not thread safe. Each method is individually
// documented with threading requirements.
class COMPONENT_EXPORT(CHROMEOS_LACROS) LacrosService {
 public:
  class Observer {
   public:
    // Called when the new policy data is received from Ash.
    virtual void OnPolicyUpdated(
        const std::vector<uint8_t>& policy_fetch_response) {}

   protected:
    virtual ~Observer() = default;
  };

  // The getter is safe to call from all threads.
  //
  // This method returns nullptr very early or late in the application
  // lifecycle. We've chosen to have precise constructor/destructor timings
  // rather than rely on a lazy initializer and no destructor to allow for
  // more precise testing.
  //
  // If this is accessed on a thread other than the affine sequence, the caller
  // must invalidate or destroy the pointer before shutdown. Attempting to use
  // this pointer during shutdown can result in UaF.
  static LacrosService* Get();

  // This class is expected to be constructed and destroyed on the same
  // sequence.
  LacrosService();
  LacrosService(const LacrosService&) = delete;
  LacrosService& operator=(const LacrosService&) = delete;
  ~LacrosService();

  // This can be called on any thread. This call allows LacrosService
  // to start receiving messages from ash-chrome.
  // |browser_version| is the version of lacros-chrome displayed to user in
  // feedback report, etc.
  // It includes both browser version and channel in the format of:
  // {browser version} {channel}
  // For example, "87.0.0.1 dev", "86.0.4240.38 beta".
  void BindReceiver(const std::string& browser_version);

  // Each of these functions guards usage of access to the corresponding remote.
  // Keep these in alphabetical order.
  // Most use-cases of these methods can be replaced by IsAvailable(). See
  // crosapi::mojom::Clipboard for an example.
  bool IsAccountManagerAvailable() const;
  bool IsBrowserCdmFactoryAvailable() const;
  bool IsMediaSessionAudioFocusAvailable() const;
  bool IsMediaSessionAudioFocusDebugAvailable() const;
  bool IsMediaSessionControllerAvailable() const;
  bool IsMetricsReportingAvailable() const;
  bool IsScreenManagerAvailable() const;
  bool IsSensorHalClientAvailable() const;

  // Methods to add/remove observer. Safe to call from any thread.
  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);

  // Notifies that the device account policy is updated with the input data
  // to observers. The data comes as serialized blob of PolicyFetchResponse
  // object.
  // This must be called on the affined sequence.
  void NotifyPolicyUpdated(const std::vector<uint8_t>& policy);

  // Returns whether this interface uses the automatic registration system to be
  // available for immediate use at startup. Any crosapi interface can be
  // registered by using ConstructRemote.
  template <typename CrosapiInterface>
  bool IsRegistered() const {
    return base::Contains(interfaces_, CrosapiInterface::Uuid_);
  }

  // Guards usage to the corresponding crosapi interface. Can only be used with
  // automatically registered interfaces. See IsRegistered().
  template <typename CrosapiInterface>
  bool IsAvailable() const {
    DCHECK(IsRegistered<CrosapiInterface>());
    return interfaces_.find(CrosapiInterface::Uuid_)->second->IsAvailable();
  }

  // Returns the automatically registered remote for a given crosapi interface.
  // Can only be used with automatically registered features that are also
  // available. This method can only be called from the affine sequence (main
  // thread). The returned remote can only be used on the affine sequence (main
  // thread).
  template <typename CrosapiInterface>
  mojo::Remote<CrosapiInterface>& GetRemote() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(affine_sequence_checker_);
    DCHECK(IsAvailable<CrosapiInterface>());
    return interfaces_.find(CrosapiInterface::Uuid_)
        ->second->template Get<CrosapiInterface>();
  }

  // --------------------------------------------------------------------------
  // Some clients will want to use mojo::Remotes on arbitrary sequences (e.g.
  // background threads). The following methods allow the client to construct a
  // mojo::Remote bound to an arbitrary sequence, and pass the other endpoint of
  // the Remote (mojo::PendingReceiver) to ash to set up the interface.
  // --------------------------------------------------------------------------

  // This may be called on any thread.
  void BindAccountManagerReceiver(
      mojo::PendingReceiver<crosapi::mojom::AccountManager> pending_receiver);

  // This may be called on any thread.
  void BindAudioFocusManager(
      mojo::PendingReceiver<media_session::mojom::AudioFocusManager> remote);

  // This may be called on any thread.
  void BindAudioFocusManagerDebug(
      mojo::PendingReceiver<media_session::mojom::AudioFocusManagerDebug>
          remote);

  // This may be called on any thread.
  void BindBrowserCdmFactory(mojo::GenericPendingReceiver receiver);

  // This may be called on any thread.
  void BindGeolocationService(
      mojo::PendingReceiver<crosapi::mojom::GeolocationService>
          pending_receiver);

  // This may be called on any thread.
  void BindMachineLearningService(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService> receiver);

  // This may be called on any thread.
  void BindMediaControllerManager(
      mojo::PendingReceiver<media_session::mojom::MediaControllerManager>
          remote);

  // This may be called on any thread.
  void BindMetricsReporting(
      mojo::PendingReceiver<crosapi::mojom::MetricsReporting> receiver);

  // This may be called on any thread.
  void BindScreenManagerReceiver(
      mojo::PendingReceiver<crosapi::mojom::ScreenManager> pending_receiver);

  // This may be called on any thread.
  void BindSensorHalClient(
      mojo::PendingRemote<chromeos::sensors::mojom::SensorHalClient> remote);

  // OnLacrosStartup method of Crosapi can only be called if this method
  // returns true.
  bool IsOnBrowserStartupAvailable() const;

  // Binds video capture host.
  void BindVideoCaptureDeviceFactory(
      mojo::PendingReceiver<crosapi::mojom::VideoCaptureDeviceFactory>
          pending_receiver);

  // BindVideoCaptureDeviceFactory() can only be used if this method returns
  // true.
  bool IsVideoCaptureDeviceFactoryAvailable() const;

  // Returns BrowserInitParams which is passed from ash-chrome. On launching
  // lacros-chrome from ash-chrome, ash-chrome creates a memory backed file
  // serializes the BrowserInitParams to it, and the forked/executed
  // lacros-chrome process inherits the file descriptor. The data is read
  // in the constructor so is available from the beginning.
  // Note that, in older versions, ash-chrome passes the data via
  // LacrosChromeService::Init() mojo call to lacros-chrome. That case is still
  // handled for backward compatibility, and planned to be removed in the
  // future (crbug.com/1156033). Though, until the removal, it is recommended
  // to consider both cases, specifically, at least not to cause a crash.
  const crosapi::mojom::BrowserInitParams* init_params() const {
    return init_params_.get();
  }

  // Returns SystemIdleCache, which uses IdleInfoObserver to observe idle info
  // changes and caches the results. Requires IsIdleServiceAvailable() for full
  // function, and is robust against unavailability.
  SystemIdleCache* system_idle_cache() { return system_idle_cache_.get(); }

  // Returns the version for an ash interface with a given UUID. Returns -1 if
  // the interface is not found. This is a synchronous version of
  // mojo::Remote::QueryVersion. It relies on Ash M88. Features that need to
  // work on M87 or older should not use this.
  int GetInterfaceVersion(base::Token interface_uuid) const;

  // Sets `init_params_` to the provided value.
  // Useful for tests that cannot setup a full Lacros test environment with a
  // working Mojo connection to Ash.
  void SetInitParamsForTests(crosapi::mojom::BrowserInitParamsPtr init_params);

  using Crosapi = crosapi::mojom::Crosapi;

  // This function binds a pending receiver or remote by posting the
  // corresponding bind task to the |never_blocking_sequence_|.
  // This method is public because not all clients can use the syntax sugar of
  // ConstructRemote(), which relies on the assumption that each crosapi
  // interface only has a single associated Bind* method.
  template <typename PendingReceiverOrRemote,
            void (Crosapi::*bind_func)(PendingReceiverOrRemote)>
  void BindPendingReceiverOrRemote(
      PendingReceiverOrRemote pending_receiver_or_remote) {
    never_blocking_sequence_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &LacrosServiceNeverBlockingState::BindCrosapiFeatureReceiver<
                PendingReceiverOrRemote, bind_func>,
            weak_sequenced_state_, std::move(pending_receiver_or_remote)));
  }

 private:
  // This class is a wrapper around a crosapi remote, e.g.
  // mojo::Remote<crosapi::mojom::Automation>. This base class uses type erasure
  // to allow us to store all instances in a single container.
  class InterfaceEntryBase {
   public:
    virtual ~InterfaceEntryBase();

    // Returns the remote that is being wrapped.
    template <typename CrosapiInterface>
    mojo::Remote<CrosapiInterface>& Get() {
      return *reinterpret_cast<mojo::Remote<CrosapiInterface>*>(GetInternal());
    }

    // Returns whether Ash is sufficiently recent to support the crosapi
    // protocol that the remote is based on.
    bool IsAvailable() const { return available_; }

    // Initialization for the remote and |available_|.
    virtual void MaybeBind(uint32_t crosapi_version, LacrosService* impl) = 0;

   protected:
    InterfaceEntryBase();
    InterfaceEntryBase(const InterfaceEntryBase&) = delete;
    InterfaceEntryBase& operator=(const InterfaceEntryBase&) = delete;

    // Returns a raw pointer to a mojo::Remote<CrosapiInterface>.
    virtual void* GetInternal() = 0;

    // See |IsAvailable|.
    bool available_ = false;
  };

  // LacrosServiceNeverBlockingState is an implementation detail of
  // this class.
  friend class LacrosServiceNeverBlockingState;

  // Needs to access |disable_crosapi_for_testing_|.
  friend class ScopedDisableCrosapiForTesting;

  // Forward declare inner class to give it access to private members.
  template <typename CrosapiInterface,
            void (Crosapi::*bind_func)(mojo::PendingReceiver<CrosapiInterface>),
            uint32_t MethodMinVersion>
  class InterfaceEntry;

  // Returns ash's version of the Crosapi mojo interface version. This
  // determines which interface methods are available. This is safe to call from
  // any sequence. This can only be called after BindReceiver().
  absl::optional<uint32_t> CrosapiVersion() const;

  // Requests ash-chrome to send idle info updates.
  void StartSystemIdleCache();

  // Requests ash-chrome to send native theme info updates.
  void StartNativeThemeCache();

  // This function initializes a remote for a given CrosapiInterface.
  // It performs the following operations:
  //   1) Calls BindNewPipeAndPassReceiver() on the remote.
  //   2) Calls BindPendingReceiverOrRemote() on the PendingReceiver.
  template <typename CrosapiInterface,
            void (Crosapi::*bind_func)(mojo::PendingReceiver<CrosapiInterface>)>
  void InitializeAndBindRemote(mojo::Remote<CrosapiInterface>* remote);

  // This function constructs a new remote for a crosapi interface and stashes
  // it in |interfaces_|. This remote will later be bound during BindReceiver().
  template <typename CrosapiInterface,
            void (Crosapi::*bind_func)(mojo::PendingReceiver<CrosapiInterface>),
            uint32_t MethodMinVersion>
  void ConstructRemote();

  // Tests will set this to |true| which will make all crosapi functionality
  // unavailable. Should be set from ScopedDisableCrosapiForTesting always.
  // TODO(https://crbug.com/1131722): Ideally we could stub this out or make
  // this functional for tests without modifying production code
  static bool disable_crosapi_for_testing_;

  // BrowserService implementation injected by chrome/. Must only be used on the
  // affine sequence.
  // TODO(hidehiko): Remove this.
  std::unique_ptr<crosapi::mojom::BrowserService> browser_service_;

  // Parameters passed from ash-chrome.
  crosapi::mojom::BrowserInitParamsPtr init_params_;

  // Receiver and cache of system idle info updates.
  std::unique_ptr<SystemIdleCache> system_idle_cache_;

  // Receiver and cache of native theme info updates.
  std::unique_ptr<NativeThemeCache> native_theme_cache_;

  // A sequence that is guaranteed to never block.
  scoped_refptr<base::SequencedTaskRunner> never_blocking_sequence_;

  // This member is instantiated on the affine sequence alongside the
  // constructor. All subsequent invocations of this member, including
  // destruction, happen on the |never_blocking_sequence_|.
  std::unique_ptr<LacrosServiceNeverBlockingState, base::OnTaskRunnerDeleter>
      sequenced_state_;

  // This member is instantiated on the affine sequence, but only ever
  // dereferenced on the |never_blocking_sequence_|.
  base::WeakPtr<LacrosServiceNeverBlockingState> weak_sequenced_state_;

  // Set to true after BindReceiver() is called.
  bool did_bind_receiver_ = false;

  // The list of observers.
  scoped_refptr<base::ObserverListThreadSafe<Observer>> observer_list_;

  // Each element of |interfaces_| corresponds to a crosapi interface remote
  // (e.g. mojo::Remote<crosapi::mojom::Automation>). The key of the element is
  // the UUID of the crosapi interface. The value is a wrapper around the
  // mojo::Remote. Each element can only be used on the affine sequence. Each
  // element is automatically bound to the corresponding receiver in ash.
  std::map<base::Token, std::unique_ptr<InterfaceEntryBase>> interfaces_;

  // Checks that the method is called on the affine sequence.
  SEQUENCE_CHECKER(affine_sequence_checker_);

  base::WeakPtrFactory<LacrosService> weak_factory_{this};
};

}  // namespace chromeos

#endif  // CHROMEOS_LACROS_LACROS_SERVICE_H_
