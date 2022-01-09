// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/content/renderer/phishing_classifier/flatbuffer_scorer.h"

#include <math.h>

#include <memory>

#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/shared_memory_mapping.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "components/safe_browsing/content/renderer/phishing_classifier/features.h"
#include "components/safe_browsing/core/common/proto/client_model.pb.h"
#include "components/safe_browsing/core/common/proto/csd.pb.h"
#include "components/safe_browsing/core/common/visual_utils.h"
#include "content/public/renderer/render_thread.h"
#include "crypto/sha2.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace safe_browsing {

namespace {
bool VerifyCSDFlatBufferIndicesAndFields(const flat::ClientSideModel* model) {
  const flatbuffers::Vector<flatbuffers::Offset<flat::Hash>>* hashes =
      model->hashes();
  if (!hashes)
    return false;

  const flatbuffers::Vector<
      flatbuffers::Offset<safe_browsing::flat::ClientSideModel_::Rule>>* rules =
      model->rule();
  if (!rules)
    return false;
  for (const flat::ClientSideModel_::Rule* rule : *model->rule()) {
    if (!rule || !rule->feature())
      return false;
    for (int32_t feature : *rule->feature()) {
      if (feature < 0 || feature >= static_cast<int32_t>(hashes->size())) {
        return false;
      }
    }
  }

  const flatbuffers::Vector<int32_t>* page_terms = model->page_term();
  if (!page_terms)
    return false;
  for (int32_t page_term_idx : *page_terms) {
    if (page_term_idx < 0 ||
        page_term_idx >= static_cast<int32_t>(hashes->size())) {
      return false;
    }
  }

  const flatbuffers::Vector<uint32_t>* page_words = model->page_word();
  if (!page_words)
    return false;

  const flat::TfLiteModelMetadata* metadata = model->tflite_metadata();
  if (!metadata)
    return false;
  const flatbuffers::Vector<
      flatbuffers::Offset<flat::TfLiteModelMetadata_::Threshold>>* thresholds =
      metadata->thresholds();
  if (!thresholds)
    return false;
  for (const flat::TfLiteModelMetadata_::Threshold* threshold : *thresholds) {
    if (!threshold || !threshold->label())
      return false;
  }

  return true;
}

std::string HashToString(const flat::Hash* hash) {
  return std::string(reinterpret_cast<const char*>(hash->data()->Data()),
                     hash->data()->size());
}

void RecordScorerCreationStatus(ScorerCreationStatus status) {
  UMA_HISTOGRAM_ENUMERATION("SBClientPhishing.FlatBufferScorer.CreationStatus",
                            status, SCORER_STATUS_MAX);
}

}  // namespace

FlatBufferModelScorer::FlatBufferModelScorer() = default;
FlatBufferModelScorer::~FlatBufferModelScorer() = default;

/* static */
FlatBufferModelScorer* FlatBufferModelScorer::Create(
    base::ReadOnlySharedMemoryRegion region,
    base::File visual_tflite_model) {
  std::unique_ptr<FlatBufferModelScorer> scorer(new FlatBufferModelScorer());

  if (!region.IsValid()) {
    RecordScorerCreationStatus(SCORER_FAIL_FLATBUFFER_INVALID_REGION);
    return nullptr;
  }

  base::ReadOnlySharedMemoryMapping mapping = region.Map();
  if (!mapping.IsValid()) {
    RecordScorerCreationStatus(SCORER_FAIL_FLATBUFFER_INVALID_MAPPING);
    return nullptr;
  }

  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t*>(mapping.memory()), mapping.size());
  if (!flat::VerifyClientSideModelBuffer(verifier)) {
    RecordScorerCreationStatus(SCORER_FAIL_FLATBUFFER_FAILED_VERIFY);
    return nullptr;
  }
  scorer->flatbuffer_model_ = flat::GetClientSideModel(mapping.memory());

  if (!VerifyCSDFlatBufferIndicesAndFields(scorer->flatbuffer_model_)) {
    RecordScorerCreationStatus(SCORER_FAIL_FLATBUFFER_BAD_INDICES_OR_FIELDS);
    return nullptr;
  }

  // Only do this part if the visual model file exists
  if (visual_tflite_model.IsValid()) {
    scorer->visual_tflite_model_ = std::make_unique<base::MemoryMappedFile>();
    if (!scorer->visual_tflite_model_->Initialize(
            std::move(visual_tflite_model))) {
      RecordScorerCreationStatus(SCORER_FAIL_MAP_VISUAL_TFLITE_MODEL);
      return nullptr;
    } else {
      for (const flat::TfLiteModelMetadata_::Threshold* flat_threshold :
           *(scorer->flatbuffer_model_->tflite_metadata()->thresholds())) {
        TfLiteModelMetadata::Threshold* threshold = scorer->thresholds_.Add();
        threshold->set_label(flat_threshold->label()->str());
        threshold->set_threshold(flat_threshold->threshold());
      }
    }
  }

  RecordScorerCreationStatus(SCORER_SUCCESS);
  scorer->flatbuffer_mapping_ = std::move(mapping);

  return scorer.release();
}

