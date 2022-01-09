// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_root_view.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/metrics/user_metrics.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/touch_uma/touch_uma.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/webplugininfo.h"
#include "net/base/filename_util.h"
#include "net/base/mime_util.h"
#include "ppapi/buildflags/buildflags.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view.h"

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/public/browser/plugin_service.h"
#endif

namespace {

using ::ui::mojom::DragOperation;

using FileSupportedCallback =
    base::OnceCallback<void(const GURL& url, bool supported)>;

// Get the MIME type of the file pointed to by the url, based on the file's
// extension. Must be called in a context that allows blocking.
std::string FindURLMimeType(const GURL& url) {
  base::FilePath full_path;
  net::FileURLToFilePath(url, &full_path);

  // Get the MIME type based on the filename.
  std::string mime_type;
  // This call may block on some platforms.
  net::GetMimeTypeFromFile(full_path, &mime_type);

  return mime_type;
}

void OnFindURLMimeType(const GURL& url,
                       int process_id,
                       FileSupportedCallback callback,
                       const std::string& mime_type) {
  // Check whether the mime type, if given, is known to be supported or whether
  // there is a plugin that supports the mime type (e.g. PDF).
  // TODO(bauerb): This possibly uses stale information, but it's guaranteed not
  // to do disk access.
  bool result = mime_type.empty() || blink::IsSupportedMimeType(mime_type);

#if BUILDFLAG(ENABLE_PLUGINS)
  content::WebPluginInfo plugin;
  result = result ||
           content::PluginService::GetInstance()->GetPluginInfo(
               process_id, url, mime_type, false, nullptr, &plugin, nullptr);
#endif

  std::move(callback).Run(url, result);
}

bool GetURLForDrop(const ui::DropTargetEvent& event, GURL* url) {
  DCHECK(url);
  std::u16string title;
  return event.data().GetURLAndTitle(ui::FilenameToURLPolicy::CONVERT_FILENAMES,
                                     url, &title) &&
         url->is_valid();
}

DragOperation GetDropEffect(const ui::DropTargetEvent& event, const GURL& url) {
  const int source_ops = event.source_operations();
  if (source_ops & ui::DragDropTypes::DRAG_COPY)
    return DragOperation::kCopy;
  if (source_ops & ui::DragDropTypes::DRAG_LINK)
    return DragOperation::kLink;
  return DragOperation::kMove;
}

}  // namespace

BrowserRootView::DropInfo::DropInfo() = default;

BrowserRootView::DropInfo::~DropInfo() {
  if (target)
    target->HandleDragExited();
}

BrowserRootView::BrowserRootView(BrowserView* browser_view,
                                 views::Widget* widget)
    : views::internal::RootView(widget), browser_view_(browser_view) {}

BrowserRootView::~BrowserRootView() {
  // It's possible to destroy the browser while a drop is active.  In this case,
  // |drop_info_| will be non-null, but its |target| likely points to an
  // already-deleted child.  Clear the target so ~DropInfo() will not try and
  // notify it of the drag ending. http://crbug.com/1001942
  if (drop_info_)
    drop_info_->target = nullptr;
}

bool BrowserRootView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  if (tabstrip()->GetVisible() || toolbar()->GetVisible()) {
    *formats = ui::OSExchangeData::URL | ui::OSExchangeData::STRING;
    return true;
  }
  return false;
}

bool BrowserRootView::AreDropTypesRequired() {
  return true;
}

bool BrowserRootView::CanDrop(const ui::OSExchangeData& data) {
  // If it's not tabbed browser, we don't have to support drag and drops.
  if (!browser_view_->GetIsNormalType())
    return false;

  if (!tabstrip()->GetVisible() && !toolbar()->GetVisible())
    return false;

  // If there is a URL, we'll allow the drop.
  if (data.HasURL(ui::FilenameToURLPolicy::CONVERT_FILENAMES))
    return true;

  // If there isn't a URL, see if we can 'paste and go'.
  return GetPasteAndGoURL(data, nullptr);
}

