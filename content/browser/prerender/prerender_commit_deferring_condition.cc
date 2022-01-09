// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/prerender/prerender_commit_deferring_condition.h"

#include "content/browser/prerender/prerender_host.h"
#include "content/browser/prerender/prerender_host_registry.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "third_party/blink/public/common/features.h"

namespace content {

namespace {

// Returns the root prerender frame tree node associated with navigation_request
// of ongoing prerender activation.
FrameTreeNode* GetRootPrerenderFrameTreeNode(int prerender_frame_tree_node_id) {
  FrameTreeNode* prerender_frame_tree_node =
      FrameTreeNode::GloballyFindByID(prerender_frame_tree_node_id);
  return prerender_frame_tree_node
             ? prerender_frame_tree_node->frame_tree()->root()
             : nullptr;
}

}  // namespace

// static
std::unique_ptr<CommitDeferringCondition>
PrerenderCommitDeferringCondition::MaybeCreate(
    NavigationRequest& navigation_request,
    NavigationType navigation_type,
    absl::optional<int> candidate_prerender_frame_tree_node_id) {
  // Don't create if this navigation is not for prerender page activation.
  if (navigation_type != NavigationType::kPrerenderedPageActivation)
    return nullptr;

  return base::WrapUnique(new PrerenderCommitDeferringCondition(
      navigation_request, candidate_prerender_frame_tree_node_id.value()));
}

PrerenderCommitDeferringCondition::~PrerenderCommitDeferringCondition() =
    default;

PrerenderCommitDeferringCondition::PrerenderCommitDeferringCondition(
    NavigationRequest& navigation_request,
    int candidate_prerender_frame_tree_node_id)
    : WebContentsObserver(navigation_request.GetWebContents()),
      candidate_prerender_frame_tree_node_id_(
          candidate_prerender_frame_tree_node_id) {
  DCHECK_NE(candidate_prerender_frame_tree_node_id_,
            RenderFrameHost::kNoFrameTreeNodeId);
}

CommitDeferringCondition::Result
PrerenderCommitDeferringCondition::WillCommitNavigation(
    base::OnceClosure resume) {
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);

  // If the prerender FrameTreeNode is gone, the prerender activation is allowed
  // to continue here but will fail soon.
  if (!prerender_frame_tree_node)
    return Result::kProceed;

  // If there is no ongoing main frame navigation in prerender frame tree, the
  // prerender activation is allowed to continue.
  if (!prerender_frame_tree_node->HasNavigation())
    return Result::kProceed;

  // Defer the prerender activation until the ongoing prerender main frame
  // navigation commits.
  done_closure_ = std::move(resume);
  defer_start_time_ = base::TimeTicks::Now();
  return Result::kDefer;
}

void PrerenderCommitDeferringCondition::DidFinishNavigation(
    NavigationHandle* handle) {
  auto* finished_navigation_request = NavigationRequest::From(handle);

  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);

  // If the prerender frame tree node is gone, there is nothing to do.
  if (!prerender_frame_tree_node)
    return;

  // If the finished navigation is not for the prerendering main frame,
  // ignore this event.
  if (finished_navigation_request->frame_tree_node() !=
      prerender_frame_tree_node) {
    return;
  }

  // Since the prerender navigation finished, and
  // PrerenderNavigationThrottle disallows another navigation after the
  // initial commit, there should not be another navigation starting.
  //
  // The old navigation might not yet have cleaned up yet, so try that
  // first.
  prerender_frame_tree_node->render_manager()->MaybeCleanUpNavigation();
  DCHECK(!prerender_frame_tree_node->HasNavigation());

  if (done_closure_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                     std::move(done_closure_));

    // Record the defer waiting time for PrerenderCommitDeferringCondition.
    base::TimeDelta delta = base::TimeTicks::Now() - defer_start_time_;
    base::UmaHistogramTimes("Navigation.Prerender.ActivationCommitDeferTime",
                            delta);
  }
}

}  // namespace content
