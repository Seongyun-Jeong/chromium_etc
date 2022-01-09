// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
#define CHROME_BROWSER_ASH_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/observer_list.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/ash/input_method/assistive_window_controller.h"
#include "chrome/browser/ash/input_method/assistive_window_controller_delegate.h"
#include "chrome/browser/ash/input_method/candidate_window_controller.h"
#include "chrome/browser/ash/input_method/ime_service_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
// TODO(https://crbug.com/1164001): remove and use forward declaration.
#include "ui/base/ime/ash/component_extension_ime_manager.h"
#include "ui/base/ime/ash/ime_engine_handler_interface.h"
// TODO(https://crbug.com/1164001): remove and use forward declaration.
#include "ui/base/ime/ash/ime_keyboard.h"
// TODO(https://crbug.com/1164001): remove and use forward declaration.
#include "ui/base/ime/ash/input_method_delegate.h"
#include "ui/base/ime/ash/input_method_manager.h"
#include "ui/base/ime/ash/input_method_util.h"

namespace ui {
class IMEEngineHandlerInterface;
}  // namespace ui

namespace ash {
namespace input_method {

// The implementation of InputMethodManager.
class InputMethodManagerImpl : public InputMethodManager,
                               public CandidateWindowController::Observer,
                               public AssistiveWindowControllerDelegate,
                               public content::NotificationObserver {
 public:
  class StateImpl : public InputMethodManager::State {
   public:
    StateImpl(InputMethodManagerImpl* manager,
              Profile* profile,
              const InputMethodDescriptor* initial_input_method = nullptr);

    Profile* GetProfile() const;

    // Returns true if |input_method_id| is in |enabled_input_method_ids_|.
    bool InputMethodIsEnabled(const std::string& input_method_id) const;

    // TODO(nona): Support dynamical unloading.
    void LoadNecessaryComponentExtensions();

    void SetMenuActivated(bool activated);

    bool IsMenuActivated() const;

    // Override the input view URL used to explicitly display some keyset.
    void OverrideInputViewUrl(const GURL& url);

    // Reset the input view URL to the default url of the current input method.
    void ResetInputViewUrl();

    // InputMethodManager::State overrides.
    scoped_refptr<InputMethodManager::State> Clone() const override;
    void AddInputMethodExtension(
        const std::string& extension_id,
        const InputMethodDescriptors& descriptors,
        ui::IMEEngineHandlerInterface* instance) override;
    void RemoveInputMethodExtension(const std::string& extension_id) override;
    void ChangeInputMethod(const std::string& input_method_id,
                           bool show_message) override;
    void ChangeInputMethodToJpKeyboard() override;
    void ChangeInputMethodToJpIme() override;
    void ToggleInputMethodForJpIme() override;
    bool EnableInputMethod(
        const std::string& new_enabled_input_method_id) override;
    void EnableLoginLayouts(
        const std::string& language_code,
        const std::vector<std::string>& initial_layouts) override;
    void EnableLockScreenLayouts() override;
    void GetInputMethodExtensions(InputMethodDescriptors* result) override;
    std::unique_ptr<InputMethodDescriptors>
    GetEnabledInputMethodsSortedByLocalizedDisplayNames() const override;
    std::unique_ptr<InputMethodDescriptors> GetEnabledInputMethods()
        const override;
    const std::vector<std::string>& GetEnabledInputMethodIds() const override;
    const InputMethodDescriptor* GetInputMethodFromId(
        const std::string& input_method_id) const override;
    size_t GetNumEnabledInputMethods() const override;
    void SetEnabledExtensionImes(std::vector<std::string>* ids) override;
    void SetInputMethodLoginDefault() override;
    void SetInputMethodLoginDefaultFromVPD(const std::string& locale,
                                           const std::string& layout) override;
    void SwitchToNextInputMethod() override;
    void SwitchToLastUsedInputMethod() override;
    InputMethodDescriptor GetCurrentInputMethod() const override;
    bool ReplaceEnabledInputMethods(
        const std::vector<std::string>& new_enabled_input_method_ids) override;
    bool SetAllowedInputMethods(
        const std::vector<std::string>& new_allowed_input_method_ids,
        bool enable_allowed_input_methods) override;
    const std::vector<std::string>& GetAllowedInputMethodIds() const override;
    void EnableInputView() override;
    void DisableInputView() override;
    const GURL& GetInputViewUrl() const override;
    InputMethodManager::UIStyle GetUIStyle() const override;
    void SetUIStyle(InputMethodManager::UIStyle ui_style) override;

   protected:
    friend base::RefCounted<input_method::InputMethodManager::State>;
    ~StateImpl() override;

   private:
    // Returns true if (manager_->state_ == this).
    bool IsActive() const;

    // Adds new input method to given list if possible
    bool EnableInputMethodImpl(
        const std::string& input_method_id,
        std::vector<std::string>* new_enabled_input_method_ids) const;

    // Returns true if the passed input method is allowed. By default, all input
    // methods are allowed. After SetAllowedKeyboardLayoutInputMethods was
    // called, the passed keyboard layout input methods are allowed and all
    // non-keyboard input methods remain to be allowed.
    bool IsInputMethodAllowed(const std::string& input_method_id) const;

    // Returns the first hardware input method that is allowed or the first
    // allowed input method, if no hardware input method is allowed.
    std::string GetAllowedFallBackKeyboardLayout() const;

    // Returns Input Method that best matches given id.
    const InputMethodDescriptor* LookupInputMethod(
        const std::string& input_method_id);

    Profile* const profile_;

    InputMethodManagerImpl* const manager_;

    std::string last_used_input_method_id_;

    InputMethodDescriptor current_input_method_;

    std::vector<std::string> enabled_input_method_ids_;

    // All input methods that have been registered by InputMethodEngines.
    // The key is the input method ID.
    std::map<std::string, InputMethodDescriptor> available_input_methods_;

    // The allowed keyboard layout input methods (e.g. by policy).
    std::vector<std::string> allowed_keyboard_layout_input_method_ids_;

    // The pending input method id for delayed 3rd party IME enabling.
    std::string pending_input_method_id_;

    std::vector<std::string> enabled_extension_imes_;

    // The URL of the input view of the current (active) ime with parameters
    // (e.g. layout, keyset).
    GURL input_view_url_;

    // Whether the input view URL has been forcibly overridden e.g. to show a
    // specific keyset.
    bool input_view_url_overridden_ = false;

    InputMethodManager::UIStyle ui_style_ =
        InputMethodManager::UIStyle::kNormal;

    // True if the opt-in IME menu is activated.
    bool menu_activated_ = false;

    // Do not forget to update StateImpl::Clone() when adding new data members!!
  };

  // Constructs an InputMethodManager instance. The client is responsible for
  // calling |SetUISessionState| in response to relevant changes in browser
  // state.
  InputMethodManagerImpl(std::unique_ptr<InputMethodDelegate> delegate,
                         std::unique_ptr<ComponentExtensionIMEManagerDelegate>
                             component_extension_ime_manager_delegate,
                         bool enable_extension_loading);

  InputMethodManagerImpl(const InputMethodManagerImpl&) = delete;
  InputMethodManagerImpl& operator=(const InputMethodManagerImpl&) = delete;

  ~InputMethodManagerImpl() override;

  // Sets |candidate_window_controller_|.
  void SetCandidateWindowControllerForTesting(
      CandidateWindowController* candidate_window_controller);
  // Sets |keyboard_|.
  void SetImeKeyboardForTesting(ImeKeyboard* keyboard);

  // InputMethodManager override:
  void AddObserver(InputMethodManager::Observer* observer) override;
  void AddCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) override;
  void AddImeMenuObserver(
      InputMethodManager::ImeMenuObserver* observer) override;
  void RemoveObserver(InputMethodManager::Observer* observer) override;
  void RemoveCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) override;
  void RemoveImeMenuObserver(
      InputMethodManager::ImeMenuObserver* observer) override;
  void ActivateInputMethodMenuItem(const std::string& key) override;
  void ConnectInputEngineManager(
      mojo::PendingReceiver<chromeos::ime::mojom::InputEngineManager> receiver)
      override;
  bool IsISOLevel5ShiftUsedByCurrentInputMethod() const override;
  bool IsAltGrUsedByCurrentInputMethod() const override;
  bool ArePositionalShortcutsUsedByCurrentInputMethod() const override;
  void NotifyImeMenuItemsChanged(
      const std::string& engine_id,
      const std::vector<InputMethodManager::MenuItem>& items) override;
  void MaybeNotifyImeMenuActivationChanged() override;
  void OverrideKeyboardKeyset(ImeKeyset keyset) override;
  void SetImeMenuFeatureEnabled(ImeMenuFeature feature, bool enabled) override;
  bool GetImeMenuFeatureEnabled(ImeMenuFeature feature) const override;
  void NotifyObserversImeExtraInputStateChange() override;
  ui::VirtualKeyboardController* GetVirtualKeyboardController() override;
  void NotifyInputMethodExtensionAdded(
      const std::string& extension_id) override;
  void NotifyInputMethodExtensionRemoved(
      const std::string& extension_id) override;
  ImeKeyboard* GetImeKeyboard() override;
  InputMethodUtil* GetInputMethodUtil() override;
  ComponentExtensionIMEManager* GetComponentExtensionIMEManager() override;
  bool IsLoginKeyboard(const std::string& layout) const override;
  bool MigrateInputMethods(std::vector<std::string>* input_method_ids) override;
  scoped_refptr<InputMethodManager::State> CreateNewState(
      Profile* profile) override;
  scoped_refptr<InputMethodManager::State> GetActiveIMEState() override;
  void SetState(scoped_refptr<InputMethodManager::State> state) override;
  void ImeMenuActivationChanged(bool is_active) override;

