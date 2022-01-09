// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/print_backend.h"

#include "base/memory/ref_counted.h"
#include "base/notreached.h"
#include "base/values.h"
#include "printing/mojom/print.mojom.h"

#if defined(USE_CUPS)
#include "printing/backend/cups_ipp_utils.h"
#include "printing/backend/print_backend_cups_ipp.h"
#endif  // defined(USE_CUPS)

namespace printing {

// Provides either a stubbed out PrintBackend implementation or a CUPS IPP
// implementation for use on ChromeOS.
class PrintBackendChromeOS : public PrintBackend {
 public:
  explicit PrintBackendChromeOS(const std::string& locale);

  // PrintBackend implementation.
  mojom::ResultCode EnumeratePrinters(PrinterList* printer_list) override;
  mojom::ResultCode GetDefaultPrinterName(
      std::string& default_printer) override;
  mojom::ResultCode GetPrinterBasicInfo(
      const std::string& printer_name,
      PrinterBasicInfo* printer_info) override;
  mojom::ResultCode GetPrinterCapsAndDefaults(
      const std::string& printer_name,
      PrinterCapsAndDefaults* printer_info) override;
  mojom::ResultCode GetPrinterSemanticCapsAndDefaults(
      const std::string& printer_name,
      PrinterSemanticCapsAndDefaults* printer_info) override;
  std::string GetPrinterDriverInfo(const std::string& printer_name) override;
  bool IsValidPrinter(const std::string& printer_name) override;

 protected:
  ~PrintBackendChromeOS() override = default;
};

PrintBackendChromeOS::PrintBackendChromeOS(const std::string& locale)
    : PrintBackend(locale) {}

mojom::ResultCode PrintBackendChromeOS::EnumeratePrinters(
    PrinterList* printer_list) {
  return mojom::ResultCode::kSuccess;
}

mojom::ResultCode PrintBackendChromeOS::GetPrinterBasicInfo(
    const std::string& printer_name,
    PrinterBasicInfo* printer_info) {
  return mojom::ResultCode::kFailed;
}

mojom::ResultCode PrintBackendChromeOS::GetPrinterCapsAndDefaults(
    const std::string& printer_name,
    PrinterCapsAndDefaults* printer_info) {
  NOTREACHED();
  return mojom::ResultCode::kFailed;
}

mojom::ResultCode PrintBackendChromeOS::GetPrinterSemanticCapsAndDefaults(
    const std::string& printer_name,
    PrinterSemanticCapsAndDefaults* printer_info) {
  NOTREACHED();
  return mojom::ResultCode::kFailed;
}

std::string PrintBackendChromeOS::GetPrinterDriverInfo(
    const std::string& printer_name) {
  NOTREACHED();
  return std::string();
}

mojom::ResultCode PrintBackendChromeOS::GetDefaultPrinterName(
    std::string& default_printer) {
  default_printer = std::string();
  return mojom::ResultCode::kSuccess;
}

bool PrintBackendChromeOS::IsValidPrinter(const std::string& printer_name) {
  NOTREACHED();
  return true;
}

// static
scoped_refptr<PrintBackend> PrintBackend::CreateInstanceImpl(
    const base::DictionaryValue* print_backend_settings,
    const std::string& locale,
    bool /*for_cloud_print*/) {
#if defined(USE_CUPS)
  return base::MakeRefCounted<PrintBackendCupsIpp>(
      CreateConnection(print_backend_settings), locale);
#else
  return base::MakeRefCounted<PrintBackendChromeOS>(locale);
#endif  // defined(USE_CUPS)
}

}  // namespace printing
