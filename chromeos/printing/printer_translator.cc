// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/printing/printer_translator.h"

#include <memory>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chromeos/printing/cups_printer_status.h"
#include "chromeos/printing/printer_configuration.h"
#include "chromeos/printing/uri.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/url_constants.h"

namespace chromeos {

namespace {

// For historical reasons, the effective_make_and_model field is just
// effective_model for policy printers.
const char kEffectiveModel[] = "effective_model";

// printer fields
const char kDisplayName[] = "display_name";
const char kDescription[] = "description";
const char kManufacturer[] = "manufacturer";
const char kModel[] = "model";
const char kUri[] = "uri";
const char kUUID[] = "uuid";
const char kPpdResource[] = "ppd_resource";
const char kAutoconf[] = "autoconf";
const char kGuid[] = "guid";

// Populates the |printer| object with corresponding fields from |value|.
// Returns false if |value| is missing a required field.
bool DictionaryToPrinter(const base::Value& value, Printer* printer) {
  // Mandatory fields
  const std::string* display_name = value.FindStringKey(kDisplayName);
  if (display_name) {
    printer->set_display_name(*display_name);
  } else {
    LOG(WARNING) << "Display name required";
    return false;
  }

  const std::string* uri = value.FindStringKey(kUri);
  if (uri) {
    std::string message;
    if (!printer->SetUri(*uri, &message)) {
      LOG(WARNING) << message;
      return false;
    }
  } else {
    LOG(WARNING) << "Uri required";
    return false;
  }

  // Optional fields
  const std::string* description = value.FindStringKey(kDescription);
  if (description)
    printer->set_description(*description);

  const std::string* manufacturer = value.FindStringKey(kManufacturer);
  const std::string* model = value.FindStringKey(kModel);

  std::string make_and_model = manufacturer ? *manufacturer : std::string();
  if (!make_and_model.empty() && model && !model->empty())
    make_and_model.append(" ");
  if (model)
    make_and_model.append(*model);
  printer->set_make_and_model(make_and_model);

  const std::string* uuid = value.FindStringKey(kUUID);
  if (uuid)
    printer->set_uuid(*uuid);

  return true;
}

// Create an empty CupsPrinterInfo dictionary value. It should be consistent
// with the fields in js side. See cups_printers_browser_proxy.js for the
// definition of CupsPrintersInfo.
base::Value CreateEmptyPrinterInfo() {
  base::Value printer_info(base::Value::Type::DICTIONARY);
  printer_info.SetBoolKey("isManaged", false);
  printer_info.SetStringKey("ppdManufacturer", "");
  printer_info.SetStringKey("ppdModel", "");
  printer_info.SetStringKey("printerAddress", "");
  printer_info.SetBoolPath("printerPpdReference.autoconf", false);
  printer_info.SetStringKey("printerDescription", "");
  printer_info.SetStringKey("printerId", "");
  printer_info.SetStringKey("printerMakeAndModel", "");
  printer_info.SetStringKey("printerName", "");
  printer_info.SetStringKey("printerPPDPath", "");
  printer_info.SetStringKey("printerProtocol", "ipp");
  printer_info.SetStringKey("printerQueue", "");
  printer_info.SetStringKey("printerStatus", "");
  return printer_info;
}

// Formats a host and port string. The |port| portion is omitted if it is
// unspecified or invalid.
std::string PrinterAddress(const Uri& uri) {
  const int port = uri.GetPort();
  if (port > -1) {
    return base::StringPrintf("%s:%d", uri.GetHostEncoded().c_str(), port);
  }
  return uri.GetHostEncoded();
}

}  // namespace

const char kPrinterId[] = "id";

std::unique_ptr<Printer> RecommendedPrinterToPrinter(const base::Value& pref) {
  std::string id;
  // Printer id comes from the id or guid field depending on the source.
  const std::string* printer_id = pref.FindStringKey(kPrinterId);
  const std::string* printer_guid = pref.FindStringKey(kGuid);
  if (printer_id) {
    id = *printer_id;
  } else if (printer_guid) {
    id = *printer_guid;
  } else {
    LOG(WARNING) << "Record id required";
    return nullptr;
  }

  auto printer = std::make_unique<Printer>(id);
  if (!DictionaryToPrinter(pref, printer.get())) {
    LOG(WARNING) << "Failed to parse policy printer.";
    return nullptr;
  }

  printer->set_source(Printer::SRC_POLICY);

  const base::Value* ppd = pref.FindDictKey(kPpdResource);
  if (ppd) {
    Printer::PpdReference* ppd_reference = printer->mutable_ppd_reference();
    const std::string* make_and_model = ppd->FindStringKey(kEffectiveModel);
    if (make_and_model)
      ppd_reference->effective_make_and_model = *make_and_model;
    absl::optional<bool> autoconf = ppd->FindBoolKey(kAutoconf);
    if (autoconf.has_value())
      ppd_reference->autoconf = *autoconf;
  }
  if (!printer->ppd_reference().autoconf &&
      printer->ppd_reference().effective_make_and_model.empty()) {
    // Either autoconf flag or make and model is mandatory.
    LOG(WARNING)
        << "Missing autoconf flag and model information for policy printer.";
    return nullptr;
  }
  if (printer->ppd_reference().autoconf &&
      !printer->ppd_reference().effective_make_and_model.empty()) {
    // PPD reference can't contain both autoconf and make and model.
    LOG(WARNING) << "Autoconf flag is set together with model information for "
                    "policy printer.";
    return nullptr;
  }

  return printer;
}

base::Value GetCupsPrinterInfo(const Printer& printer) {
  base::Value printer_info = CreateEmptyPrinterInfo();

  printer_info.SetBoolKey("isManaged",
                          printer.source() == Printer::Source::SRC_POLICY);
  printer_info.SetStringKey("printerId", printer.id());
  printer_info.SetStringKey("printerName", printer.display_name());
  printer_info.SetStringKey("printerDescription", printer.description());
  printer_info.SetStringKey("printerMakeAndModel", printer.make_and_model());
  // NOTE: This assumes the the function IsIppEverywhere() simply returns
  // |printer.ppd_reference_.autoconf|. If the implementation of
  // IsIppEverywhere() changes this will need to be changed as well.
  printer_info.SetBoolPath("printerPpdReference.autoconf",
                           printer.IsIppEverywhere());
  printer_info.SetStringKey("printerPPDPath",
                            printer.ppd_reference().user_supplied_ppd_url);
  printer_info.SetStringKey("printServerUri", printer.print_server_uri());

  if (!printer.HasUri()) {
    // Uri is invalid so we set default values.
    LOG(WARNING) << "Could not parse uri.  Defaulting values";
    printer_info.SetStringKey("printerAddress", "");
    printer_info.SetStringKey("printerQueue", "");
    printer_info.SetStringKey("printerProtocol",
                              "ipp");  // IPP is our default protocol.
    return printer_info;
  }

  if (printer.IsUsbProtocol())
    printer_info.SetStringKey("ppdManufacturer",
                              printer.usb_printer_manufacturer());
  printer_info.SetStringKey("printerProtocol", printer.uri().GetScheme());
  printer_info.SetStringKey("printerAddress", PrinterAddress(printer.uri()));
  std::string printer_queue = printer.uri().GetPathEncodedAsString();
  if (!printer_queue.empty())
    printer_queue = printer_queue.substr(1);  // removes the leading '/'
  if (!printer.uri().GetQueryEncodedAsString().empty())
    printer_queue += "?" + printer.uri().GetQueryEncodedAsString();
  printer_info.SetStringKey("printerQueue", printer_queue);

  return printer_info;
}

base::Value CreateCupsPrinterStatusDictionary(
    const CupsPrinterStatus& cups_printer_status) {
  base::Value printer_status(base::Value::Type::DICTIONARY);

  printer_status.SetKey("printerId",
                        base::Value(cups_printer_status.GetPrinterId()));
  printer_status.SetKey(
      "timestamp",
      base::Value(cups_printer_status.GetTimestamp().ToJsTimeIgnoringNull()));

  base::Value status_reasons(base::Value::Type::LIST);
  for (auto reason : cups_printer_status.GetStatusReasons()) {
    base::Value status_reason(base::Value::Type::DICTIONARY);
    status_reason.SetKey("reason",
                         base::Value(static_cast<int>(reason.GetReason())));
    status_reason.SetKey("severity",
                         base::Value(static_cast<int>(reason.GetSeverity())));
    status_reasons.Append(std::move(status_reason));
  }
  printer_status.SetKey("statusReasons", std::move(status_reasons));

  return printer_status;
}
}  // namespace chromeos
