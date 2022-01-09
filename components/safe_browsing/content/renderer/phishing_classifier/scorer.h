// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This abstract class loads a client-side model and lets you compute a phishing
// score for a set of previously extracted features.  The phishing score
// corresponds to the probability that the features are indicative of a phishing
// site.
//
// For more details on how the score is actually computed, consult the two
// derived classes protobuf_scorer.h and flatbuffer_scorer.h
//
// See features.h for a list of features that are currently used.

#ifndef COMPONENTS_SAFE_BROWSING_CONTENT_RENDERER_PHISHING_CLASSIFIER_SCORER_H_
#define COMPONENTS_SAFE_BROWSING_CONTENT_RENDERER_PHISHING_CLASSIFIER_SCORER_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_set>

#include "base/callback.h"
#include "base/files/file.h"
#include "base/files/memory_mapped_file.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_piece.h"
#include "components/optimization_guide/machine_learning_tflite_buildflags.h"
#include "components/safe_browsing/core/common/proto/client_model.pb.h"
#include "components/safe_browsing/core/common/proto/csd.pb.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace safe_browsing {
class FeatureMap;

// Enum used to keep stats about the status of the Scorer creation.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum ScorerCreationStatus {
  SCORER_SUCCESS = 0,
  SCORER_FAIL_MODEL_OPEN_FAIL = 1,       // Not used anymore
  SCORER_FAIL_MODEL_FILE_EMPTY = 2,      // Not used anymore
  SCORER_FAIL_MODEL_FILE_TOO_LARGE = 3,  // Not used anymore
  SCORER_FAIL_MODEL_PARSE_ERROR = 4,
  SCORER_FAIL_MODEL_MISSING_FIELDS = 5,
  SCORER_FAIL_MAP_VISUAL_TFLITE_MODEL = 6,
  SCORER_FAIL_FLATBUFFER_INVALID_REGION = 7,
  SCORER_FAIL_FLATBUFFER_INVALID_MAPPING = 8,
  SCORER_FAIL_FLATBUFFER_FAILED_VERIFY = 9,
  SCORER_FAIL_FLATBUFFER_BAD_INDICES_OR_FIELDS = 10,
  SCORER_STATUS_MAX  // Always add new values before this one.
};

// Scorer methods are virtual to simplify mocking of this class,
// and to allow inheritance.
class Scorer {
 public:
  virtual ~Scorer();
  // Most clients should use the factory method.  This constructor is public
  // to allow for mock implementations.
  Scorer();

  // This method computes the probability that the given features are indicative
  // of phishing.  It returns a score value that falls in the range [0.0,1.0]
  // (range is inclusive on both ends).
  virtual double ComputeScore(const FeatureMap& features) const = 0;

  // This method matches the given |bitmap| against the visual model. It
  // modifies |request| appropriately, and returns the new request. This expects
  // to be called on the renderer main thread, but will perform scoring
  // asynchronously on a worker thread.
  virtual void GetMatchingVisualTargets(
      const SkBitmap& bitmap,
      std::unique_ptr<ClientPhishingRequest> request,
      base::OnceCallback<void(std::unique_ptr<ClientPhishingRequest>)> callback)
      const = 0;

// TODO(crbug/1278502): This is disabled as a temporary measure due to crashes.
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB) && !defined(OS_CHROMEOS) && \
    !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_CHROMEOS_LACROS)
  // This method applies the TfLite visual model to the given bitmap. It
  // asynchronously returns the list of scores for each category, in the same
  // order as `tflite_thresholds()`.
  virtual void ApplyVisualTfLiteModel(
      const SkBitmap& bitmap,
      base::OnceCallback<void(std::vector<double>)> callback) = 0;
#endif

  // Returns the version number of the loaded client model.
  virtual int model_version() const = 0;

  bool HasVisualTfLiteModel() const;

  // -- Accessors used by the page feature extractor ---------------------------

  // Returns a callback to find if a page word is in the model.
  virtual base::RepeatingCallback<bool(uint32_t)> find_page_word_callback()
      const = 0;

  // Returns a callback to find if a page term is in the model.
  virtual base::RepeatingCallback<bool(const std::string&)>
  find_page_term_callback() const = 0;

  // Return the maximum number of words per term for the loaded model.
  virtual size_t max_words_per_term() const = 0;

  // Returns the murmurhash3 seed for the loaded model.
  virtual uint32_t murmurhash3_seed() const = 0;

  // Return the maximum number of unique shingle hashes per page.
  virtual size_t max_shingles_per_page() const = 0;

  // Return the number of words in a shingle.
  virtual size_t shingle_size() const = 0;

  // Returns the threshold probability above which we send a CSD ping.
  virtual float threshold_probability() const = 0;

  // Returns the version of the visual TFLite model.
  virtual int tflite_model_version() const = 0;

  // Returns the thresholds configured for the visual TFLite model categories.
  virtual const google::protobuf::RepeatedPtrField<
      TfLiteModelMetadata::Threshold>&
  tflite_thresholds() const = 0;

  // Disable copy and move.
  Scorer(const Scorer&) = delete;
  Scorer& operator=(const Scorer&) = delete;

 protected:
  // Helper function which converts log odds to a probability in the range
  // [0.0,1.0].
  static double LogOdds2Prob(double log_odds);

  // Helper struct used to return the scores and the memory mapped file
  // containing the model back to the main thread.
  struct VisualTfliteModelHelperResult {
    VisualTfliteModelHelperResult();
    ~VisualTfliteModelHelperResult();
    VisualTfliteModelHelperResult(const VisualTfliteModelHelperResult&) =
        delete;
    VisualTfliteModelHelperResult& operator=(
        const VisualTfliteModelHelperResult&) = delete;
    VisualTfliteModelHelperResult(VisualTfliteModelHelperResult&&);
    VisualTfliteModelHelperResult& operator=(VisualTfliteModelHelperResult&&);

    std::vector<double> scores;
    std::unique_ptr<base::MemoryMappedFile> visual_tflite_model;
  };

  // Apply the tflite model to the bitmap, and return scores.
  static VisualTfliteModelHelperResult ApplyVisualTfLiteModelHelper(
      const SkBitmap& bitmap,
      int input_width,
      int input_height,
      std::unique_ptr<base::MemoryMappedFile> visual_tflite_model);
  void OnVisualTfLiteModelComplete(
      base::OnceCallback<void(std::vector<double>)> callback,
      VisualTfliteModelHelperResult result);

  std::unique_ptr<base::MemoryMappedFile> visual_tflite_model_;
  base::WeakPtrFactory<Scorer> weak_ptr_factory_{this};

 private:
  friend class PhishingScorerTest;
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CONTENT_RENDERER_PHISHING_CLASSIFIER_SCORER_H_