void BrowserRootView::OnDragEntered(const ui::DropTargetEvent& event) {
  drop_info_ = std::make_unique<DropInfo>();
  GURL url;
  if (GetURLForDrop(event, &url)) {
    drop_info_->url = url;

    // Check if the file is supported.
    if (url.SchemeIsFile()) {
      // Avoid crashing while the tab strip is being initialized or is empty.
      content::WebContents* web_contents =
          browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
      if (!web_contents) {
        return;
      }

      content::RenderFrameHost* rfh = web_contents->GetMainFrame();
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(&FindURLMimeType, url),
          base::BindOnce(&OnFindURLMimeType, url, rfh->GetProcess()->GetID(),
                         base::BindOnce(&BrowserRootView::OnFileSupported,
                                        weak_ptr_factory_.GetWeakPtr())));
    }
  }
}

int BrowserRootView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!drop_info_)
    OnDragEntered(event);

  if (auto* drop_target = GetDropTarget(event)) {
    if (drop_info_->target && drop_info_->target != drop_target)
      drop_info_->target->HandleDragExited();
    drop_info_->target = drop_target;

    if (!drop_info_->file_supported ||
        drop_info_->url.SchemeIs(url::kJavaScriptScheme)) {
      drop_info_->index.reset();
    } else {
      drop_info_->index =
          GetDropIndexForEvent(event, event.data(), drop_target);
    }

    drop_target->HandleDragUpdate(drop_info_->index);
    return drop_info_->index
               ? static_cast<int>(GetDropEffect(event, drop_info_->url))
               : ui::DragDropTypes::DRAG_NONE;
  }

  OnDragExited();
  return ui::DragDropTypes::DRAG_NONE;
}

void BrowserRootView::OnDragExited() {
  drop_info_.reset();
}

DragOperation BrowserRootView::OnPerformDrop(const ui::DropTargetEvent& event) {
  using base::UserMetricsAction;

  if (!drop_info_)
    return DragOperation::kNone;

  auto cb = GetDropCallback(event);
  ui::mojom::DragOperation output_drag_op = ui::mojom::DragOperation::kNone;
  std::move(cb).Run(event, output_drag_op);

  return output_drag_op;
}

views::View::DropCallback BrowserRootView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  if (!drop_info_)
    return base::DoNothing();

  // Moving `drop_info_` ensures we call HandleDragExited() on |drop_info_|'s
  // |target| when this function returns.
  return base::BindOnce(&BrowserRootView::NavigateToDropUrl,
                        weak_ptr_factory_.GetWeakPtr(), std::move(drop_info_));
}

bool BrowserRootView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  // TODO(dfried): See if it's possible to move this logic deeper into the view
  // hierarchy - ideally to TabStripRegionView.

  // Scroll-event-changes-tab is incompatible with scrolling tabstrip, so
  // disable it if the latter feature is enabled.
  if (browser_defaults::kScrollEventChangesTab &&
      !base::FeatureList::IsEnabled(features::kScrollableTabStrip)) {
    // Switch to the left/right tab if the wheel-scroll happens over the
    // tabstrip, or the empty space beside the tabstrip.
    views::View* hit_view = GetEventHandlerForPoint(event.location());
    int hittest =
        GetWidget()->non_client_view()->NonClientHitTest(event.location());
    if (tabstrip()->Contains(hit_view) ||
        hittest == HTCAPTION ||
        hittest == HTTOP) {
      scroll_remainder_x_ += event.x_offset();
      scroll_remainder_y_ += event.y_offset();

      // Number of integer scroll events that have passed in each direction.
      int whole_scroll_amount_x =
          std::lround(static_cast<double>(scroll_remainder_x_) /
                      ui::MouseWheelEvent::kWheelDelta);
      int whole_scroll_amount_y =
          std::lround(static_cast<double>(scroll_remainder_y_) /
                      ui::MouseWheelEvent::kWheelDelta);

      // Adjust the remainder such that any whole scrolls we have taken action
      // for don't count towards the scroll remainder.
      scroll_remainder_x_ -=
          whole_scroll_amount_x * ui::MouseWheelEvent::kWheelDelta;
      scroll_remainder_y_ -=
          whole_scroll_amount_y * ui::MouseWheelEvent::kWheelDelta;

      // Count a scroll in either axis - summing the axes works for this.
      int whole_scroll_offset = whole_scroll_amount_x + whole_scroll_amount_y;

      Browser* browser = browser_view_->browser();
      TabStripModel* model = browser->tab_strip_model();
      // Switch to the next tab only if not at the end of the tab-strip.
      if (whole_scroll_offset < 0 &&
          model->active_index() + 1 < model->count()) {
        chrome::SelectNextTab(
            browser, {TabStripModel::GestureType::kWheel, event.time_stamp()});
        return true;
      }

      // Switch to the previous tab only if not at the beginning of the
      // tab-strip.
      if (whole_scroll_offset > 0 && model->active_index() > 0) {
        chrome::SelectPreviousTab(
            browser, {TabStripModel::GestureType::kWheel, event.time_stamp()});
        return true;
      }
    }
  }
  return RootView::OnMouseWheel(event);
}

