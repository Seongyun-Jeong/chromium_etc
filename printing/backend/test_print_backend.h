// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_TEST_PRINT_BACKEND_H_
#define PRINTING_BACKEND_TEST_PRINT_BACKEND_H_

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "printing/backend/print_backend.h"
#include "printing/mojom/print.mojom.h"

namespace printing {

// PrintBackend which doesn't interact with the OS and responses
// can be overridden as necessary.
class TestPrintBackend : public PrintBackend {
 public:
  TestPrintBackend();

  // PrintBackend overrides
  mojom::ResultCode EnumeratePrinters(PrinterList* printer_list) override;
  mojom::ResultCode GetDefaultPrinterName(
      std::string& default_printer) override;
  mojom::ResultCode GetPrinterBasicInfo(
      const std::string& printer_name,
      PrinterBasicInfo* printer_info) override;
  mojom::ResultCode GetPrinterSemanticCapsAndDefaults(
      const std::string& printer_name,
      PrinterSemanticCapsAndDefaults* printer_info) override;
  mojom::ResultCode GetPrinterCapsAndDefaults(
      const std::string& printer_name,
      PrinterCapsAndDefaults* printer_info) override;
  std::string GetPrinterDriverInfo(const std::string& printer_name) override;
  bool IsValidPrinter(const std::string& printer_name) override;

  // Methods for test setup:

  // Sets a default printer.  The default is the empty string.
  void SetDefaultPrinterName(const std::string& printer_name);

  // Adds a printer to satisfy IsValidPrinter(), EnumeratePrinters(),
  // GetPrinterBasicInfo(), and GetPrinterSemanticCapsAndDefaults().
  // While `caps` can be null, it will cause queries for the capabilities to
  // fail, and thus is likely not of interest for most tests.  IsValidPrinter()
  // will still show true even if `caps` is null, which provides the benefit of
  // simulating a printer that exists in the system but cannot be queried.
  // `info` can be null, which will result in queries for basic info to fail.
  // Calling EnumeratePrinters() will include the identified `printer_name`
  // even if either parameter is null.
  void AddValidPrinter(const std::string& printer_name,
                       std::unique_ptr<PrinterSemanticCapsAndDefaults> caps,
                       std::unique_ptr<PrinterBasicInfo> info);

  // Adds a printer which will cause a Mojom data validation error.
  void AddInvalidDataPrinter(const std::string& printer_name);

  // Adds a printer which will fail with an access-denied permission error for
  // calls specific to a particular `printer_name`.
  void AddAccessDeniedPrinter(const std::string& printer_name);

 protected:
  ~TestPrintBackend() override;

 private:
  void AddPrinter(const std::string& printer_name,
                  std::unique_ptr<PrinterSemanticCapsAndDefaults> caps,
                  std::unique_ptr<PrinterBasicInfo> info,
                  bool blocked_by_permissions);

  struct PrinterData {
    PrinterData(std::unique_ptr<PrinterSemanticCapsAndDefaults> caps,
                std::unique_ptr<PrinterBasicInfo> info,
                bool blocked_by_permissions);
    ~PrinterData();

    std::unique_ptr<PrinterSemanticCapsAndDefaults> caps;
    std::unique_ptr<PrinterBasicInfo> info;
    bool blocked_by_permissions = false;
  };

  std::string default_printer_name_;
  // The values in `printer_map_` will not be null.  The use of a pointer to
  // PrinterData is just a workaround for the deleted copy assignment operator
  // for the `caps` field, as that prevents the PrinterData container from
  // being copied into the map.
  base::flat_map<std::string, std::unique_ptr<PrinterData>> printer_map_;
};

}  // namespace printing

#endif  // PRINTING_BACKEND_TEST_PRINT_BACKEND_H_