  // content::NotificationObserver overrides:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

 private:
  // CandidateWindowController::Observer overrides:
  void CandidateClicked(int index) override;
  void CandidateWindowOpened() override;
  void CandidateWindowClosed() override;

  // AssistiveWindowControllerDelegate overrides:
  void AssistiveWindowButtonClicked(
      const ui::ime::AssistiveWindowButton& button) const override;

  // Creates and initializes |candidate_window_controller_| if it hasn't been
  // done.
  void MaybeInitializeCandidateWindowController();
  // Creates and initializes |assistive_window_controller_| if it hasn't been
  // done.
  void MaybeInitializeAssistiveWindowController();

  // Change system input method to the one specified in the active state.
  void ChangeInputMethodInternalFromActiveState(bool show_message,
                                                bool notify_menu);

  // Starts or stops the system input method framework as needed.
  // (after list of enabled input methods has been updated).
  // If state is active, current (active) input method is updated.
  void ReconfigureIMFramework(StateImpl* state);

  // Record input method usage histograms.
  void RecordInputMethodUsage(const std::string& input_method_id);

  // Notifies the current input method or the list of enabled input method IDs
  // changed.
  void NotifyImeMenuListChanged();

  // Request that the virtual keyboard be reloaded.
  void ReloadKeyboard();

