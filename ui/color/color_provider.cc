// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/color/color_provider.h"

#include <map>
#include <set>
#include <utility>

#include "base/logging.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider_utils.h"
#include "ui/gfx/color_palette.h"

namespace ui {

ColorProvider::ColorProvider() = default;

ColorProvider::~ColorProvider() = default;

ColorMixer& ColorProvider::AddMixer() {
  DCHECK(!color_map_);

  mixers_.emplace_after(first_postprocessing_mixer_,
                        GetLastNonPostprocessingMixer(),
                        base::BindRepeating(
                            [](const ColorProvider* provider) {
                              return provider->GetLastNonPostprocessingMixer();
                            },
                            base::Unretained(this)));
  return *std::next(first_postprocessing_mixer_, 1);
}

ColorMixer& ColorProvider::AddPostprocessingMixer() {
  DCHECK(!color_map_);

  if (first_postprocessing_mixer_ == mixers_.before_begin()) {
    mixers_.emplace_front(
        mixers_.empty() ? nullptr : &mixers_.front(),
        base::BindRepeating(
            [](const ColorProvider* provider) {
              return provider->GetLastNonPostprocessingMixer();
            },
            base::Unretained(this)));
    first_postprocessing_mixer_ = mixers_.begin();
  } else {
    mixers_.emplace_front(
        &mixers_.front(),
        base::BindRepeating([](const ColorMixer* mixer) { return mixer; },
                            base::Unretained(&mixers_.front())));
  }
  return mixers_.front();
}

SkColor ColorProvider::GetColor(ColorId id) const {
  DCHECK_COLOR_ID_VALID(id);
  DCHECK(color_map_);
  auto i = color_map_->find(id);
  return i == color_map_->end() ? gfx::kPlaceholderColor : i->second;
}

void ColorProvider::GenerateColorMap() {
  // This should only be called to generate the `color_map_` once.
  DCHECK(!color_map_);

  if (mixers_.empty())
    DVLOG(2) << "ColorProvider::GenerateColorMap: No mixers defined!";

  // Iterate over associated mixers and extract the ColorIds defined for this
  // provider.
  std::set<ColorId> color_ids;
  for (const auto& mixer : mixers_) {
    const auto mixer_color_ids = mixer.GetDefinedColorIds();
    color_ids.insert(mixer_color_ids.begin(), mixer_color_ids.end());
  }

  // Iterate through all defined ColorIds and seed the `color_map` with the
  // computed values. Use a std::map rather than a base::flat_map since it has
  // frequent inserts and could grow very large.
  std::map<ColorId, SkColor> color_map;
  for (const auto& color_id : color_ids)
    color_map.insert({color_id, mixers_.front().GetResultColor(color_id)});

  // Construct the color_map_.
  color_map_ = std::make_unique<ColorMap>(color_map.begin(), color_map.end());

  // Clear away all associated mixers as these are no longer needed.
  mixers_.clear();
  first_postprocessing_mixer_ = mixers_.before_begin();
}

const ColorMixer* ColorProvider::GetLastNonPostprocessingMixer() const {
  const auto it = std::next(first_postprocessing_mixer_, 1);
  return (it == mixers_.cend()) ? nullptr : &(*it);
}

}  // namespace ui