void BrowserRootView::OnMouseExited(const ui::MouseEvent& event) {
  // Reset the remainders so tab switches occur halfway through a smooth scroll.
  scroll_remainder_x_ = 0;
  scroll_remainder_y_ = 0;
  RootView::OnMouseExited(event);
}

void BrowserRootView::PaintChildren(const views::PaintInfo& paint_info) {
  views::internal::RootView::PaintChildren(paint_info);

  // ToolbarView can't paint its own top stroke because the stroke is drawn just
  // above its bounds, where the active tab can overwrite it to visually join
  // with the toolbar.  This painting can't be done in the NonClientFrameView
  // because parts of the BrowserView (such as tabs) would get rendered on top
  // of the stroke.  It can't be done in BrowserView either because that view is
  // offset from the widget by a few DIPs, which is toublesome for computing a
  // subpixel offset when using fractional scale factors.  So we're forced to
  // put this drawing in the BrowserRootView.
  if (tabstrip()->ShouldDrawStrokes() && browser_view_->IsToolbarVisible()) {
    ui::PaintRecorder recorder(paint_info.context(),
                               paint_info.paint_recording_size(),
                               paint_info.paint_recording_scale_x(),
                               paint_info.paint_recording_scale_y(), nullptr);
    gfx::Canvas* canvas = recorder.canvas();

    const float scale = canvas->image_scale();

    gfx::RectF toolbar_bounds(browser_view_->toolbar()->bounds());
    ConvertRectToTarget(browser_view_, this, &toolbar_bounds);
    const int bottom = std::round(toolbar_bounds.y() * scale);
    const int x = std::round(toolbar_bounds.x() * scale);
    const int width = std::round(toolbar_bounds.width() * scale);

    gfx::ScopedCanvas scoped_canvas(canvas);
    int active_tab_index = tabstrip()->controller()->GetActiveIndex();
    if (active_tab_index != ui::ListSelectionModel::kUnselectedIndex) {
      Tab* active_tab = tabstrip()->tab_at(active_tab_index);
      if (active_tab && active_tab->GetVisible()) {
        gfx::RectF bounds(active_tab->GetMirroredBounds());
        ConvertRectToTarget(tabstrip(), this, &bounds);
        canvas->ClipRect(bounds, SkClipOp::kDifference);
      }
    }
    canvas->UndoDeviceScaleFactor();

    cc::PaintFlags flags;
    flags.setColor(tabstrip()->GetToolbarTopSeparatorColor());
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setAntiAlias(true);
    canvas->DrawRect(gfx::RectF(x, bottom - scale, width, scale), flags);
  }
}

void BrowserRootView::OnEventProcessingStarted(ui::Event* event) {
  if (event->IsGestureEvent()) {
    ui::GestureEvent* gesture_event = event->AsGestureEvent();
    if (gesture_event->type() == ui::ET_GESTURE_TAP &&
        gesture_event->location().y() <= 0 &&
        gesture_event->location().x() <= browser_view_->GetBounds().width()) {
      TouchUMA::RecordGestureAction(TouchUMA::kGestureRootViewTopTap);
    }
  }

  RootView::OnEventProcessingStarted(event);
}