  std::unique_ptr<InputMethodDelegate> delegate_;

  // A list of objects that monitor the manager.
  base::ObserverList<InputMethodManager::Observer>::Unchecked observers_;
  base::ObserverList<CandidateWindowObserver>::Unchecked
      candidate_window_observers_;
  base::ObserverList<ImeMenuObserver>::Unchecked ime_menu_observers_;

  scoped_refptr<StateImpl> state_;

  // The candidate window.  This will be deleted when the APP_TERMINATING
  // message is sent.
  std::unique_ptr<CandidateWindowController> candidate_window_controller_;
  // The assistive window.  This will be deleted when the APP_TERMINATING
  // message is sent.
  std::unique_ptr<AssistiveWindowController> assistive_window_controller_;

  // An object which provides miscellaneous input method utility functions. Note
  // that |util_| is required to initialize |keyboard_|.
  InputMethodUtil util_;

  // An object which provides component extension ime management functions.
  std::unique_ptr<ComponentExtensionIMEManager>
      component_extension_ime_manager_;

  // An object for switching XKB layouts and keyboard status like caps lock and
  // auto-repeat interval.
  std::unique_ptr<ImeKeyboard> keyboard_;

  // The set of layouts that do not use positional shortcuts.
  base::flat_set<std::string> non_positional_layouts_;

  // Whether load IME extensions.
  bool enable_extension_loading_;

  // Whether the expanded IME menu is activated.
  bool is_ime_menu_activated_ = false;

  // The enabled state of keyboard features.
  uint32_t features_enabled_state_;

  // The engine map from extension_id to an engine.
  using EngineMap = std::map<std::string, ui::IMEEngineHandlerInterface*>;
  using ProfileEngineMap = std::map<Profile*, EngineMap, ProfileCompare>;
  ProfileEngineMap engine_map_;

  // Map a profile to the IME service connector.
  typedef std::
      map<Profile*, std::unique_ptr<ImeServiceConnector>, ProfileCompare>
          ImeServiceConnectorMap;
  ImeServiceConnectorMap ime_service_connectors_;

  content::NotificationRegistrar notification_registrar_;
};

}  // namespace input_method
}  // namespace ash

// TODO(https://crbug.com/1164001): remove when ChromeOS code migration is done.
namespace chromeos {
namespace input_method {
using ::ash::input_method::InputMethodManagerImpl;
}  // namespace input_method
}  // namespace chromeos

#endif  // CHROME_BROWSER_ASH_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
