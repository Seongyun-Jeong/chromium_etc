// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_
#define CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/api/printing/print_job_controller.h"
#include "chrome/browser/extensions/api/printing/print_job_submitter.h"
#include "chrome/common/extensions/api/printing.h"
#include "chrome/services/printing/public/mojom/pdf_flattener.mojom.h"
#include "chromeos/crosapi/mojom/local_printer.mojom.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/event_router_factory.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/gfx/native_widget_types.h"

class PrefRegistrySimple;

namespace chromeos {
class CupsWrapper;
class Printer;
}  // namespace chromeos

namespace content {
class BrowserContext;
}  // namespace content

namespace printing {
struct PrinterStatus;
}  // namespace printing

namespace extensions {

class PrintJobSubmitter;
class ExtensionRegistry;

// Handles chrome.printing API functions calls, observes NotificationService,
// and generates OnJobStatusChanged() events of chrome.printing API.
// The callback function is never run directly - it is posted to
// base::SequencedTaskRunnerHandle::Get().
class PrintingAPIHandler : public BrowserContextKeyedAPI,
                           public crosapi::mojom::PrintJobObserver,
                           public content::NotificationObserver {
 public:
  using SubmitJobCallback = base::OnceCallback<void(
      absl::optional<api::printing::SubmitJobStatus> status,
      std::unique_ptr<std::string> job_id,
      absl::optional<std::string> error)>;
  using GetPrintersCallback =
      base::OnceCallback<void(std::vector<api::printing::Printer>)>;
  using GetPrinterInfoCallback = base::OnceCallback<void(
      absl::optional<base::Value> capabilities,
      absl::optional<api::printing::PrinterStatus> status,
      absl::optional<std::string> error)>;

  static std::unique_ptr<PrintingAPIHandler> CreateForTesting(
      content::BrowserContext* browser_context,
      EventRouter* event_router,
      ExtensionRegistry* extension_registry,
      std::unique_ptr<PrintJobController> print_job_controller,
      std::unique_ptr<chromeos::CupsWrapper> cups_wrapper,
      crosapi::mojom::LocalPrinter* local_printer);

  explicit PrintingAPIHandler(content::BrowserContext* browser_context);
  PrintingAPIHandler(content::BrowserContext* browser_context,
                     EventRouter* event_router,
                     ExtensionRegistry* extension_registry,
                     std::unique_ptr<PrintJobController> print_job_controller,
                     std::unique_ptr<chromeos::CupsWrapper> cups_wrapper,
                     crosapi::mojom::LocalPrinter* local_printer = nullptr);
  PrintingAPIHandler(const PrintingAPIHandler&) = delete;
  PrintingAPIHandler& operator=(const PrintingAPIHandler&) = delete;
  ~PrintingAPIHandler() override;

  static std::string CreateUniqueId(const std::string& printer_id, int job_id);

  // NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;
  // BrowserContextKeyedAPI:
  static BrowserContextKeyedAPIFactory<PrintingAPIHandler>*
  GetFactoryInstance();

  // Returns the current instance for |browser_context|.
  static PrintingAPIHandler* Get(content::BrowserContext* browser_context);

  // Print jobs should be registered before OnPrintJobUpdate() is called.
  void RegisterPrintJob(const std::string& printer_id,
                        int job_id,
                        const std::string& extension_id);

  // crosapi::mojom::PrintJobObserver:
  void OnPrintJobUpdate(const std::string& printer_id,
                        unsigned int job_id,
                        crosapi::mojom::PrintJobStatus status) override;

  // Register the printing API preference with the |registry|.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Submits the job to printing pipeline.
  // If |extension| is not present among PrintingAPIExtensionsAllowlist
  // extensions, special print job request dialog is shown to the user to ask
  // for their confirmation.
  // |native_window| is needed to show this dialog.
  void SubmitJob(gfx::NativeWindow native_window,
                 scoped_refptr<const extensions::Extension> extension,
                 std::unique_ptr<api::printing::SubmitJob::Params> params,
                 PrintJobSubmitter::SubmitJobCallback callback);

  // Returns an error message if an error occurred.
  absl::optional<std::string> CancelJob(const std::string& extension_id,
                                        const std::string& job_id);

  void GetPrinters(GetPrintersCallback callback);

  void GetPrinterInfo(const std::string& printer_id,
                      GetPrinterInfoCallback callback);

  void SetPrintJobControllerForTesting(
      std::unique_ptr<PrintJobController> print_job_controller);

 private:
  // Needed for BrowserContextKeyedAPI implementation.
  friend class BrowserContextKeyedAPIFactory<PrintingAPIHandler>;

  struct PrintJobInfo {
    std::string printer_id;
    int job_id;
    std::string extension_id;
  };

  // This is needed to save ownership of |print_job_submitter| object which
  // could be destructed because of asynchronous work otherwise.
  void OnPrintJobSubmitted(
      std::unique_ptr<PrintJobSubmitter> print_job_submitter,
      PrintJobSubmitter::SubmitJobCallback callback,
      absl::optional<api::printing::SubmitJobStatus> status,
      std::unique_ptr<std::string> job_id,
      absl::optional<std::string> error);

  void OnPrintersRetrieved(
      GetPrintersCallback callback,
      std::vector<crosapi::mojom::LocalDestinationInfoPtr> data);

  // GetPrinterInfo() calls this function.
  void OnPrinterCapabilitiesRetrieved(
      const std::string& printer_id,
      GetPrinterInfoCallback callback,
      crosapi::mojom::CapabilitiesResponsePtr caps);

  // OnPrinterCapabilitiesRetrieved() calls this function.
  void OnPrinterStatusRetrieved(
      GetPrinterInfoCallback callback,
      base::Value capabilities,
      std::unique_ptr<::printing::PrinterStatus> printer_status);

  // BrowserContextKeyedAPI:
  static const bool kServiceIsNULLWhileTesting = true;
  static const char* service_name() { return "PrintingAPIHandler"; }

  content::BrowserContext* const browser_context_;
  EventRouter* const event_router_;
  ExtensionRegistry* const extension_registry_;
  std::unique_ptr<PrintJobController> print_job_controller_;
  std::unique_ptr<chromeos::CupsWrapper> cups_wrapper_;
  content::NotificationRegistrar registrar_;

  // Remote interface used to flatten a PDF.
  mojo::Remote<printing::mojom::PdfFlattener> pdf_flattener_;

  // Stores mapping from job id to PrintJobInfo object.
  // This is needed to cancel print jobs.
  base::flat_map<std::string, PrintJobInfo> print_jobs_;

  crosapi::mojom::LocalPrinter* local_printer_;
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  int local_printer_version_ = 0;
#endif

  mojo::Receiver<crosapi::mojom::PrintJobObserver> receiver_{this};

  base::WeakPtrFactory<PrintingAPIHandler> weak_ptr_factory_{this};
};

template <>
struct BrowserContextFactoryDependencies<PrintingAPIHandler> {
  static void DeclareFactoryDependencies(
      BrowserContextKeyedAPIFactory<PrintingAPIHandler>* factory) {
    factory->DependsOn(EventRouterFactory::GetInstance());
  }
};

template <>
KeyedService*
BrowserContextKeyedAPIFactory<PrintingAPIHandler>::BuildServiceInstanceFor(
    content::BrowserContext* context) const;

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_
