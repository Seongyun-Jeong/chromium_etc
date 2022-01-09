// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_SHILL_SHILL_CLIENT_HELPER_H_
#define CHROMEOS_DBUS_SHILL_SHILL_CLIENT_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "chromeos/dbus/shill/shill_property_changed_observer.h"

namespace base {
class ListValue;
class Value;
}  // namespace base

namespace dbus {

class MessageWriter;
class MethodCall;
class ObjectProxy;
class Signal;

}  // namespace dbus

namespace chromeos {

// A class to help implement Shill clients.
class ShillClientHelper {
 public:
  class RefHolder;

  // A callback to handle responses of methods returning a ListValue.
  using ListValueCallback =
      base::OnceCallback<void(const base::ListValue& result)>;

  // A callback to handle errors for method call.
  using ErrorCallback =
      base::OnceCallback<void(const std::string& error_name,
                              const std::string& error_message)>;

  // A callback that handles responses for methods with string results.
  using StringCallback = base::OnceCallback<void(const std::string& result)>;

  // A callback that handles responses for methods with boolean results.
  using BooleanCallback = base::OnceCallback<void(bool result)>;

  // Callback used to notify owner when this can be safely released.
  using ReleasedCallback = base::OnceCallback<void(ShillClientHelper* helper)>;

  explicit ShillClientHelper(dbus::ObjectProxy* proxy);

  ShillClientHelper(const ShillClientHelper&) = delete;
  ShillClientHelper& operator=(const ShillClientHelper&) = delete;

  virtual ~ShillClientHelper();

  // Sets |released_callback_|. This is optional and should only be called at
  // most once.
  void SetReleasedCallback(ReleasedCallback callback);

  // Adds an |observer| of the PropertyChanged signal.
  void AddPropertyChangedObserver(ShillPropertyChangedObserver* observer);

  // Removes an |observer| of the PropertyChanged signal.
  void RemovePropertyChangedObserver(ShillPropertyChangedObserver* observer);

  // Starts monitoring PropertyChanged signal. If there aren't observers for the
  // PropertyChanged signal, the actual monitoring will be delayed until the
  // first observer is added.
  void MonitorPropertyChanged(const std::string& interface_name);

  // Calls a method without results.
  void CallVoidMethod(dbus::MethodCall* method_call,
                      VoidDBusMethodCallback callback);

  // Calls a method with an object path result where there is an error callback.
  void CallObjectPathMethodWithErrorCallback(dbus::MethodCall* method_call,
                                             ObjectPathCallback callback,
                                             ErrorCallback error_callback);

  // Calls a method with a value result.
  void CallValueMethod(dbus::MethodCall* method_call,
                       DBusMethodCallback<base::Value> callback);

  // Calls a method without results with error callback.
  void CallVoidMethodWithErrorCallback(dbus::MethodCall* method_call,
                                       base::OnceClosure callback,
                                       ErrorCallback error_callback);

  // Calls a method with a boolean result with error callback.
  void CallBooleanMethodWithErrorCallback(dbus::MethodCall* method_call,
                                          BooleanCallback callback,
                                          ErrorCallback error_callback);

  // Calls a method with a string result with error callback.
  void CallStringMethodWithErrorCallback(dbus::MethodCall* method_call,
                                         StringCallback callback,
                                         ErrorCallback error_callback);

  // Calls a method with a dictionary value result with error callback.
  void CallValueMethodWithErrorCallback(
      dbus::MethodCall* method_call,
      base::OnceCallback<void(base::Value result)> callback,
      ErrorCallback error_callback);

  // Calls a method with a boolean array result with error callback.
  void CallListValueMethodWithErrorCallback(dbus::MethodCall* method_call,
                                            ListValueCallback callback,
                                            ErrorCallback error_callback);

  const dbus::ObjectProxy* object_proxy() const { return proxy_; }

  // Appends the value to the writer as a variant. If |value| is a dictionary it
  // will be written as a string -> variant dictionary, a{sv}. If |value| is a
  // List then it must be a List of String values and is written as type 'as'.
  static void AppendValueDataAsVariant(dbus::MessageWriter* writer,
                                       const base::Value& value);

  // Appends a string-to-variant dictionary to the writer as an '{sv}' array.
  // Each value is written using AppendValueDataAsVariant.
  static void AppendServiceProperties(dbus::MessageWriter* writer,
                                      const base::Value& dictionary);

  // Helper method to check for a dictionary result in GetProperties calls.
  static void OnGetProperties(const dbus::ObjectPath& device_path,
                              DBusMethodCallback<base::Value> callback,
                              absl::optional<base::Value> result);

 protected:
  // Reference / Ownership management. If the number of active refs (observers
  // + in-progress method calls) becomes 0, |released_callback_| (if set) will
  // be called.
  void AddRef();
  void Release();

 private:
  // Starts monitoring PropertyChanged signal.
  void MonitorPropertyChangedInternal(const std::string& interface_name);

  // Handles the result of signal connection setup.
  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool success);

  // Handles PropertyChanged signal.
  void OnPropertyChanged(dbus::Signal* signal);

  dbus::ObjectProxy* proxy_;
  ReleasedCallback released_callback_;
  int active_refs_;
  base::ObserverList<ShillPropertyChangedObserver,
                     true /* check_empty */>::Unchecked observer_list_;
  std::vector<std::string> interfaces_to_be_monitored_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<ShillClientHelper> weak_ptr_factory_{this};
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_SHILL_SHILL_CLIENT_HELPER_H_