double FlatBufferModelScorer::ComputeRuleScore(
    const flat::ClientSideModel_::Rule* rule,
    const FeatureMap& features) const {
  const std::unordered_map<std::string, double>& feature_map =
      features.features();
  double rule_score = 1.0;
  for (int32_t feature : *rule->feature()) {
    const flat::Hash* hash = flatbuffer_model_->hashes()->Get(feature);
    std::string hash_str(reinterpret_cast<const char*>(hash->data()->Data()),
                         hash->data()->size());
    const auto it = feature_map.find(hash_str);
    if (it == feature_map.end() || it->second == 0.0) {
      // If the feature of the rule does not exist in the given feature map the
      // feature weight is considered to be zero.  If the feature weight is zero
      // we leave early since we know that the rule score will be zero.
      return 0.0;
    }
    rule_score *= it->second;
  }
  return rule_score * rule->weight();
}

double FlatBufferModelScorer::ComputeScore(const FeatureMap& features) const {
  double logodds = 0.0;
  for (const flat::ClientSideModel_::Rule* rule : *flatbuffer_model_->rule()) {
    logodds += ComputeRuleScore(rule, features);
  }
  return LogOdds2Prob(logodds);
}

// Only DOM model implemented for FlatBuffer.
void FlatBufferModelScorer::GetMatchingVisualTargets(
    const SkBitmap& bitmap,
    std::unique_ptr<ClientPhishingRequest> request,
    base::OnceCallback<void(std::unique_ptr<ClientPhishingRequest>)> callback)
    const {
  NOTIMPLEMENTED();
}

#if BUILDFLAG(BUILD_WITH_TFLITE_LIB) && !defined(OS_CHROMEOS) && \
    !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_CHROMEOS_LACROS)
void FlatBufferModelScorer::ApplyVisualTfLiteModel(
    const SkBitmap& bitmap,
    base::OnceCallback<void(std::vector<double>)> callback) {
  DCHECK(content::RenderThread::IsMainThread());
  if (visual_tflite_model_ && visual_tflite_model_->IsValid()) {
    base::Time start_post_task_time = base::Time::Now();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::TaskPriority::BEST_EFFORT, base::WithBaseSyncPrimitives()},
        base::BindOnce(&ApplyVisualTfLiteModelHelper, bitmap,
                       flatbuffer_model_->tflite_metadata()->input_width(),
                       flatbuffer_model_->tflite_metadata()->input_height(),
                       std::move(visual_tflite_model_)),
        base::BindOnce(&FlatBufferModelScorer::OnVisualTfLiteModelComplete,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
    base::UmaHistogramTimes(
        "SBClientPhishing.TfLiteModelLoadTime.FlatbufferScorer",
        base::Time::Now() - start_post_task_time);
  } else {
    std::move(callback).Run(std::vector<double>());
  }
}
#endif

int FlatBufferModelScorer::model_version() const {
  return flatbuffer_model_->version();
}

bool FlatBufferModelScorer::has_page_term(const std::string& str) const {
  const flatbuffers::Vector<flatbuffers::Offset<flat::Hash>>* hashes =
      flatbuffer_model_->hashes();
  flatbuffers::Vector<flatbuffers::Offset<flat::Hash>>::const_iterator
      hashes_iter =
          std::lower_bound(hashes->begin(), hashes->end(), str,
                           [](const flat::Hash* hash, const std::string& str) {
                             std::string hash_str = HashToString(hash);
                             return hash_str.compare(str) < 0;
                           });
  if (hashes_iter == hashes->end() || HashToString(*hashes_iter) != str)
    return false;
  int index = hashes_iter - hashes->begin();
  const flatbuffers::Vector<int32_t>* page_terms =
      flatbuffer_model_->page_term();
  return std::binary_search(page_terms->begin(), page_terms->end(), index);
}

base::RepeatingCallback<bool(const std::string&)>
FlatBufferModelScorer::find_page_term_callback() const {
  return base::BindRepeating(&FlatBufferModelScorer::has_page_term,
                             base::Unretained(this));
}

bool FlatBufferModelScorer::has_page_word(uint32_t page_word_hash) const {
  const flatbuffers::Vector<uint32_t>* page_words =
      flatbuffer_model_->page_word();
  return std::binary_search(page_words->begin(), page_words->end(),
                            page_word_hash);
}

base::RepeatingCallback<bool(uint32_t)>
FlatBufferModelScorer::find_page_word_callback() const {
  return base::BindRepeating(&FlatBufferModelScorer::has_page_word,
                             base::Unretained(this));
}

size_t FlatBufferModelScorer::max_words_per_term() const {
  return flatbuffer_model_->max_words_per_term();
}
uint32_t FlatBufferModelScorer::murmurhash3_seed() const {
  return flatbuffer_model_->murmur_hash_seed();
}
size_t FlatBufferModelScorer::max_shingles_per_page() const {
  return flatbuffer_model_->max_shingles_per_page();
}
size_t FlatBufferModelScorer::shingle_size() const {
  return flatbuffer_model_->shingle_size();
}
float FlatBufferModelScorer::threshold_probability() const {
  return flatbuffer_model_->threshold_probability();
}
int FlatBufferModelScorer::tflite_model_version() const {
  return flatbuffer_model_->tflite_metadata()->version();
}
const google::protobuf::RepeatedPtrField<TfLiteModelMetadata::Threshold>&
FlatBufferModelScorer::tflite_thresholds() const {
  return thresholds_;
}

}  // namespace safe_browsing
