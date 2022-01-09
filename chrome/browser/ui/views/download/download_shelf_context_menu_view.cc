// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/download/download_shelf_context_menu_view.h"

#include "base/bind.h"
#include "base/check.h"
#include "base/i18n/rtl.h"
#include "base/metrics/histogram_functions.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_stats.h"
#include "chrome/browser/ui/views/download/download_item_view.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/page_navigator.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/menu/menu_runner.h"

DownloadShelfContextMenuView::DownloadShelfContextMenuView(
    DownloadItemView* download_item_view)
    : DownloadShelfContextMenu(download_item_view->model()->GetWeakPtr()),
      download_item_view_(download_item_view) {}

DownloadShelfContextMenuView::DownloadShelfContextMenuView(
    base::WeakPtr<DownloadUIModel> download_ui_model)
    : DownloadShelfContextMenu(download_ui_model) {}

DownloadShelfContextMenuView::~DownloadShelfContextMenuView() = default;

void DownloadShelfContextMenuView::Run(
    views::Widget* parent_widget,
    const gfx::Rect& rect,
    ui::MenuSourceType source_type,
    base::RepeatingClosure on_menu_closed_callback) {
  using Position = views::MenuAnchorPosition;
  ui::MenuModel* menu_model = GetMenuModel();
  // Run() should not be getting called if the DownloadItem was destroyed.
  DCHECK(menu_model);

  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model,
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&DownloadShelfContextMenuView::OnMenuClosed,
                          base::Unretained(this),
                          std::move(on_menu_closed_callback)));

  // The menu's alignment is determined based on the UI layout.
  Position position;
  if (base::i18n::IsRTL())
    position = Position::kTopRight;
  else
    position = Position::kTopLeft;

  menu_runner_->RunMenuAt(parent_widget, nullptr, rect, position, source_type);
}

void DownloadShelfContextMenuView::SetOnMenuWillShowCallback(
    base::OnceClosure on_menu_will_show_callback) {
  on_menu_will_show_callback_ = std::move(on_menu_will_show_callback);
}

void DownloadShelfContextMenuView::OnMenuClosed(
    base::RepeatingClosure on_menu_closed_callback) {
  close_time_ = base::TimeTicks::Now();

  // This must be run before clearing |menu_runner_| who owns the reference.
  if (!on_menu_closed_callback.is_null())
    on_menu_closed_callback.Run();

  menu_runner_.reset();
}

void DownloadShelfContextMenuView::OnMenuWillShow(ui::SimpleMenuModel* source) {
  if (on_menu_will_show_callback_)
    std::move(on_menu_will_show_callback_).Run();
}

void DownloadShelfContextMenuView::ExecuteCommand(int command_id,
                                                  int event_flags) {
  DownloadCommands::Command command =
      static_cast<DownloadCommands::Command>(command_id);

  if (command == DownloadCommands::KEEP && download_item_view_) {
    // TODO(kerenzhu): We will need SBER in WebUI download shelf.
    // Refactor this feature out of DownloadItemView so that it can be used in
    // WebUI.
    download_item_view_->MaybeSubmitDownloadToFeedbackService(
        DownloadCommands::KEEP);
  } else {
    DownloadShelfContextMenu::ExecuteCommand(command_id, event_flags);
  }

  if (!download_commands_executed_recorded_[command_id]) {
    base::UmaHistogramEnumeration(
        "Download.ShelfContextMenuAction",
        DownloadCommandToShelfAction(command,
                                     /*clicked=*/true));
    download_commands_executed_recorded_[command_id] = true;
  }
}