BrowserRootView::DropTarget* BrowserRootView::GetDropTarget(
    const ui::DropTargetEvent& event) {
  // See if we should drop links onto tabstrip first.
  if (tabstrip()->GetVisible()) {
    // Allow the drop as long as the mouse is over tabstrip or vertically
    // before it.
    gfx::Point tabstrip_loc_in_host;
    ConvertPointToTarget(tabstrip(), this, &tabstrip_loc_in_host);
    if (event.y() < tabstrip_loc_in_host.y() + tabstrip()->height())
      return tabstrip();
  }

  // See if we can drop links onto toolbar.
  gfx::Point loc_in_toolbar(event.location());
  ConvertPointToTarget(this, toolbar(), &loc_in_toolbar);
  return toolbar()->HitTestPoint(loc_in_toolbar) ? toolbar() : nullptr;
}

BrowserRootView::DropIndex BrowserRootView::GetDropIndexForEvent(
    const ui::DropTargetEvent& event,
    const ui::OSExchangeData& data,
    DropTarget* target) {
  gfx::Point loc_in_view(event.location());
  ConvertPointToTarget(this, target->GetViewForDrop(), &loc_in_view);
  ui::DropTargetEvent event_in_view(data, gfx::PointF(loc_in_view),
                                    gfx::PointF(loc_in_view),
                                    event.source_operations());
  return target->GetDropIndex(event_in_view);
}

void BrowserRootView::OnFileSupported(const GURL& url, bool supported) {
  if (drop_info_ && drop_info_->url == url)
    drop_info_->file_supported = supported;
}

bool BrowserRootView::GetPasteAndGoURL(const ui::OSExchangeData& data,
                                       GURL* url) {
  if (!data.HasString())
    return false;

  std::u16string text;
  if (!data.GetString(&text) || text.empty())
    return false;
  text = AutocompleteMatch::SanitizeString(text);

  AutocompleteMatch match;
  AutocompleteClassifierFactory::GetForProfile(
      browser_view_->browser()->profile())->Classify(
          text, false, false, metrics::OmniboxEventProto::INVALID_SPEC, &match,
          nullptr);
  if (!match.destination_url.is_valid())
    return false;

  if (url)
    *url = match.destination_url;
  return true;
}

void BrowserRootView::NavigateToDropUrl(
    std::unique_ptr<DropInfo> drop_info,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op) {
  DCHECK(drop_info);

  Browser* const browser = browser_view_->browser();
  TabStripModel* const model = browser->tab_strip_model();

  // If the browser window is not visible, it's about to be destroyed.
  if (!browser->window()->IsVisible() || model->empty())
    return;

  if (drop_info->index->value > model->GetTabCount())
    return;

  // Extract the URL and create a new ui::OSExchangeData containing the URL. We
  // do this as the TabStrip doesn't know about the autocomplete edit and needs
  // to know about it to handle 'paste and go'.
  GURL url;
  if (!GetURLForDrop(event, &url)) {
    // The url isn't valid. Use the paste and go url.
    GetPasteAndGoURL(event.data(), &url);
  }

  // Do nothing if the file was unsupported, the URL is invalid, or this is a
  // javascript: URL (prevent self-xss). The URL may have been changed after
  // |drop_info| was created.
  if (!drop_info->file_supported || !url.is_valid() ||
      url.SchemeIs(url::kJavaScriptScheme)) {
    output_drag_op = DragOperation::kNone;
    return;
  }

  NavigateParams params(browser_view_->browser(), url,
                        ui::PAGE_TRANSITION_LINK);
  params.tabstrip_index = drop_info->index->value;
  if (drop_info->index->drop_before) {
    base::RecordAction(base::UserMetricsAction("Tab_DropURLBetweenTabs"));
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    if (drop_info->index->drop_in_group &&
        drop_info->index->value < model->count())
      params.group = model->GetTabGroupForTab(drop_info->index->value);
  } else {
    base::RecordAction(base::UserMetricsAction("Tab_DropURLOnTab"));
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    params.source_contents = model->GetWebContentsAt(drop_info->index->value);
  }

  params.window_action = NavigateParams::SHOW_WINDOW;
  Navigate(&params);

  output_drag_op = GetDropEffect(event, url);
}

BEGIN_METADATA(BrowserRootView, views::internal::RootView)
END_METADATA
