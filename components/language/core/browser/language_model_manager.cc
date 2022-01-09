// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/language/core/browser/language_model_manager.h"

#include <utility>

namespace language {

LanguageModelManager::LanguageModelManager(PrefService* prefs,
                                           const std::string& ui_lang)
    : primary_model_type_(ModelType::BASELINE) {
}

LanguageModelManager::~LanguageModelManager() {}

void LanguageModelManager::AddModel(const ModelType type,
                                    std::unique_ptr<LanguageModel> model) {
  models_[type] = std::move(model);
}

void LanguageModelManager::SetPrimaryModel(ModelType type) {
  DCHECK(models_.find(type) != models_.end());
  primary_model_type_ = type;
}

LanguageModel* LanguageModelManager::GetPrimaryModel() const {
  return models_.at(primary_model_type_).get();
}

LanguageModel* LanguageModelManager::GetLanguageModel(ModelType type) {
  if (models_.find(type) == models_.end()) {
    return nullptr;
  }
  return models_.at(type).get();
}
}  // namespace language
