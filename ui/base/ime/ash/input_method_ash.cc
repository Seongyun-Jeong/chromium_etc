// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/ash/input_method_ash.h"

#include <stddef.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/check.h"
#include "base/i18n/char_iterator.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "base/time/default_clock.h"
#include "chromeos/system/devicemode.h"
#include "ui/base/ime/ash/ime_bridge.h"
#include "ui/base/ime/ash/ime_engine_handler_interface.h"
#include "ui/base/ime/ash/ime_keyboard.h"
#include "ui/base/ime/ash/input_method_manager.h"
#include "ui/base/ime/ash/typing_session_manager.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/input_method_delegate.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/gfx/geometry/rect.h"

namespace ui {

ui::IMEEngineHandlerInterface* GetEngine() {
  auto* bridge = ui::IMEBridge::Get();
  return bridge ? bridge->GetCurrentEngineHandler() : nullptr;
}

// InputMethodAsh implementation -----------------------------------------
InputMethodAsh::InputMethodAsh(internal::InputMethodDelegate* delegate)
    : InputMethodBase(delegate),
      typing_session_manager_(base::DefaultClock::GetInstance()) {
  ResetContext();
}

InputMethodAsh::~InputMethodAsh() {
  ConfirmCompositionText(/* reset_engine */ true, /* keep_selection */ false);
  // We are dead, so we need to ask the client to stop relying on us.
  OnInputMethodChanged();

  if (ui::IMEBridge::Get() &&
      ui::IMEBridge::Get()->GetInputContextHandler() == this) {
    ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
  }
  typing_session_manager_.EndAndRecordSession();
}

InputMethodAsh::PendingSetCompositionRange::PendingSetCompositionRange(
    const gfx::Range& range,
    const std::vector<ui::ImeTextSpan>& text_spans)
    : range(range), text_spans(text_spans) {}

InputMethodAsh::PendingSetCompositionRange::PendingSetCompositionRange(
    const PendingSetCompositionRange& other) = default;

InputMethodAsh::PendingSetCompositionRange::~PendingSetCompositionRange() =
    default;

ui::EventDispatchDetails InputMethodAsh::DispatchKeyEvent(ui::KeyEvent* event) {
  DCHECK(!(event->flags() & ui::EF_IS_SYNTHESIZED));

  // For OS_CHROMEOS build of Chrome running on Linux, the IME keyboard cannot
  // track the Caps Lock state by itself, so need to call SetCapsLockEnabled()
  // method to reflect the Caps Lock state by the key event.
  auto* manager = ash::input_method::InputMethodManager::Get();
  if (manager) {
    ash::input_method::ImeKeyboard* keyboard = manager->GetImeKeyboard();
    if (keyboard && event->type() == ET_KEY_PRESSED &&
        event->key_code() != ui::VKEY_CAPITAL &&
        keyboard->CapsLockIsEnabled() != event->IsCapsLockOn()) {
      // Synchronize the keyboard state with event's state if they do not
      // match. Do not synchronize for Caps Lock key because it is already
      // handled in event rewriter.
      keyboard->SetCapsLockEnabled(event->IsCapsLockOn());
    }

    // For JP106 language input keys, makes sure they can be passed to the app
    // so that the VDI web apps can be supported. See https://crbug.com/816341.
    // VKEY_CONVERT: Henkan key
    // VKEY_NONCONVERT: Muhenkan key
    // VKEY_DBE_SBCSCHAR/VKEY_DBE_DBCSCHAR: ZenkakuHankaku key
    ash::input_method::InputMethodManager::State* state =
        manager->GetActiveIMEState().get();
    if (event->type() == ET_KEY_PRESSED && state) {
      bool language_input_key = true;
      switch (event->key_code()) {
        case ui::VKEY_CONVERT:
          state->ChangeInputMethodToJpIme();
          break;
        case ui::VKEY_NONCONVERT:
          state->ChangeInputMethodToJpKeyboard();
          break;
        case ui::VKEY_DBE_SBCSCHAR:
        case ui::VKEY_DBE_DBCSCHAR:
          state->ToggleInputMethodForJpIme();
          break;
        default:
          language_input_key = false;
          break;
      }
      if (language_input_key) {
        // Dispatches the event to app/blink.
        // TODO(shuchen): Eventually, the language input keys should be handed
        // over to the IME extension to process. And IMF can handle if the IME
        // extension didn't handle.
        return DispatchKeyEventPostIME(event);
      }
    }
  }

  // If |context_| is not usable, then we can only dispatch the key event as is.
  // We only dispatch the key event to input method when the |context_| is an
  // normal input field (not a password field).
  // Note: We need to send the key event to ibus even if the |context_| is not
  // enabled, so that ibus can have a chance to enable the |context_|.
  if (IsPasswordOrNoneInputFieldFocused() || !GetEngine()) {
    if (event->type() == ET_KEY_PRESSED) {
      if (ExecuteCharacterComposer(*event)) {
        // Treating as PostIME event if character composer handles key event and
        // generates some IME event,
        return ProcessKeyEventPostIME(event,
                                      /* handled */ true,
                                      /* stopped_propagation */ true);
      }
      return ProcessUnfilteredKeyPressEvent(event);
    }
    return DispatchKeyEventPostIME(event);
  }

  handling_key_event_ = true;
  GetEngine()->ProcessKeyEvent(
      *event, base::BindOnce(&InputMethodAsh::ProcessKeyEventDone,
                             weak_ptr_factory_.GetWeakPtr(),
                             // Pass the ownership of the new copied event.
                             base::Owned(new ui::KeyEvent(*event))));
  return ui::EventDispatchDetails();
}

void InputMethodAsh::ProcessKeyEventDone(ui::KeyEvent* event, bool is_handled) {
  DCHECK(event);
  if (event->type() == ET_KEY_PRESSED) {
    if (is_handled) {
      // IME event has a priority to be handled, so that character composer
      // should be reset.
      character_composer_.Reset();
    } else {
      // If IME does not handle key event, passes keyevent to character composer
      // to be able to compose complex characters.
      is_handled = ExecuteCharacterComposer(*event);

      if (!is_handled &&
          !KeycodeConverter::IsDomKeyForModifier(event->GetDomKey())) {
        // If the character composer didn't handle it either, then confirm any
        // composition text before forwarding the key event. We ignore modifier
        // keys because, for example, if the IME handles Shift+A, then we don't
        // want the Shift key to confirm the composition text. Only confirm the
        // composition text when the IME does not handle the full key combo.
        ConfirmCompositionText(/* reset_engine */ true,
                               /* keep_selection */ true);
      }
    }
  }
  if (event->type() == ET_KEY_PRESSED || event->type() == ET_KEY_RELEASED) {
    std::ignore = ProcessKeyEventPostIME(event, is_handled,
                                         /* stopped_propagation */ false);
  }
  handling_key_event_ = false;
}

void InputMethodAsh::OnTextInputTypeChanged(const TextInputClient* client) {
  if (!IsTextInputClientFocused(client))
    return;

  UpdateContextFocusState();

  ui::IMEEngineHandlerInterface* engine = GetEngine();
  if (engine) {
    ui::IMEEngineHandlerInterface::InputContext context(
        GetTextInputType(), GetTextInputMode(), GetTextInputFlags(),
        GetClientFocusReason(), GetClientShouldDoLearning());
    // When focused input client is not changed, a text input type change
    // should cause blur/focus events to engine. The focus in to or out from
    // password field should also notify engine.
    engine->FocusOut();
    engine->FocusIn(context);
  }

  OnCaretBoundsChanged(client);

  InputMethodBase::OnTextInputTypeChanged(client);
}

void InputMethodAsh::OnCaretBoundsChanged(const TextInputClient* client) {
  if (IsTextInputTypeNone() || !IsTextInputClientFocused(client))
    return;

  NotifyTextInputCaretBoundsChanged(client);

  if (IsPasswordOrNoneInputFieldFocused())
    return;

  // The current text input type should not be NONE if |context_| is focused.
  DCHECK(client == GetTextInputClient());
  DCHECK(!IsTextInputTypeNone());

  if (GetEngine())
    GetEngine()->SetCompositionBounds(GetCompositionBounds(client));

  ash::IMECandidateWindowHandlerInterface* candidate_window =
      ui::IMEBridge::Get()->GetCandidateWindowHandler();
  ash::IMEAssistiveWindowHandlerInterface* assistive_window =
      ui::IMEBridge::Get()->GetAssistiveWindowHandler();
  if (!candidate_window && !assistive_window)
    return;

  const gfx::Rect caret_rect = client->GetCaretBounds();

  gfx::Rect composition_head;
  if (client->HasCompositionText())
    client->GetCompositionCharacterBounds(0, &composition_head);

  // Pepper doesn't support composition bounds, so fall back to caret bounds to
  // avoid a bad user experience (the IME window moved to upper left corner).
  if (composition_head.IsEmpty())
    composition_head = caret_rect;
  if (candidate_window)
    candidate_window->SetCursorBounds(caret_rect, composition_head);

  if (assistive_window) {
    ash::Bounds bounds;
    bounds.caret = caret_rect;
    bounds.autocorrect = client->GetAutocorrectCharacterBounds();
    client->GetCompositionCharacterBounds(0, &bounds.composition_text);
    assistive_window->SetBounds(bounds);
  }

  gfx::Range text_range;
  gfx::Range selection_range;
  std::u16string surrounding_text;
  if (!client->GetTextRange(&text_range) ||
      !client->GetTextFromRange(text_range, &surrounding_text) ||
      !client->GetEditableSelectionRange(&selection_range)) {
    previous_surrounding_text_.clear();
    previous_selection_range_ = gfx::Range::InvalidRange();
    return;
  }

  if (previous_selection_range_ == selection_range &&
      previous_surrounding_text_ == surrounding_text)
    return;

  previous_selection_range_ = selection_range;
  previous_surrounding_text_ = surrounding_text;

  if (!selection_range.IsValid()) {
    // TODO(nona): Ideally selection_range should not be invalid.
    // TODO(nona): If javascript changes the focus on page loading, even (0,0)
    //             can not be obtained. Need investigation.
    return;
  }

  // Here SetSurroundingText accepts relative position of |surrounding_text|, so
  // we have to convert |selection_range| from node coordinates to
  // |surrounding_text| coordinates.
  if (GetEngine()) {
    GetEngine()->SetSurroundingText(
        surrounding_text, selection_range.start() - text_range.start(),
        selection_range.end() - text_range.start(), text_range.start());
  }
}

void InputMethodAsh::CancelComposition(const TextInputClient* client) {
  if (!IsPasswordOrNoneInputFieldFocused() && IsTextInputClientFocused(client))
    ResetContext();
}

bool InputMethodAsh::IsCandidatePopupOpen() const {
  // TODO(yukishiino): Implement this method.
  return false;
}

VirtualKeyboardController* InputMethodAsh::GetVirtualKeyboardController() {
  auto* manager = ash::input_method::InputMethodManager::Get();
  if (manager) {
    if (auto* controller = manager->GetVirtualKeyboardController())
      return controller;
  }
  return InputMethodBase::GetVirtualKeyboardController();
}

void InputMethodAsh::OnFocus() {
  ui::IMEBridge* bridge = ui::IMEBridge::Get();
  if (bridge) {
    bridge->SetInputContextHandler(this);
  }
}

void InputMethodAsh::OnTouch(ui::EventPointerType pointerType) {
  TextInputClient* client = GetTextInputClient();
  if (!client || !IsTextInputClientFocused(client)) {
    return;
  }
  ui::IMEEngineHandlerInterface* engine = GetEngine();
  if (engine) {
    engine->OnTouch(pointerType);
  }
}

void InputMethodAsh::OnBlur() {
  if (ui::IMEBridge::Get() &&
      ui::IMEBridge::Get()->GetInputContextHandler() == this)
    ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
}

void InputMethodAsh::OnWillChangeFocusedClient(TextInputClient* focused_before,
                                               TextInputClient* focused) {
  ConfirmCompositionText(/* reset_engine */ true, /* keep_selection */ false);

  // Remove any autocorrect range in the unfocused TextInputClient.
  gfx::Range text_range;
  if (focused_before && focused_before->GetTextRange(&text_range)) {
    focused_before->SetAutocorrectRange(gfx::Range());
  }

  if (GetEngine())
    GetEngine()->FocusOut();
}

void InputMethodAsh::OnDidChangeFocusedClient(TextInputClient* focused_before,
                                              TextInputClient* focused) {
  // Force to update the input type since client's TextInputStateChanged()
  // function might not be called if text input types before the client loses
  // focus and after it acquires focus again are the same.
  UpdateContextFocusState();

  if (GetEngine()) {
    ui::IMEEngineHandlerInterface::InputContext context(
        GetTextInputType(), GetTextInputMode(), GetTextInputFlags(),
        GetClientFocusReason(), GetClientShouldDoLearning());
    GetEngine()->FocusIn(context);
  }

  OnCaretBoundsChanged(GetTextInputClient());
}

bool InputMethodAsh::SetCompositionRange(
    uint32_t before,
    uint32_t after,
    const std::vector<ui::ImeTextSpan>& text_spans) {
  TextInputClient* client = GetTextInputClient();

  if (IsTextInputTypeNone())
    return false;
  typing_session_manager_.Heartbeat();
  // The given range and spans are relative to the current selection.
  gfx::Range range;
  if (!client->GetEditableSelectionRange(&range))
    return false;

  const gfx::Range composition_range(range.start() - before,
                                     range.end() + after);

  // Check that the composition range is valid.
  gfx::Range text_range;
  client->GetTextRange(&text_range);
  if (!text_range.Contains(composition_range))
    return false;

  return SetComposingRange(composition_range.start(), composition_range.end(),
                           text_spans);
}

bool InputMethodAsh::SetComposingRange(
    uint32_t start,
    uint32_t end,
    const std::vector<ui::ImeTextSpan>& text_spans) {
  TextInputClient* client = GetTextInputClient();

  if (IsTextInputTypeNone())
    return false;

  const auto ordered_range = std::minmax(start, end);
  const gfx::Range composition_range(ordered_range.first, ordered_range.second);

  // Use a default text span that spans across the whole composition range.
  auto non_empty_text_spans =
      !text_spans.empty()
          ? text_spans
          : std::vector<ui::ImeTextSpan>{ui::ImeTextSpan(
                ui::ImeTextSpan::Type::kComposition,
                /*start_offset=*/0, /*end_offset=*/composition_range.length())};

  // If we have pending key events, then delay the operation until
  // |ProcessKeyEventPostIME|. Otherwise, process it immediately.
  if (handling_key_event_) {
    composition_changed_ = true;
    pending_composition_range_ =
        PendingSetCompositionRange{composition_range, non_empty_text_spans};
    return true;
  } else {
    composing_text_ = true;
    return client->SetCompositionFromExistingText(composition_range,
                                                  non_empty_text_spans);
  }
}

gfx::Range InputMethodAsh::GetAutocorrectRange() {
  if (IsTextInputTypeNone())
    return gfx::Range();
  return GetTextInputClient()->GetAutocorrectRange();
}

gfx::Rect InputMethodAsh::GetAutocorrectCharacterBounds() {
  if (IsTextInputTypeNone())
    return gfx::Rect();
  return GetTextInputClient()->GetAutocorrectCharacterBounds();
}

gfx::Rect InputMethodAsh::GetTextFieldBounds() {
  if (IsTextInputTypeNone())
    return gfx::Rect();
  absl::optional<gfx::Rect> control_bounds;
  absl::optional<gfx::Rect> selection_bounds;
  GetTextInputClient()->GetActiveTextInputControlLayoutBounds(
      &control_bounds, &selection_bounds);
  return control_bounds ? *control_bounds : gfx::Rect();
}

bool InputMethodAsh::SetAutocorrectRange(const gfx::Range& range) {
  if (IsTextInputTypeNone())
    return false;

  // If we have pending key events, then delay the operation until
  // |ProcessKeyEventPostIME|. Otherwise, process it immediately.
  if (handling_key_event_) {
    pending_autocorrect_range_ = range;
    return true;
  } else {
    return GetTextInputClient()->SetAutocorrectRange(range);
  }
}

absl::optional<GrammarFragment> InputMethodAsh::GetGrammarFragment(
    const gfx::Range& range) {
  if (IsTextInputTypeNone())
    return absl::nullopt;
  return GetTextInputClient()->GetGrammarFragment(range);
}

bool InputMethodAsh::ClearGrammarFragments(const gfx::Range& range) {
  if (IsTextInputTypeNone())
    return false;
  return GetTextInputClient()->ClearGrammarFragments(range);
}

bool InputMethodAsh::AddGrammarFragments(
    const std::vector<GrammarFragment>& fragments) {
  if (IsTextInputTypeNone())
    return false;
  return GetTextInputClient()->AddGrammarFragments(fragments);
}

bool InputMethodAsh::SetSelectionRange(uint32_t start, uint32_t end) {
  if (IsTextInputTypeNone())
    return false;
  typing_session_manager_.Heartbeat();
  return GetTextInputClient()->SetEditableSelectionRange(
      gfx::Range(start, end));
}

void InputMethodAsh::ConfirmCompositionText(bool reset_engine,
                                            bool keep_selection) {
  TextInputClient* client = GetTextInputClient();
  if (client && client->HasCompositionText()) {
    const uint32_t characters_committed =
        client->ConfirmCompositionText(keep_selection);
    typing_session_manager_.CommitCharacters(characters_committed);
  }
  // See https://crbug.com/984472.
  ResetContext(reset_engine);
}

void InputMethodAsh::ResetContext(bool reset_engine) {
  if (IsPasswordOrNoneInputFieldFocused() || !GetTextInputClient())
    return;

  const bool was_composing = composing_text_;

  pending_composition_ = absl::nullopt;
  pending_commit_ = absl::nullopt;
  composing_text_ = false;
  composition_changed_ = false;

  if (reset_engine && was_composing && GetEngine())
    GetEngine()->Reset();

  character_composer_.Reset();
}

void InputMethodAsh::UpdateContextFocusState() {
  ResetContext();
  OnInputMethodChanged();

  // Propagate the focus event to the candidate window handler which also
  // manages the input method mode indicator.
  ash::IMECandidateWindowHandlerInterface* candidate_window =
      ui::IMEBridge::Get()->GetCandidateWindowHandler();
  if (candidate_window)
    candidate_window->FocusStateChanged(!IsPasswordOrNoneInputFieldFocused());

  // Propagate focus event to assistive window handler.
  ash::IMEAssistiveWindowHandlerInterface* assistive_window =
      ui::IMEBridge::Get()->GetAssistiveWindowHandler();
  if (assistive_window)
    assistive_window->FocusStateChanged();

  ui::IMEEngineHandlerInterface::InputContext context(
      GetTextInputType(), GetTextInputMode(), GetTextInputFlags(),
      GetClientFocusReason(), GetClientShouldDoLearning());
  ui::IMEBridge::Get()->SetCurrentInputContext(context);
}

ui::EventDispatchDetails InputMethodAsh::ProcessKeyEventPostIME(
    ui::KeyEvent* event,
    bool handled,
    bool stopped_propagation) {
  TextInputClient* client = GetTextInputClient();
  if (!client) {
    // As ibus works asynchronously, there is a chance that the focused client
    // loses focus before this method gets called.
    return DispatchKeyEventPostIME(event);
  }

  if (event->type() == ET_KEY_PRESSED && handled) {
    ui::EventDispatchDetails dispatch_details =
        ProcessFilteredKeyPressEvent(event);
    if (event->stopped_propagation()) {
      ResetContext();
      return dispatch_details;
    }
  }
  ui::EventDispatchDetails dispatch_details;

  // In case the focus was changed by the key event. The |context_| should have
  // been reset when the focused window changed.
  if (client != GetTextInputClient())
    return dispatch_details;

  MaybeProcessPendingInputMethodResult(event, handled);

  // In case the focus was changed when sending input method results to the
  // focused window.
  if (client != GetTextInputClient())
    return dispatch_details;

  if (handled)
    return dispatch_details;  // IME handled the key event. do not forward.

  if (event->type() == ET_KEY_PRESSED)
    return ProcessUnfilteredKeyPressEvent(event);

  if (event->type() == ET_KEY_RELEASED)
    return DispatchKeyEventPostIME(event);
  return dispatch_details;
}

ui::EventDispatchDetails InputMethodAsh::ProcessFilteredKeyPressEvent(
    ui::KeyEvent* event) {
  if (NeedInsertChar())
    return DispatchKeyEventPostIME(event);

  ui::KeyEvent fabricated_event(ET_KEY_PRESSED, VKEY_PROCESSKEY, event->code(),
                                event->flags(), DomKey::PROCESS,
                                event->time_stamp());
  ui::EventDispatchDetails dispatch_details =
      DispatchKeyEventPostIME(&fabricated_event);
  if (fabricated_event.stopped_propagation())
    event->StopPropagation();
  return dispatch_details;
}

ui::EventDispatchDetails InputMethodAsh::ProcessUnfilteredKeyPressEvent(
    ui::KeyEvent* event) {
  TextInputClient* prev_client = GetTextInputClient();
  ui::EventDispatchDetails details = DispatchKeyEventPostIME(event);
  if (event->stopped_propagation()) {
    ResetContext();
    return details;
  }

  // We shouldn't dispatch the character anymore if the key event dispatch
  // caused focus change. For example, in the following scenario,
  // 1. visit a web page which has a <textarea>.
  // 2. click Omnibox.
  // 3. enable Korean IME, press A, then press Tab to move the focus to the web
  //    page.
  // We should return here not to send the Tab key event to RWHV.
  TextInputClient* client = GetTextInputClient();
  if (!client || client != prev_client)
    return details;

  // If a key event was not filtered by |context_| and |character_composer_|,
  // then it means the key event didn't generate any result text. So we need
  // to send corresponding character to the focused text input client.
  if (event->GetCharacter()) {
    client->InsertChar(*event);
    typing_session_manager_.CommitCharacters(1);
  }
  return details;
}

void InputMethodAsh::MaybeProcessPendingInputMethodResult(ui::KeyEvent* event,
                                                          bool handled) {
  TextInputClient* client = GetTextInputClient();
  DCHECK(client);

  if (pending_commit_) {
    if (handled && NeedInsertChar()) {
      for (const auto& ch : pending_commit_->text) {
        KeyEvent ch_event(ET_KEY_PRESSED, VKEY_UNKNOWN, EF_NONE);
        ch_event.set_character(ch);
        client->InsertChar(ch_event);
      }
    } else if (pending_commit_->text.empty()) {
      client->InsertText(
          u"", TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
      composing_text_ = false;
    } else {
      // Split the commit into two separate commits, one for the substring
      // before the cursor and one for the substring after.
      const std::u16string before_cursor =
          pending_commit_->text.substr(0, pending_commit_->cursor);
      if (!before_cursor.empty()) {
        client->InsertText(
            before_cursor,
            TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
      }
      const std::u16string after_cursor =
          pending_commit_->text.substr(pending_commit_->cursor);
      if (!after_cursor.empty()) {
        client->InsertText(
            after_cursor,
            TextInputClient::InsertTextCursorBehavior::kMoveCursorBeforeText);
      }
      composing_text_ = false;
    }
    typing_session_manager_.CommitCharacters(pending_commit_->text.length());
  }

  // TODO(https://crbug.com/952757): Refactor this code to be clearer and less
  // error-prone.
  if (composition_changed_ && !IsTextInputTypeNone()) {
    if (pending_composition_range_) {
      client->SetCompositionFromExistingText(
          pending_composition_range_->range,
          pending_composition_range_->text_spans);
    }
    if (pending_composition_) {
      composing_text_ = true;
      client->SetCompositionText(*pending_composition_);
    } else if (!pending_commit_ && !pending_composition_range_) {
      client->ClearCompositionText();
    }

    pending_composition_ = absl::nullopt;
    pending_composition_range_.reset();
  }

  if (pending_autocorrect_range_) {
    client->SetAutocorrectRange(*pending_autocorrect_range_);
    pending_autocorrect_range_.reset();
  }

  // We should not clear composition text here, as it may belong to the next
  // composition session.
  pending_commit_ = absl::nullopt;
  composition_changed_ = false;
}

bool InputMethodAsh::NeedInsertChar() const {
  return GetTextInputClient() &&
         (IsTextInputTypeNone() || (!composing_text_ && pending_commit_ &&
                                    pending_commit_->text.length() == 1 &&
                                    pending_commit_->cursor == 1));
}

bool InputMethodAsh::HasInputMethodResult() const {
  return pending_commit_ || composition_changed_;
}

void InputMethodAsh::CommitText(
    const std::u16string& text,
    TextInputClient::InsertTextCursorBehavior cursor_behavior) {
  // We need to receive input method result even if the text input type is
  // TEXT_INPUT_TYPE_NONE, to make sure we can always send correct
  // character for each key event to the focused text input client.
  if (!GetTextInputClient())
    return;

  if (!CanComposeInline()) {
    // Hides the candidate window for preedit text.
    UpdateCompositionText(CompositionText(), 0, false);
  }

  // Append the text to the buffer, because commit signal might be fired
  // multiple times when processing a key event.
  if (!pending_commit_) {
    pending_commit_ = PendingCommit();
  }
  pending_commit_->text.insert(pending_commit_->cursor, text);
  if (cursor_behavior ==
      TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText) {
    pending_commit_->cursor += text.length();
  }

  // If we are not handling key event, do not bother sending text result if the
  // focused text input client does not support text input.
  if (!handling_key_event_ && !IsTextInputTypeNone()) {
    if (!SendFakeProcessKeyEvent(true)) {
      GetTextInputClient()->InsertText(text, cursor_behavior);
      typing_session_manager_.CommitCharacters(text.length());
    }
    SendFakeProcessKeyEvent(false);
    pending_commit_ = absl::nullopt;
  }
}

void InputMethodAsh::UpdateCompositionText(const CompositionText& text,
                                           uint32_t cursor_pos,
                                           bool visible) {
  if (IsTextInputTypeNone())
    return;

  if (!CanComposeInline()) {
    ash::IMECandidateWindowHandlerInterface* candidate_window =
        ui::IMEBridge::Get()->GetCandidateWindowHandler();
    if (candidate_window)
      candidate_window->UpdatePreeditText(text.text, cursor_pos, visible);
  }

  // |visible| argument is very confusing. For example, what's the correct
  // behavior when:
  // 1. OnUpdatePreeditText() is called with a text and visible == false, then
  // 2. OnShowPreeditText() is called afterwards.
  //
  // If it's only for clearing the current preedit text, then why not just use
  // OnHidePreeditText()?
  if (!visible) {
    HidePreeditText();
    return;
  }

  pending_composition_ = ExtractCompositionText(text, cursor_pos);
  composition_changed_ = true;

  // In case OnShowPreeditText() is not called.
  if (pending_composition_->text.length())
    composing_text_ = true;

  if (!handling_key_event_) {
    // If we receive a composition text without pending key event, then we need
    // to send it to the focused text input client directly.
    if (!SendFakeProcessKeyEvent(true)) {
      GetTextInputClient()->SetCompositionText(*pending_composition_);
    }
    SendFakeProcessKeyEvent(false);
    composition_changed_ = false;
    pending_composition_ = absl::nullopt;
  }
}

void InputMethodAsh::HidePreeditText() {
  if (IsTextInputTypeNone())
    return;

  // Intentionally leaves |composing_text_| unchanged.
  composition_changed_ = true;
  pending_composition_ = absl::nullopt;

  if (!handling_key_event_) {
    TextInputClient* client = GetTextInputClient();
    if (client && client->HasCompositionText()) {
      if (!SendFakeProcessKeyEvent(true))
        client->ClearCompositionText();
      SendFakeProcessKeyEvent(false);
    }
    composition_changed_ = false;
  }
}

bool InputMethodAsh::CanComposeInline() const {
  TextInputClient* client = GetTextInputClient();
  return client ? client->CanComposeInline() : true;
}

bool InputMethodAsh::GetClientShouldDoLearning() const {
  TextInputClient* client = GetTextInputClient();
  return client && client->ShouldDoLearning();
}

int InputMethodAsh::GetTextInputFlags() const {
  TextInputClient* client = GetTextInputClient();
  return client ? client->GetTextInputFlags() : 0;
}

TextInputMode InputMethodAsh::GetTextInputMode() const {
  TextInputClient* client = GetTextInputClient();
  return client ? client->GetTextInputMode() : TEXT_INPUT_MODE_DEFAULT;
}

void InputMethodAsh::SendKeyEvent(KeyEvent* event) {
  ui::EventDispatchDetails details = DispatchKeyEvent(event);
  DCHECK(!details.dispatcher_destroyed);
}

SurroundingTextInfo InputMethodAsh::GetSurroundingTextInfo() {
  gfx::Range text_range;
  SurroundingTextInfo info;
  TextInputClient* client = GetTextInputClient();
  if (!client->GetTextRange(&text_range) ||
      !client->GetTextFromRange(text_range, &info.surrounding_text) ||
      !client->GetEditableSelectionRange(&info.selection_range)) {
    return SurroundingTextInfo();
  }
  // Makes the |selection_range| be relative to the |surrounding_text|.
  info.selection_range.set_start(info.selection_range.start() -
                                 text_range.start());
  info.selection_range.set_end(info.selection_range.end() - text_range.start());
  return info;
}

void InputMethodAsh::DeleteSurroundingText(int32_t offset, uint32_t length) {
  if (!GetTextInputClient())
    return;

  if (GetTextInputClient()->HasCompositionText())
    return;

  uint32_t before = offset >= 0 ? 0U : static_cast<uint32_t>(-1 * offset);
  GetTextInputClient()->ExtendSelectionAndDelete(before, length - before);
}

bool InputMethodAsh::ExecuteCharacterComposer(const ui::KeyEvent& event) {
  if (!character_composer_.FilterKeyPress(event))
    return false;

  // CharacterComposer consumed the key event. Update the composition text.
  CompositionText preedit;
  preedit.text = character_composer_.preedit_string();
  UpdateCompositionText(preedit, preedit.text.size(), !preedit.text.empty());
  const std::u16string& commit_text = character_composer_.composed_character();
  if (!commit_text.empty()) {
    CommitText(commit_text,
               TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  }
  return true;
}

CompositionText InputMethodAsh::ExtractCompositionText(
    const CompositionText& text,
    uint32_t cursor_position) const {
  CompositionText composition;
  composition.text = text.text;

  if (composition.text.empty())
    return composition;

  // ibus uses character index for cursor position and attribute range, but we
  // use char16 offset for them. So we need to do conversion here.
  std::vector<size_t> char16_offsets;
  size_t length = composition.text.length();
  for (base::i18n::UTF16CharIterator char_iterator(composition.text);
       !char_iterator.end(); char_iterator.Advance()) {
    char16_offsets.push_back(char_iterator.array_pos());
  }

  // The text length in Unicode characters.
  auto char_length = static_cast<uint32_t>(char16_offsets.size());
  // Make sure we can convert the value of |char_length| as well.
  char16_offsets.push_back(length);

  size_t cursor_offset = char16_offsets[std::min(char_length, cursor_position)];

  composition.selection = gfx::Range(cursor_offset);

  const ImeTextSpans text_ime_text_spans = text.ime_text_spans;
  if (!text_ime_text_spans.empty()) {
    for (const auto& text_ime_text_span : text_ime_text_spans) {
      const uint32_t start = text_ime_text_span.start_offset;
      const uint32_t end = text_ime_text_span.end_offset;
      if (start >= end)
        continue;
      ImeTextSpan ime_text_span(ui::ImeTextSpan::Type::kComposition,
                                char16_offsets[start], char16_offsets[end],
                                text_ime_text_span.thickness,
                                ui::ImeTextSpan::UnderlineStyle::kSolid,
                                text_ime_text_span.background_color);
      ime_text_span.underline_color = text_ime_text_span.underline_color;
      composition.ime_text_spans.push_back(ime_text_span);
    }
  }

  DCHECK(text.selection.start() <= text.selection.end());
  if (text.selection.start() < text.selection.end()) {
    const uint32_t start = text.selection.start();
    const uint32_t end = text.selection.end();
    ImeTextSpan ime_text_span(
        ui::ImeTextSpan::Type::kComposition, char16_offsets[start],
        char16_offsets[end], ui::ImeTextSpan::Thickness::kThick,
        ui::ImeTextSpan::UnderlineStyle::kSolid, SK_ColorTRANSPARENT);
    composition.ime_text_spans.push_back(ime_text_span);

    // If the cursor is at start or end of this ime_text_span, then we treat
    // it as the selection range as well, but make sure to set the cursor
    // position to the selection end.
    if (ime_text_span.start_offset == cursor_offset) {
      composition.selection.set_start(ime_text_span.end_offset);
      composition.selection.set_end(cursor_offset);
    } else if (ime_text_span.end_offset == cursor_offset) {
      composition.selection.set_start(ime_text_span.start_offset);
      composition.selection.set_end(cursor_offset);
    }
  }

  // Use a thin underline with text color by default.
  if (composition.ime_text_spans.empty()) {
    composition.ime_text_spans.push_back(ImeTextSpan(
        ui::ImeTextSpan::Type::kComposition, 0, length,
        ui::ImeTextSpan::Thickness::kThin,
        ui::ImeTextSpan::UnderlineStyle::kSolid, SK_ColorTRANSPARENT));
  }

  return composition;
}

bool InputMethodAsh::IsPasswordOrNoneInputFieldFocused() {
  TextInputType type = GetTextInputType();
  return type == TEXT_INPUT_TYPE_NONE || type == TEXT_INPUT_TYPE_PASSWORD;
}

TextInputClient::FocusReason InputMethodAsh::GetClientFocusReason() const {
  TextInputClient* client = GetTextInputClient();
  return client ? client->GetFocusReason() : TextInputClient::FOCUS_REASON_NONE;
}

bool InputMethodAsh::HasCompositionText() {
  TextInputClient* client = GetTextInputClient();
  return client && client->HasCompositionText();
}

std::u16string InputMethodAsh::GetCompositionText() {
  TextInputClient* client = GetTextInputClient();
  if (!client) {
    return u"";
  }

  gfx::Range composition_range;
  client->GetCompositionTextRange(&composition_range);
  std::u16string composition_text;
  client->GetTextFromRange(composition_range, &composition_text);

  return composition_text;
}

ukm::SourceId InputMethodAsh::GetClientSourceForMetrics() {
  TextInputClient* client = GetTextInputClient();
  return client ? client->GetClientSourceForMetrics() : ukm::kInvalidSourceId;
}

InputMethod* InputMethodAsh::GetInputMethod() {
  return this;
}

}  // namespace ui
