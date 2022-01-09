// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PRINTING_SECTION_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PRINTING_SECTION_H_

#include "base/values.h"
#include "chrome/browser/chromeos/printing/cups_printers_manager.h"
#include "chrome/browser/ui/webui/settings/chromeos/os_settings_section.h"

namespace content {
class WebUIDataSource;
}  // namespace content

namespace chromeos {

class CupsPrintersManager;

namespace settings {

class SearchTagRegistry;

// Provides UI strings and search tags for Printing settings.
class PrintingSection : public OsSettingsSection,
                        public CupsPrintersManager::Observer {
 public:
  PrintingSection(Profile* profile,
                  SearchTagRegistry* search_tag_registry,
                  CupsPrintersManager* printers_manager);
  ~PrintingSection() override;

 private:
  // OsSettingsSection:
  void AddLoadTimeData(content::WebUIDataSource* html_source) override;
  void AddHandlers(content::WebUI* web_ui) override;
  int GetSectionNameMessageId() const override;
  mojom::Section GetSection() const override;
  mojom::SearchResultIcon GetSectionIcon() const override;
  std::string GetSectionPath() const override;
  bool LogMetric(mojom::Setting setting, base::Value& value) const override;
  void RegisterHierarchy(HierarchyGenerator* generator) const override;

  // CupsPrintersManager::Observer
  void OnPrintersChanged(PrinterClass printer_class,
                         const std::vector<Printer>& printers) override;

  void UpdateSavedPrintersSearchTags();

  CupsPrintersManager* printers_manager_;
};

}  // namespace settings
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_PRINTING_SECTION_H_
