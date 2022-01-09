// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/controller.h"

#include <memory>
#include <utility>

#include "base/callback_helpers.h"
#include "base/containers/flat_map.h"
#include "base/guid.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/gtest_util.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill_assistant/browser/cud_condition.pb.h"
#include "components/autofill_assistant/browser/device_context.h"
#include "components/autofill_assistant/browser/features.h"
#include "components/autofill_assistant/browser/mock_autofill_assistant_tts_controller.h"
#include "components/autofill_assistant/browser/mock_client.h"
#include "components/autofill_assistant/browser/mock_controller_observer.h"
#include "components/autofill_assistant/browser/mock_personal_data_manager.h"
#include "components/autofill_assistant/browser/public/mock_runtime_manager.h"
#include "components/autofill_assistant/browser/service/mock_service.h"
#include "components/autofill_assistant/browser/service/service.h"
#include "components/autofill_assistant/browser/test_util.h"
#include "components/autofill_assistant/browser/trigger_context.h"
#include "components/autofill_assistant/browser/web/mock_web_controller.h"
#include "components/strings/grit/components_strings.h"
#include "components/ukm/content/source_url_recorder.h"
#include "components/ukm/test_ukm_recorder.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "net/http/http_status_code.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/common/features.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill_assistant {

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;
using ::testing::WithArgs;

namespace {

constexpr char kClientLocale[] = "en-US";

// Same as non-mock, but provides default mock callbacks.
struct MockCollectUserDataOptions : public CollectUserDataOptions {
  MockCollectUserDataOptions() {
    base::MockOnceCallback<void(UserData*, const UserModel*)>
        mock_confirm_callback;
    confirm_callback = mock_confirm_callback.Get();
    base::MockOnceCallback<void(int, UserData*, const UserModel*)>
        mock_actions_callback;
    additional_actions_callback = mock_actions_callback.Get();
    base::MockOnceCallback<void(int, UserData*, const UserModel*)>
        mock_terms_callback;
    terms_link_callback = mock_terms_callback.Get();
    selected_user_data_changed_callback = base::DoNothing();
  }
};

}  // namespace

class ControllerTest : public testing::Test {
 public:
  ControllerTest() {
    scoped_feature_list_.InitAndEnableFeature(
        features::kAutofillAssistantChromeEntry);
  }

  void SetUp() override {
    web_contents_ = content::WebContentsTester::CreateTestWebContents(
        &browser_context_, nullptr);
    auto web_controller = std::make_unique<NiceMock<MockWebController>>();
    mock_web_controller_ = web_controller.get();
    auto service = std::make_unique<NiceMock<MockService>>();
    mock_service_ = service.get();
    auto tts_controller =
        std::make_unique<NiceMock<MockAutofillAssistantTtsController>>();
    mock_tts_controller_ = tts_controller.get();
    ukm::InitializeSourceUrlRecorderForWebContents(web_contents_.get());

    ON_CALL(mock_client_, GetWebContents).WillByDefault(Return(web_contents()));
    ON_CALL(mock_client_, HasHadUI()).WillByDefault(Return(true));
    ON_CALL(mock_client_, GetLocale()).WillByDefault(Return(kClientLocale));

    mock_runtime_manager_ = std::make_unique<MockRuntimeManager>();
    controller_ = std::make_unique<Controller>(
        web_contents(), &mock_client_, task_environment()->GetMockTickClock(),
        mock_runtime_manager_->GetWeakPtr(), std::move(service),
        std::move(tts_controller), &ukm_recorder_,
        /* annotate_dom_model_service= */ nullptr);
    controller_->SetWebControllerForTest(std::move(web_controller));

    ON_CALL(mock_client_, AttachUI()).WillByDefault(Invoke([this]() {
      controller_->SetUiShown(true);
    }));

    ON_CALL(mock_client_, DestroyUI()).WillByDefault(Invoke([this]() {
      controller_->SetUiShown(false);
    }));

    // Fetching scripts succeeds for all URLs, but return nothing.
    ON_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillByDefault(RunOnceCallback<2>(net::HTTP_OK, ""));

    // Scripts run, but have no actions.
    ON_CALL(*mock_service_, OnGetActions(_, _, _, _, _, _))
        .WillByDefault(RunOnceCallback<5>(net::HTTP_OK, ""));

    ON_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _, _))
        .WillByDefault(RunOnceCallback<5>(net::HTTP_OK, ""));

    ON_CALL(*mock_web_controller_, FindElement(_, _, _))
        .WillByDefault(RunOnceCallback<2>(ClientStatus(), nullptr));

    ON_CALL(mock_observer_, OnStateChanged(_))
        .WillByDefault(Invoke([this](AutofillAssistantState state) {
          states_.emplace_back(state);
        }));
    ON_CALL(mock_observer_, OnKeyboardSuppressionStateChanged(_))
        .WillByDefault(Invoke(
            [this](bool state) { keyboard_states_.emplace_back(state); }));
    controller_->AddObserver(&mock_observer_);
  }

  void TearDown() override {
    controller_->RemoveObserver(&mock_observer_);
    controller_.reset();
  }

  content::WebContents* web_contents() { return web_contents_.get(); }

  content::BrowserTaskEnvironment* task_environment() {
    return &task_environment_;
  }

 protected:
  static SupportedScriptProto* AddRunnableScript(
      SupportsScriptResponseProto* response,
      const std::string& name_and_path,
      bool direct_action = true) {
    SupportedScriptProto* script = response->add_scripts();
    script->set_path(name_and_path);
    if (direct_action) {
      script->mutable_presentation()->mutable_direct_action()->add_names(
          name_and_path);
    }
    return script;
  }

  void SetupScripts(SupportsScriptResponseProto scripts) {
    std::string scripts_str;
    scripts.SerializeToString(&scripts_str);
    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillOnce(RunOnceCallback<2>(net::HTTP_OK, scripts_str));
  }

  void SetupActionsForScript(const std::string& path,
                             ActionsResponseProto actions_response) {
    std::string actions_response_str;
    actions_response.SerializeToString(&actions_response_str);
    EXPECT_CALL(*mock_service_, OnGetActions(StrEq(path), _, _, _, _, _))
        .WillOnce(RunOnceCallback<5>(net::HTTP_OK, actions_response_str));
  }

  void Start() { Start("http://initialurl.com"); }

  void Start(const std::string& url_string) {
    Start(url_string, std::make_unique<TriggerContext>());
  }

  void Start(const std::string& url_string,
             std::unique_ptr<TriggerContext> trigger_context) {
    GURL url(url_string);
    SetLastCommittedUrl(url);
    controller_->Start(url, std::move(trigger_context));
  }

  void Track() {
    SetLastCommittedUrl(GURL("http://initialurl.com"));
    controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  }

  void SetLastCommittedUrl(const GURL& url) {
    content::WebContentsTester::For(web_contents())->SetLastCommittedURL(url);
  }

  void SimulateNavigateToUrl(const GURL& url) {
    SetLastCommittedUrl(url);
    content::NavigationSimulator::NavigateAndCommitFromDocument(
        url, web_contents()->GetMainFrame());
    content::WebContentsTester::For(web_contents())->TestSetIsLoading(false);
    controller_->DidFinishLoad(nullptr, GURL(""));
  }

  void SimulateWebContentsFocused() {
    controller_->OnWebContentsFocused(nullptr);
  }

  // Sets up the next call to the service for scripts to return |response|.
  void SetNextScriptResponse(const SupportsScriptResponseProto& response) {
    std::string response_str;
    response.SerializeToString(&response_str);

    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));
  }

  // Sets up all calls to the service for scripts to return |response|.
  void SetRepeatedScriptResponse(const SupportsScriptResponseProto& response) {
    std::string response_str;
    response.SerializeToString(&response_str);

    EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
        .WillRepeatedly(RunOnceCallback<2>(net::HTTP_OK, response_str));
  }

  UserData* GetUserData() { return &controller_->user_data_; }

  UiDelegate* GetUiDelegate() { return controller_.get(); }

  void SetNavigatingToNewDocument(bool value) {
    controller_->navigating_to_new_document_ = value;
  }

  RequiredDataPiece MakeRequiredDataPiece(autofill::ServerFieldType field) {
    RequiredDataPiece required_data_piece;
    required_data_piece.mutable_condition()->set_key(static_cast<int>(field));
    required_data_piece.mutable_condition()->mutable_not_empty();
    return required_data_piece;
  }

  void EnableTtsForTest() { controller_->tts_enabled_ = true; }

  void SetTtsButtonStateForTest(TtsButtonState state) {
    controller_->tts_button_state_ = state;
  }

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  content::RenderViewHostTestEnabler rvh_test_enabler_;
  content::TestBrowserContext browser_context_;
  std::unique_ptr<content::WebContents> web_contents_;
  base::test::ScopedFeatureList scoped_feature_list_;
  base::TimeTicks now_;
  std::vector<AutofillAssistantState> states_;
  std::vector<bool> keyboard_states_;
  raw_ptr<MockService> mock_service_;
  raw_ptr<MockWebController> mock_web_controller_;
  raw_ptr<MockAutofillAssistantTtsController> mock_tts_controller_;
  NiceMock<MockClient> mock_client_;
  std::unique_ptr<MockRuntimeManager> mock_runtime_manager_;
  NiceMock<MockControllerObserver> mock_observer_;
  ukm::TestAutoSetUkmRecorder ukm_recorder_;
  std::unique_ptr<Controller> controller_;
};

struct NavigationState {
  bool navigating = false;
  bool has_errors = false;

  bool operator==(const NavigationState& other) const {
    return navigating == other.navigating && has_errors == other.has_errors;
  }
};

std::ostream& operator<<(std::ostream& out, const NavigationState& state) {
  out << "{navigating=" << state.navigating << ","
      << "has_errors=" << state.has_errors << "}";
  return out;
}

// A Listener that keeps track of the reported state of the delegate captured
// from OnNavigationStateChanged.
class NavigationStateChangeListener
    : public ScriptExecutorDelegate::NavigationListener {
 public:
  explicit NavigationStateChangeListener(ScriptExecutorDelegate* delegate)
      : delegate_(delegate) {}
  ~NavigationStateChangeListener() override;
  void OnNavigationStateChanged() override;

  std::vector<NavigationState> events;

 private:
  const raw_ptr<ScriptExecutorDelegate> delegate_;
};

NavigationStateChangeListener::~NavigationStateChangeListener() {}

void NavigationStateChangeListener::OnNavigationStateChanged() {
  NavigationState state;
  state.navigating = delegate_->IsNavigatingToNewDocument();
  state.has_errors = delegate_->HasNavigationError();
  events.emplace_back(state);
}

class ScriptExecutorListener : public ScriptExecutorDelegate::Listener {
 public:
  explicit ScriptExecutorListener() = default;
  ~ScriptExecutorListener() override;

  void OnPause(const std::string& message,
               const std::string& button_label) override;

  int pause_count = 0;
};

ScriptExecutorListener::~ScriptExecutorListener() {}

void ScriptExecutorListener::OnPause(const std::string& message,
                                     const std::string& button_label) {
  ++pause_count;
}

TEST_F(ControllerTest, ReportDirectActions) {
  SupportsScriptResponseProto script_response;

  AddRunnableScript(&script_response, "action");

  SetNextScriptResponse(script_response);

  testing::InSequence seq;

  Track();

  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(),
              UnorderedElementsAre(AllOf(
                  Field(&ScriptHandle::direct_action,
                        Field(&DirectAction::names, ElementsAre("action"))))));
}

TEST_F(ControllerTest, RunDirectActionWithArguments) {
  SupportsScriptResponseProto script_response;

  // script is available as a direct action.
  auto* script1 = AddRunnableScript(&script_response, "action");
  auto* action = script1->mutable_presentation()->mutable_direct_action();
  action->add_required_arguments("required");
  action->add_optional_arguments("arg0");
  action->add_optional_arguments("arg1");

  SetNextScriptResponse(script_response);

  testing::InSequence seq;

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());

  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(),
              ElementsAre(Field(
                  &ScriptHandle::direct_action,
                  AllOf(Field(&DirectAction::names, ElementsAre("action")),
                        Field(&DirectAction::required_arguments,
                              ElementsAre("required")),
                        Field(&DirectAction::optional_arguments,
                              ElementsAre("arg0", "arg1"))))));

  EXPECT_CALL(*mock_service_, OnGetActions("action", _, _, _, _, _))
      .WillOnce(Invoke([](const std::string& script_path, const GURL& url,
                          const TriggerContext& trigger_context,
                          const std::string& global_payload,
                          const std::string& script_payload,
                          Service::ResponseCallback& callback) {
        EXPECT_THAT(trigger_context.GetScriptParameters().ToProto(),
                    testing::UnorderedElementsAreArray(
                        base::flat_map<std::string, std::string>(
                            {{"required", "value"}, {"arg0", "value0"}})));
        EXPECT_TRUE(trigger_context.GetDirectAction());

        std::move(callback).Run(true, "");
      }));

  TriggerContext::Options options;
  options.is_direct_action = true;
  EXPECT_TRUE(controller_->PerformDirectAction(
      0, std::make_unique<TriggerContext>(
             /* parameters = */ std::make_unique<ScriptParameters>(
                 base::flat_map<std::string, std::string>{{"required", "value"},
                                                          {"arg0", "value0"}}),
             options)));
}

TEST_F(ControllerTest, NoScripts) {
  SupportsScriptResponseProto empty;
  SetNextScriptResponse(empty);

  EXPECT_CALL(mock_client_,
              RecordDropOut(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, NoRelevantScripts) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_domain("http://otherdomain.com");
  SetNextScriptResponse(script_response);

  EXPECT_CALL(mock_client_,
              RecordDropOut(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, NoRelevantScriptYet) {
  SupportsScriptResponseProto script_response;
  *AddRunnableScript(&script_response, "no_match_yet")
       ->mutable_presentation()
       ->mutable_precondition()
       ->mutable_element_condition()
       ->mutable_match() = ToSelectorProto("#element");
  SetNextScriptResponse(script_response);

  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
}

TEST_F(ControllerTest, ClearUserActionsOnSelection) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);

  ActionsResponseProto runnable_script;
  auto* prompt_action = runnable_script.add_actions()->mutable_prompt();
  prompt_action->add_choices()->mutable_chip()->set_text("continue");
  prompt_action->add_choices()->mutable_chip()->set_text("other");

  SetupActionsForScript("runnable", runnable_script);
  SetNextScriptResponse(script_response);

  {
    testing::InSequence seq;
    // User actions are cleared when the script is executed.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0)));
    // The prompt aciton has 2 chips.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(2)));
    // When one chip is selected the user actions are cleared.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0)));
    // This test doesn't specify what happens after that.
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(_)).Times(AnyNumber());
  }
  Start();
  EXPECT_TRUE(controller_->PerformUserAction(0));
}

TEST_F(ControllerTest, ClearDirectActionsWhenRunning) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  AddRunnableScript(&script_response, "script2");

  ActionsResponseProto runnable_script;
  auto* prompt_action = runnable_script.add_actions()->mutable_prompt();
  prompt_action->add_choices()->mutable_chip()->set_text("continue");

  SetupActionsForScript("script1", runnable_script);
  SetNextScriptResponse(script_response);

  Track();
  // We initially have 2 direct action scripts available.
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(2));
  // We execute one of them.
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  // There are no direct actions available once the script is running.
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(0));
}

TEST_F(ControllerTest, ScriptStartMessage) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "script");
  script->mutable_presentation()->set_start_message("Starting Script...");
  SetNextScriptResponse(script_response);

  ActionsResponseProto script_actions;
  script_actions.add_actions()->mutable_tell()->set_message("Script running.");
  SetupActionsForScript("script", script_actions);

  Start("http://a.example.com/path");

  {
    testing::InSequence seq;
    EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Starting Script..."));
    EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Script running."));
  }
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
}

TEST_F(ControllerTest, UpdateClientSettings) {
  SupportsScriptResponseProto script_response;
  ClientSettingsProto* initial_client_settings_proto =
      script_response.mutable_client_settings();
  initial_client_settings_proto->set_periodic_script_check_interval_ms(1);
  initial_client_settings_proto->set_display_strings_locale("en-US");
  ClientSettingsProto::DisplayString* initial_display_string;
  for (int i = 0; i < ClientSettingsProto::DisplayStringId_MAX + 1; i++) {
    initial_display_string =
        initial_client_settings_proto->add_display_strings();
    initial_display_string->set_id(
        static_cast<ClientSettingsProto::DisplayStringId>(i));
    initial_display_string->set_value("us_test");
  }
  ClientSettings initial_client_settings;
  initial_client_settings.UpdateFromProto(*initial_client_settings_proto);

  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  ClientSettingsProto* changed_client_settings_proto =
      actions_response.add_actions()
          ->mutable_update_client_settings()
          ->mutable_client_settings();
  changed_client_settings_proto->set_display_strings_locale("fr-FR");
  ClientSettingsProto::DisplayString* changed_display_string;
  for (int i = 0; i < ClientSettingsProto::DisplayStringId_MAX + 1; i++) {
    changed_display_string =
        changed_client_settings_proto->add_display_strings();
    changed_display_string->set_id(
        static_cast<ClientSettingsProto::DisplayStringId>(i));
    changed_display_string->set_value("fr_test");
  }
  ClientSettings changed_client_settings;
  changed_client_settings.UpdateFromProto(*changed_client_settings_proto);

  SetupActionsForScript("script", actions_response);

  EXPECT_CALL(mock_observer_,
              OnStatusMessageChanged(l10n_util::GetStringFUTF8(
                  IDS_AUTOFILL_ASSISTANT_LOADING, u"a.example.com")))
      .Times(1);
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_,
              OnClientSettingsChanged(
                  AllOf(Field(&ClientSettings::periodic_script_check_interval,
                              base::Milliseconds(1)),
                        Field(&ClientSettings::display_strings_locale, "en-US"),
                        Field(&ClientSettings::display_strings,
                              initial_client_settings.display_strings))))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnClientSettingsChanged(
                  AllOf(Field(&ClientSettings::periodic_script_check_interval,
                              base::Milliseconds(1)),
                        Field(&ClientSettings::display_strings_locale, "fr-FR"),
                        Field(&ClientSettings::display_strings,
                              changed_client_settings.display_strings))))
      .Times(1);
  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetSettings(),
              AllOf(Field(&ClientSettings::periodic_script_check_interval,
                          base::Milliseconds(1)),
                    Field(&ClientSettings::display_strings_locale, "fr-FR"),
                    Field(&ClientSettings::display_strings,
                          changed_client_settings.display_strings)));
}

TEST_F(ControllerTest, Stop) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "stop");
  SetNextScriptResponse(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_stop();
  std::string actions_response_str;
  actions_response.SerializeToString(&actions_response_str);
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("stop"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_OK, actions_response_str));

  Start();
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  testing::InSequence seq;
  EXPECT_CALL(mock_client_, Shutdown(Metrics::DropOutReason::SCRIPT_SHUTDOWN));
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
}

TEST_F(ControllerTest, CloseCustomTab) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "stop");
  SetNextScriptResponse(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_stop()->set_close_cct(true);
  std::string actions_response_str;
  actions_response.SerializeToString(&actions_response_str);
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("stop"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_OK, actions_response_str));

  Start();
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));
  EXPECT_CALL(mock_observer_, CloseCustomTab()).Times(1);

  testing::InSequence seq;
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::CUSTOM_TAB_CLOSED));
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
}

TEST_F(ControllerTest, StopWithFeedbackChip) {
  SupportsScriptResponseProto script_response;
  script_response.mutable_client_settings()->set_display_strings_locale(
      "en-US");
  ClientSettingsProto::DisplayString* display_str =
      script_response.mutable_client_settings()->add_display_strings();
  display_str->set_id(ClientSettingsProto::SEND_FEEDBACK);
  display_str->set_value("send_feedback");
  AddRunnableScript(&script_response, "stop");
  SetNextScriptResponse(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_tell()->set_message("I give up");
  actions_response.add_actions()->mutable_stop()->set_show_feedback_chip(true);
  std::string actions_response_str;
  actions_response.SerializeToString(&actions_response_str);
  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("stop"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_OK, actions_response_str));

  Start();
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  testing::InSequence seq;
  EXPECT_CALL(mock_client_,
              RecordDropOut(Metrics::DropOutReason::SCRIPT_SHUTDOWN));
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_THAT(
      controller_->GetUserActions(),
      ElementsAre(Property(&UserAction::chip,
                           AllOf(Field(&Chip::type, FEEDBACK_ACTION),
                                 Field(&Chip::text, "send_feedback")))));
}

TEST_F(ControllerTest, RefreshScriptWhenDomainChanges) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  std::string scripts_str;
  script_response.SerializeToString(&scripts_str);

  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(Eq(GURL("http://a.example.com/path1")), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, scripts_str));
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(Eq(GURL("http://b.example.com/path1")), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, scripts_str));

  Start("http://a.example.com/path1");
  SimulateNavigateToUrl(GURL("http://a.example.com/path2"));
  SimulateNavigateToUrl(GURL("http://b.example.com/path1"));
  SimulateNavigateToUrl(GURL("http://b.example.com/path2"));
}

TEST_F(ControllerTest, Autostart) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()->mutable_tell()->set_message("autostart");
  autostart_script.add_actions()->mutable_stop();
  SetupActionsForScript("autostart", autostart_script);

  EXPECT_CALL(mock_client_, AttachUI());
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Full history state transitions
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED));
  EXPECT_THAT(keyboard_states_, ElementsAre(true, true, false));
}

TEST_F(ControllerTest,
       AutostartFallbackWithNoRunnableScriptsShowsFeedbackChip) {
  SupportsScriptResponseProto script_response;
  auto* autostart = AddRunnableScript(&script_response, "runnable");
  autostart->mutable_presentation()->set_autostart(true);

  Start("http://a.example.com/path");
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(FEEDBACK_ACTION, controller_->GetUserActions().at(0).chip().type);
}

TEST_F(ControllerTest,
       AutostartErrorDoesNotShowFeedbackChipWithFeatureFlagDisabled) {
  // Disable the feedback chip feature.
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitAndDisableFeature(
      features::kAutofillAssistantFeedbackChip);

  SupportsScriptResponseProto script_response;
  auto* autostart =
      AddRunnableScript(&script_response, "runnable", /*direct_action=*/false);
  autostart->mutable_presentation()->set_autostart(true);
  SetRepeatedScriptResponse(script_response);

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0u)))
      .Times(AnyNumber());
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(Gt(0u)))).Times(0);

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetUserActions(), SizeIs(0));
}

TEST_F(ControllerTest, InitialUrlLoads) {
  GURL initialUrl("http://a.example.com/path");
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(Eq(initialUrl), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, ""));

  controller_->Start(initialUrl, std::make_unique<TriggerContext>());
}

TEST_F(ControllerTest, ProgressSetAtStart) {
  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(_));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  Start();
  EXPECT_EQ(0, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, SetProgressStep) {
  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(_));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  Start();

  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(_));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  controller_->SetStepProgressBarConfiguration(config);
  EXPECT_EQ(0, controller_->GetProgressActiveStep());

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(1));
  controller_->SetProgressActiveStep(1);
  EXPECT_EQ(1, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, IgnoreProgressStepDecreases) {
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  Start();

  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(_));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(Not(1)))
      .Times(AnyNumber());
  controller_->SetProgressActiveStep(2);
  controller_->SetProgressActiveStep(1);
}

TEST_F(ControllerTest, NewProgressStepConfigurationClampsStep) {
  Start();

  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  config.add_annotated_step_icons()->set_identifier("icon3");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(3));
  controller_->SetProgressActiveStep(3);
  EXPECT_EQ(3, controller_->GetProgressActiveStep());

  ShowProgressBarProto::StepProgressBarConfiguration new_config;
  new_config.add_annotated_step_icons()->set_identifier("icon1");
  new_config.add_annotated_step_icons()->set_identifier("icon2");
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(2));
  controller_->SetStepProgressBarConfiguration(new_config);
  EXPECT_EQ(2, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, ProgressStepWrapsNegativesToMax) {
  Start();

  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  config.add_annotated_step_icons()->set_identifier("icon3");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(3));
  controller_->SetProgressActiveStep(-1);
  EXPECT_EQ(3, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, ProgressStepClampsOverflowToMax) {
  Start();

  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  config.add_annotated_step_icons()->set_identifier("icon3");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(3));
  controller_->SetProgressActiveStep(std::numeric_limits<int>::max());
  EXPECT_EQ(3, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, SetProgressStepFromIdentifier) {
  Start();

  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(1));
  EXPECT_TRUE(controller_->SetProgressActiveStepIdentifier("icon2"));
  EXPECT_EQ(1, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, SetProgressStepFromUnknownIdentifier) {
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  Start();
  EXPECT_EQ(0, controller_->GetProgressActiveStep());

  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(_));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(0));
  ShowProgressBarProto::StepProgressBarConfiguration config;
  config.add_annotated_step_icons()->set_identifier("icon1");
  config.add_annotated_step_icons()->set_identifier("icon2");
  controller_->SetStepProgressBarConfiguration(config);

  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(_)).Times(0);
  EXPECT_FALSE(controller_->SetProgressActiveStepIdentifier("icon3"));
  EXPECT_EQ(0, controller_->GetProgressActiveStep());
}

TEST_F(ControllerTest, AttachUIWhenStarting) {
  EXPECT_CALL(mock_client_, AttachUI());
  Start();
}

TEST_F(ControllerTest, AttachUIWhenContentsFocused) {
  SimulateWebContentsFocused();  // must not call AttachUI

  testing::InSequence seq;
  EXPECT_CALL(mock_client_, AttachUI());

  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script1");
  SetNextScriptResponse(script_response);
  Start();  // must call AttachUI

  EXPECT_CALL(mock_client_, AttachUI());
  SimulateWebContentsFocused();  // must call AttachUI

  EXPECT_CALL(mock_client_, AttachUI());
  controller_->OnFatalError("test", /*show_feedback_chip= */ false,
                            Metrics::DropOutReason::TAB_CHANGED);
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
  SimulateWebContentsFocused();  // must call AttachUI
}

TEST_F(ControllerTest, KeepCheckingForElement) {
  SupportsScriptResponseProto script_response;
  *AddRunnableScript(&script_response, "no_match_yet")
       ->mutable_presentation()
       ->mutable_precondition()
       ->mutable_element_condition()
       ->mutable_match() = ToSelectorProto("#element");
  SetNextScriptResponse(script_response);

  Track();
  // No scripts yet; the element doesn't exit.
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(0));

  for (int i = 0; i < 3; i++) {
    task_environment()->FastForwardBy(base::Seconds(1));
    EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(0));
  }

  EXPECT_CALL(*mock_web_controller_, FindElement(_, _, _))
      .WillRepeatedly(WithArgs<2>([](auto&& callback) {
        std::move(callback).Run(OkClientStatus(),
                                std::make_unique<ElementFinder::Result>());
      }));
  task_environment()->FastForwardBy(base::Seconds(1));

  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));
}

TEST_F(ControllerTest, ScriptTimeoutError) {
  // Wait for #element to show up for will_never_match. After 25s, execute the
  // script on_timeout_error.
  SupportsScriptResponseProto script_response;
  *AddRunnableScript(&script_response, "will_never_match")
       ->mutable_presentation()
       ->mutable_precondition()
       ->mutable_element_condition()
       ->mutable_match() = ToSelectorProto("#element");
  script_response.mutable_script_timeout_error()->set_timeout_ms(30000);
  script_response.mutable_script_timeout_error()->set_script_path(
      "on_timeout_error");
  SetNextScriptResponse(script_response);

  // on_timeout_error stops everything with a custom error message.
  ActionsResponseProto on_timeout_error;
  on_timeout_error.add_actions()->mutable_tell()->set_message("I give up");
  on_timeout_error.add_actions()->mutable_stop();
  std::string on_timeout_error_str;
  on_timeout_error.SerializeToString(&on_timeout_error_str);
  EXPECT_CALL(*mock_service_,
              OnGetActions(StrEq("on_timeout_error"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_OK, on_timeout_error_str));

  Start("http://a.example.com/path");
  for (int i = 0; i < 30; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::Seconds(1));
  }
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
  EXPECT_EQ("I give up", controller_->GetStatusMessage());
}

TEST_F(ControllerTest, ScriptTimeoutWarning) {
  // Wait for #element to show up for will_never_match. After 10s, execute the
  // script on_timeout_error.
  SupportsScriptResponseProto script_response;
  *AddRunnableScript(&script_response, "will_never_match")
       ->mutable_presentation()
       ->mutable_precondition()
       ->mutable_element_condition()
       ->mutable_match() = ToSelectorProto("#element");
  script_response.mutable_script_timeout_error()->set_timeout_ms(4000);
  script_response.mutable_script_timeout_error()->set_script_path(
      "on_timeout_error");
  SetNextScriptResponse(script_response);

  // on_timeout_error displays an error message and terminates
  ActionsResponseProto on_timeout_error;
  on_timeout_error.add_actions()->mutable_tell()->set_message("This is slow");
  std::string on_timeout_error_str;
  on_timeout_error.SerializeToString(&on_timeout_error_str);
  EXPECT_CALL(*mock_service_,
              OnGetActions(StrEq("on_timeout_error"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_OK, on_timeout_error_str));

  Start("http://a.example.com/path");

  // Warning after 4s, script succeeds and the client continues to wait.
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::Seconds(1));
  }
  EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
  EXPECT_EQ("This is slow", controller_->GetStatusMessage());
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(AutofillAssistantState::STARTING, controller_->GetState());
    task_environment()->FastForwardBy(base::Seconds(1));
  }
}

TEST_F(ControllerTest, SuccessfulNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  controller_->RemoveNavigationListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, false}));
}

TEST_F(ControllerTest, FailedNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);
  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  controller_->RemoveNavigationListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_TRUE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, true}));
}

TEST_F(ControllerTest, NavigationWithRedirects) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);

  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://original.example.com/"), web_contents()->GetMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Start();
  EXPECT_TRUE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  simulator->Redirect(GURL("http://redirect.example.com/"));
  EXPECT_TRUE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  simulator->Commit();
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  controller_->RemoveNavigationListener(&listener);

  // Redirection should not be reported as a state change.
  EXPECT_THAT(listener.events, ElementsAre(NavigationState{true, false},
                                           NavigationState{false, false}));
}

TEST_F(ControllerTest, EventuallySuccessfulNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);
  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  controller_->RemoveNavigationListener(&listener);

  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  EXPECT_THAT(listener.events,
              ElementsAre(
                  // 1st navigation starts
                  NavigationState{true, false},
                  // 1st navigation fails
                  NavigationState{false, true},
                  // 2nd navigation starts, while in error state
                  NavigationState{true, true},
                  // 2nd navigation succeeds
                  NavigationState{false, false}));
}

TEST_F(ControllerTest, RemoveListener) {
  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());
  listener.events.clear();
  controller_->RemoveNavigationListener(&listener);

  content::NavigationSimulator::NavigateAndFailFromDocument(
      GURL("http://initialurl.com"), net::ERR_CONNECTION_TIMED_OUT,
      web_contents()->GetMainFrame());
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());

  EXPECT_THAT(listener.events, IsEmpty());
}

TEST_F(ControllerTest, DelayStartupIfLoading) {
  SetNavigatingToNewDocument(true);

  Start("http://a.example.com/");
  EXPECT_EQ(AutofillAssistantState::INACTIVE, controller_->GetState());
  EXPECT_EQ(controller_->GetDeeplinkURL().host(), "a.example.com");

  // Initial navigation.
  SimulateNavigateToUrl(GURL("http://b.example.com"));
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::STOPPED));
  EXPECT_EQ(controller_->GetDeeplinkURL().host(), "a.example.com");
  EXPECT_EQ(controller_->GetScriptURL().host(), "b.example.com");
  EXPECT_EQ(controller_->GetCurrentURL().host(), "b.example.com");

  // Navigation during the flow.
  SimulateNavigateToUrl(GURL("http://c.example.com"));
  EXPECT_EQ(controller_->GetDeeplinkURL().host(), "a.example.com");
  EXPECT_EQ(controller_->GetScriptURL().host(), "b.example.com");
  EXPECT_EQ(controller_->GetCurrentURL().host(), "c.example.com");
}

TEST_F(ControllerTest, WaitForNavigationActionTimesOut) {
  // A single script, with a wait_for_navigation action
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_expect_navigation();
  auto* action = actions_response.add_actions()->mutable_wait_for_navigation();
  action->set_timeout_ms(1000);
  SetupActionsForScript("script", actions_response);

  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&processed_actions_capture),
                      RunOnceCallback<5>(net::HTTP_OK, "")));

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Start script, which waits for some navigation event to happen after the
  // expect_navigation action has run..
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));

  // No navigation event happened within the action timeout and the script ends.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  task_environment()->FastForwardBy(base::Seconds(1));

  ASSERT_THAT(processed_actions_capture, SizeIs(2));
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[0].status());
  EXPECT_EQ(TIMED_OUT, processed_actions_capture[1].status());
}

TEST_F(ControllerTest, WaitForNavigationActionStartWithinTimeout) {
  // A single script, with a wait_for_navigation action
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_expect_navigation();
  auto* action = actions_response.add_actions()->mutable_wait_for_navigation();
  action->set_timeout_ms(1000);
  SetupActionsForScript("script", actions_response);

  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&processed_actions_capture),
                      RunOnceCallback<5>(net::HTTP_OK, "")));

  Start("http://a.example.com/path");
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Start script, which waits for some navigation event to happen after the
  // expect_navigation action has run..
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));

  // Navigation starts, but does not end, within the timeout.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://a.example.com/path"), web_contents()->GetMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Start();
  task_environment()->FastForwardBy(base::Seconds(1));

  // Navigation finishes and the script ends.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  simulator->Commit();

  ASSERT_THAT(processed_actions_capture, SizeIs(2));
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[0].status());
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[1].status());
}

TEST_F(ControllerTest, SetScriptStoreConfig) {
  // A single script, and its corresponding bundle info.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script");
  script_response.mutable_script_store_config()->set_bundle_path("bundle/path");
  script_response.mutable_script_store_config()->set_bundle_version(12);
  SetupScripts(script_response);

  ScriptStoreConfig script_store_config;
  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, SetScriptStoreConfig(_))
      .WillOnce(SaveArg<0>(&script_store_config));

  Start("http://a.example.com/path");
  controller_->GetDirectActionScripts();

  EXPECT_THAT(script_store_config.bundle_path(), Eq("bundle/path"));
  EXPECT_THAT(script_store_config.bundle_version(), Eq(12));
}

TEST_F(ControllerTest, InitialDataUrlDoesNotChange) {
  const std::string deeplink_url("http://initialurl.com/path");
  Start(deeplink_url);
  EXPECT_THAT(controller_->GetDeeplinkURL(), deeplink_url);
  EXPECT_THAT(controller_->GetCurrentURL(), deeplink_url);

  const std::string navigate_url("http://navigateurl.com/path");
  SimulateNavigateToUrl(GURL(navigate_url));
  EXPECT_THAT(controller_->GetDeeplinkURL().spec(), deeplink_url);
  EXPECT_THAT(controller_->GetCurrentURL().spec(), navigate_url);
}

TEST_F(ControllerTest, Track) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://b.example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, ""));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Execute the script, which requires showing the UI, then go back to tracking
  // mode
  EXPECT_CALL(mock_client_, AttachUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Move to a domain for which there are no scripts. This causes the controller
  // to stop.
  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::STOPPED));
  EXPECT_THAT(keyboard_states_, ElementsAre(false, true, false, false));

  // Shutdown once we've moved from domain b.example.com, for which we know
  // there are no scripts, to c.example.com, which we don't want to check.
  EXPECT_CALL(mock_client_, Shutdown(Metrics::DropOutReason::NO_SCRIPTS));
  SimulateNavigateToUrl(GURL("http://c.example.com/"));
}

TEST_F(ControllerTest, TrackScriptWithNoUI) {
  // The UI is never shown during this test.
  EXPECT_CALL(mock_client_, AttachUI()).Times(0);

  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  // Script does nothing
  ActionsResponseProto runnable_script;
  auto* hidden_tell = runnable_script.add_actions()->mutable_tell();
  hidden_tell->set_message("optional message");
  hidden_tell->set_needs_ui(false);
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackScriptShowUIOnTell) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(true);
  SetupScripts(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("error");
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(mock_client_, AttachUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // The last tell message should still be shown to the user.
  EXPECT_TRUE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, RunDirectActionWhileTrackingWithUi) {
  SupportsScriptResponseProto script_response;
  auto* script_needs_ui = AddRunnableScript(&script_response, "needs_ui");
  script_needs_ui->mutable_presentation()->set_needs_ui(true);

  auto* script_no_ui = AddRunnableScript(&script_response, "no_ui");
  script_no_ui->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  ActionsResponseProto needs_ui_script;
  needs_ui_script.add_actions()->mutable_tell()->set_message("error");
  needs_ui_script.add_actions()->mutable_stop();
  SetupActionsForScript("needs_ui", needs_ui_script);

  ActionsResponseProto no_ui_script;
  no_ui_script.add_actions()->mutable_stop();
  SetupActionsForScript("no_ui", no_ui_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(2));
  EXPECT_EQ(controller_->GetDirectActionScripts()[0].path, "needs_ui");

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(mock_client_, AttachUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // The last tell message should still be shown to the user.
  EXPECT_TRUE(controller_->NeedsUI());

  EXPECT_CALL(mock_client_, DestroyUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(1, std::make_unique<TriggerContext>()));

  // UI should have been cleared
  EXPECT_FALSE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackScriptClosesUI) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("hi");
  runnable_script.add_actions()
      ->mutable_wait_for_dom()
      ->mutable_wait_condition();
  runnable_script.add_actions()->mutable_stop();

  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(mock_client_, AttachUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // The tell action wasn't the last one before close, so UI should close when
  // the script is finished.
  EXPECT_FALSE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackScriptShowUIOnError) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(false);
  SetupScripts(script_response);

  // Running the script fails, due to a backend issue. The error message should
  // be shown.
  EXPECT_CALL(*mock_service_, OnGetActions(_, _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_UNAUTHORIZED, ""));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_FALSE(controller_->NeedsUI());
  EXPECT_CALL(mock_client_, AttachUI());
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // UI must remain visible for the user to see the error message.
  EXPECT_TRUE(controller_->NeedsUI());

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackContinuesAfterScriptError) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_CALL(*mock_service_, OnGetActions(StrEq("runnable"), _, _, _, _, _))
      .WillOnce(RunOnceCallback<5>(net::HTTP_UNAUTHORIZED, ""));

  // When the script fails, the controller transitions to STOPPED state, then
  // right away back to TRACKING state.
  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Check the full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, TrackReportsFirstSetOfScripts) {
  Service::ResponseCallback get_scripts_callback;
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
      .WillOnce(
          Invoke([&get_scripts_callback](const GURL& url,
                                         const TriggerContext& trigger_context,
                                         Service::ResponseCallback& callback) {
            get_scripts_callback = std::move(callback);
          }));

  SetLastCommittedUrl(GURL("http://example.com/"));
  bool first_check_done = false;
  controller_->Track(std::make_unique<TriggerContext>(),
                     base::BindOnce(
                         [](Controller* controller, bool* is_done) {
                           // User actions must have been set when this is
                           // called
                           EXPECT_THAT(controller->GetDirectActionScripts(),
                                       SizeIs(1));
                           *is_done = true;
                         },
                         base::Unretained(controller_.get()),
                         base::Unretained(&first_check_done)));
  EXPECT_FALSE(first_check_done);
  EXPECT_FALSE(controller_->HasRunFirstCheck());

  ASSERT_TRUE(get_scripts_callback);

  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  std::string response_str;
  script_response.SerializeToString(&response_str);
  std::move(get_scripts_callback).Run(net::HTTP_OK, response_str);

  EXPECT_TRUE(first_check_done);
  EXPECT_TRUE(controller_->HasRunFirstCheck());
}

TEST_F(ControllerTest, TrackReportsNoScripts) {
  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(std::make_unique<TriggerContext>(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsNoScriptsForNow) {
  SupportsScriptResponseProto script_response;
  *AddRunnableScript(&script_response, "no_match_yet")
       ->mutable_presentation()
       ->mutable_precondition()
       ->mutable_element_condition()
       ->mutable_match() = ToSelectorProto("#element");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(std::make_unique<TriggerContext>(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsNoScriptsForThePage) {
  // Having scripts for the domain but not for the current page is fatal in
  // STARTING or PROMPT mode, but not in TRACKING mode.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "no_match_yet")
      ->mutable_presentation()
      ->mutable_precondition()
      ->add_path_pattern("/otherpage.html");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  base::MockCallback<base::OnceCallback<void()>> callback;

  EXPECT_CALL(callback, Run());
  controller_->Track(std::make_unique<TriggerContext>(), callback.Get());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
}

TEST_F(ControllerTest, TrackReportsAlreadyDone) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  base::MockCallback<base::OnceCallback<void()>> callback;
  EXPECT_CALL(callback, Run());
  controller_->Track(std::make_unique<TriggerContext>(), callback.Get());
}

TEST_F(ControllerTest, TrackThenAutostart) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  AddRunnableScript(&script_response, "autostart", /*direct_action=*/false)
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()->mutable_tell()->set_message("autostart");
  autostart_script.add_actions()->mutable_stop();
  SetupActionsForScript("autostart", autostart_script);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("runnable");
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  EXPECT_CALL(mock_client_, AttachUI());
  Start("http://example.com/");
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  // Run "runnable", which then calls stop and ends. The controller should then
  // go back to TRACKING mode.
  controller_->PerformDirectAction(0, std::make_unique<TriggerContext>());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
  EXPECT_THAT(keyboard_states_,
              ElementsAre(false, true, true, false, true, false));
}

TEST_F(ControllerTest, BrowseStateStopsOnDifferentDomain) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(true);
  prompt->add_choices()->mutable_chip()->set_text("continue");
  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://b.example.com/"), _, _))
      .Times(0);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://c.example.com/"), _, _))
      .Times(0);

  Start("http://example.com/");
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://c.example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // go back.
  SetLastCommittedUrl(GURL("http://b.example.com"));
  content::NavigationSimulator::GoBack(web_contents());
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // Shut down once the user moves to a different domain
  EXPECT_CALL(
      mock_client_,
      RecordDropOut(Metrics::DropOutReason::DOMAIN_CHANGE_DURING_BROWSE_MODE));
  SimulateNavigateToUrl(GURL("http://other-example.com/"));
}

TEST_F(ControllerTest, BrowseStateWithDomainAllowlist) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(true);
  *prompt->add_browse_domains_allowlist() = "example.com";
  *prompt->add_browse_domains_allowlist() = "other-example.com";
  prompt->add_choices()->mutable_chip()->set_text("continue");
  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://a.example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  Start("http://a.example.com/");
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://sub.other-example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // go back.
  SetLastCommittedUrl(GURL("http://sub.other-example.com"));
  content::NavigationSimulator::GoBack(web_contents());
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // Same domain navigations as one of the allowed domains should not shut down
  // AA.
  SimulateNavigateToUrl(GURL("http://other-example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // Navigation to different domain should stop AA.
  EXPECT_CALL(
      mock_client_,
      RecordDropOut(Metrics::DropOutReason::DOMAIN_CHANGE_DURING_BROWSE_MODE));
  SimulateNavigateToUrl(GURL("http://unknown.com"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, BrowseStateWithDomainAllowlistCleanup) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(true);
  *prompt->add_browse_domains_allowlist() = "example.com";
  prompt->add_choices()->mutable_chip()->set_text("continue");

  // Second browse action without an allowlist.
  auto* prompt2 = runnable_script.add_actions()->mutable_prompt();
  prompt2->set_browse_mode(true);
  prompt2->add_choices()->mutable_chip()->set_text("done");

  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://a.example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  Start("http://a.example.com/");
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  // Click "continue".
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");
  controller_->PerformUserAction(0);

  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "done");

  // Make sure the allowlist got reset with the second prompt action.
  EXPECT_CALL(
      mock_client_,
      RecordDropOut(Metrics::DropOutReason::DOMAIN_CHANGE_DURING_BROWSE_MODE));
  SimulateNavigateToUrl(GURL("http://c.example.com/"));
}

TEST_F(ControllerTest, PromptStateStopsOnGoBack) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(false);
  prompt->add_choices()->mutable_chip()->set_text("continue");
  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  Start("http://example.com/");
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://c.example.com/"));
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  // go back.
  EXPECT_CALL(mock_client_, RecordDropOut(Metrics::DropOutReason::NAVIGATION));
  SetLastCommittedUrl(GURL("http://b.example.com"));
  content::NavigationSimulator::GoBack(web_contents());
}

TEST_F(ControllerTest, PromptStateStopsOnRendererInitiatedBack) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(false);
  prompt->add_choices()->mutable_chip()->set_text("continue");
  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  Start("http://example.com/");
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://b.example.com/"));
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  SimulateNavigateToUrl(GURL("http://c.example.com/"));
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  // Go back, emulating a history navigation initiated from JS.
  EXPECT_CALL(mock_client_, RecordDropOut(Metrics::DropOutReason::NAVIGATION));
  SetLastCommittedUrl(GURL("http://b.example.com"));
  content::NavigationSimulator::CreateHistoryNavigation(
      -1, web_contents(), true /* is_renderer_initiated */)
      ->Commit();
}

TEST_F(ControllerTest, UnexpectedNavigationDuringPromptAction_Tracking) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable");
  SetNextScriptResponse(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  std::string never_shown = "never shown";
  runnable_script.add_actions()->mutable_tell()->set_message(never_shown);
  SetupActionsForScript("runnable", runnable_script);

  SetLastCommittedUrl(GURL("http://example.com/"));
  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));
  EXPECT_EQ(controller_->GetDirectActionScripts()[0].direct_action.names.count(
                "runnable"),
            1u);

  // Start the script, which should show a prompt with the continue chip.
  controller_->PerformDirectAction(0, std::make_unique<TriggerContext>());
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");

  // Browser (not document) initiated navigation while in prompt mode (such as
  // go back): The controller stops the scripts, shows an error, then goes back
  // to tracking mode.
  //
  // The tell never_shown which follows the prompt action should never be
  // executed.
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(never_shown)).Times(0);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(testing::Not(never_shown)))
      .Times(testing::AnyNumber());

  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://example.com/otherpage"));

  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));
  EXPECT_EQ(controller_->GetDirectActionScripts()[0].direct_action.names.count(
                "runnable"),
            1u);

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, UnexpectedNavigationDuringPromptAction) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  std::string never_shown = "never shown";
  autostart_script.add_actions()->mutable_tell()->set_message(never_shown);
  SetupActionsForScript("autostart", autostart_script);

  Start();
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");

  // Browser (not document) initiated navigation while in prompt mode (such as
  // go back): The controller stops the scripts, shows an error and shuts down.
  //
  // The tell never_shown which follows the prompt action should never be
  // executed.
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(never_shown)).Times(0);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(testing::Not(never_shown)))
      .Times(testing::AnyNumber());

  // Renderer (Document) initiated navigation is allowed.
  EXPECT_CALL(mock_client_, Shutdown(_)).Times(0);
  EXPECT_CALL(mock_client_, RecordDropOut(_)).Times(0);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://a.example.com/page"), web_contents()->GetMainFrame());
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  // Expected browser initiated navigation is allowed.
  EXPECT_CALL(mock_client_, Shutdown(_)).Times(0);
  EXPECT_CALL(mock_client_, RecordDropOut(_)).Times(0);
  controller_->ExpectNavigation();
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://b.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  // Unexpected browser initiated navigation will cause an error.
  EXPECT_CALL(mock_client_, RecordDropOut(Metrics::DropOutReason::NAVIGATION));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://c.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED));
}

TEST_F(ControllerTest, UnexpectedNavigationInRunningState) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  auto* wait_for_dom = autostart_script.add_actions()->mutable_wait_for_dom();
  wait_for_dom->set_timeout_ms(10000);
  wait_for_dom->mutable_wait_condition()
      ->mutable_match()
      ->add_filters()
      ->set_css_selector("#some-element");
  SetupActionsForScript("autostart", autostart_script);

  Start();
  EXPECT_EQ(AutofillAssistantState::RUNNING, controller_->GetState());

  // Document (not user) initiated navigation while in RUNNING state:
  // The controller keeps going.
  EXPECT_CALL(mock_client_, Shutdown(_)).Times(0);
  EXPECT_CALL(mock_client_, RecordDropOut(_)).Times(0);
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://a.example.com/page"), web_contents()->GetMainFrame());
  EXPECT_EQ(AutofillAssistantState::RUNNING, controller_->GetState());

  // Expected browser initiated navigation while in RUNNING state:
  // The controller keeps going.
  EXPECT_CALL(mock_client_, Shutdown(_)).Times(0);
  EXPECT_CALL(mock_client_, RecordDropOut(_)).Times(0);
  controller_->ExpectNavigation();
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://b.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::RUNNING, controller_->GetState());

  // Unexpected browser initiated navigation while in RUNNING state:
  // The controller stops the scripts, shows an error and shuts down.
  EXPECT_CALL(mock_client_,
              RecordDropOut(Metrics::DropOutReason::NAVIGATION_WHILE_RUNNING));
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(_));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://c.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::STOPPED));
}

TEST_F(ControllerTest, NavigationAfterStopped) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  std::string never_shown = "never shown";
  autostart_script.add_actions()->mutable_tell()->set_message(never_shown);
  SetupActionsForScript("autostart", autostart_script);

  Start();
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  // Unexpected browser initiated navigation will cause an error.
  EXPECT_CALL(mock_client_, RecordDropOut(Metrics::DropOutReason::NAVIGATION));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://a.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Another navigation will destroy the UI.
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::UI_CLOSED_UNEXPECTEDLY));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://b.example.com/page"));

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED));
}

TEST_F(ControllerTest, NavigationWhileTrackingWithUi) {
  SupportsScriptResponseProto script_response;
  auto* script = AddRunnableScript(&script_response, "runnable");
  script->mutable_presentation()->set_needs_ui(true);
  SetupScripts(script_response);

  ActionsResponseProto runnable_script;
  runnable_script.add_actions()->mutable_tell()->set_message("error");
  runnable_script.add_actions()->mutable_stop();
  SetupActionsForScript("runnable", runnable_script);

  // Start tracking at example.com, with one script matching
  SetLastCommittedUrl(GURL("http://example.com/"));

  controller_->Track(std::make_unique<TriggerContext>(), base::DoNothing());
  ASSERT_THAT(controller_->GetDirectActionScripts(), SizeIs(1));

  EXPECT_TRUE(
      controller_->PerformDirectAction(0, std::make_unique<TriggerContext>()));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_TRUE(controller_->NeedsUI());

  // Browser navigation will destroy the UI.
  EXPECT_CALL(mock_client_, DestroyUI());
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://a.example.com/page"));
  EXPECT_EQ(AutofillAssistantState::TRACKING, controller_->GetState());
  EXPECT_FALSE(controller_->NeedsUI());

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::TRACKING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::TRACKING));
}

TEST_F(ControllerTest, NavigationToGooglePropertyShutsDownDestroyingUI) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "autostart")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto autostart_script;
  autostart_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("continue");
  SetupActionsForScript("autostart", autostart_script);

  Start();
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());

  EXPECT_CALL(mock_client_, Shutdown(Metrics::DropOutReason::NAVIGATION));
  GURL google("https://google.com/search");
  SetLastCommittedUrl(google);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents(),
                                                             google);

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT));
}

TEST_F(ControllerTest,
       DomainChangeToGooglePropertyDuringBrowseShutsDownDestroyingUI) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  ActionsResponseProto runnable_script;
  auto* prompt = runnable_script.add_actions()->mutable_prompt();
  prompt->set_browse_mode(true);
  prompt->add_choices()->mutable_chip()->set_text("continue");
  SetupActionsForScript("runnable", runnable_script);
  std::string response_str;
  script_response.SerializeToString(&response_str);
  EXPECT_CALL(*mock_service_,
              OnGetScriptsForUrl(GURL("http://a.example.com/"), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, response_str));

  Start("http://a.example.com/");
  EXPECT_EQ(AutofillAssistantState::BROWSE, controller_->GetState());

  EXPECT_CALL(
      mock_client_,
      Shutdown(Metrics::DropOutReason::DOMAIN_CHANGE_DURING_BROWSE_MODE));
  GURL google("https://google.com/search");
  SetLastCommittedUrl(google);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents(),
                                                             google);

  // Full history of state transitions.
  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::BROWSE));
}

TEST_F(ControllerTest, UserDataFormEmpty) {
  auto options = std::make_unique<MockCollectUserDataOptions>();

  // Request nothing, expect continue button to be enabled.
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnCollectUserDataOptionsChanged(Not(nullptr)))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserDataChanged(_, UserData::FieldChange::ALL))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());
}

TEST_F(ControllerTest, UserDataFormContactInfo) {
  auto options = std::make_unique<MockCollectUserDataOptions>();

  options->required_contact_data_pieces.push_back(
      MakeRequiredDataPiece(autofill::ServerFieldType::NAME_FULL));
  options->required_contact_data_pieces.push_back(
      MakeRequiredDataPiece(autofill::ServerFieldType::EMAIL_ADDRESS));
  options->required_contact_data_pieces.push_back(MakeRequiredDataPiece(
      autofill::ServerFieldType::PHONE_HOME_WHOLE_NUMBER));
  options->contact_details_name = "selected_profile";

  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::CONTACT_PROFILE))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);

  autofill::AutofillProfile contact_profile;
  contact_profile.SetRawInfo(autofill::ServerFieldType::EMAIL_ADDRESS,
                             u"joedoe@example.com");
  contact_profile.SetRawInfo(autofill::ServerFieldType::NAME_FULL, u"Joe Doe");
  contact_profile.SetRawInfo(autofill::ServerFieldType::PHONE_HOME_WHOLE_NUMBER,
                             u"+1 23 456 789 01");
  controller_->SetContactInfo(
      std::make_unique<autofill::AutofillProfile>(contact_profile), UNKNOWN);
  EXPECT_THAT(controller_->GetUserData()
                  ->selected_address("selected_profile")
                  ->Compare(contact_profile),
              Eq(0));
}

TEST_F(ControllerTest, UserDataFormCreditCard) {
  auto options = std::make_unique<MockCollectUserDataOptions>();

  options->request_payment_method = true;
  options->billing_address_name = "billing_address";
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  // Credit card without billing address is invalid.
  auto credit_card = std::make_unique<autofill::CreditCard>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetCreditCardInfo(credit_card.get(), "Marion Mitchell",
                                    "4111 1111 1111 1111", "01", "2020",
                                    /* billing_address_id = */ "");
  EXPECT_CALL(mock_observer_, OnUserDataChanged(_, UserData::FieldChange::CARD))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::BILLING_ADDRESS))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCreditCard(
      std::make_unique<autofill::CreditCard>(*credit_card),
      /* billing_profile =*/nullptr, UNKNOWN);

  // Credit card with valid billing address is ok.
  auto billing_address = std::make_unique<autofill::AutofillProfile>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetProfileInfo(billing_address.get(), "Marion", "Mitchell",
                                 "Morrison", "marion@me.xyz", "Fox",
                                 "123 Zoo St.", "unit 5", "Hollywood", "CA",
                                 "91601", "US", "16505678910");
  credit_card->set_billing_address_id(billing_address->guid());
  EXPECT_CALL(mock_observer_, OnUserDataChanged(_, UserData::FieldChange::CARD))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::BILLING_ADDRESS))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetCreditCard(
      std::make_unique<autofill::CreditCard>(*credit_card),
      std::make_unique<autofill::AutofillProfile>(*billing_address), UNKNOWN);
  EXPECT_THAT(GetUserData()->selected_card()->Compare(*credit_card), Eq(0));
  EXPECT_THAT(GetUserData()
                  ->selected_address("billing_address")
                  ->Compare(*billing_address),
              Eq(0));
}

TEST_F(ControllerTest, UserDataChangesByOutOfLoopWrite) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  auto user_data = std::make_unique<UserData>();

  options->required_contact_data_pieces.push_back(
      MakeRequiredDataPiece(autofill::ServerFieldType::NAME_FULL));
  options->required_contact_data_pieces.push_back(
      MakeRequiredDataPiece(autofill::ServerFieldType::EMAIL_ADDRESS));
  options->required_contact_data_pieces.push_back(MakeRequiredDataPiece(
      autofill::ServerFieldType::PHONE_HOME_WHOLE_NUMBER));
  options->contact_details_name = "selected_profile";

  testing::InSequence sequence;

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  autofill::AutofillProfile contact_profile;
  contact_profile.SetRawInfo(autofill::ServerFieldType::EMAIL_ADDRESS,
                             u"joedoe@example.com");
  contact_profile.SetRawInfo(autofill::ServerFieldType::NAME_FULL, u"Joe Doe");
  contact_profile.SetRawInfo(autofill::ServerFieldType::PHONE_HOME_WHOLE_NUMBER,
                             u"+1 23 456 789 01");
  controller_->SetContactInfo(
      std::make_unique<autofill::AutofillProfile>(contact_profile), UNKNOWN);
  EXPECT_THAT(controller_->GetUserData()
                  ->selected_address("selected_profile")
                  ->Compare(contact_profile),
              Eq(0));

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  // Can be called by a PDM update.
  controller_->WriteUserData(base::BindLambdaForTesting(
      [this](UserData* user_data, UserData::FieldChange* field_change) {
        if (user_data->has_selected_address("selected_profile")) {
          controller_->GetUserModel()->SetSelectedAutofillProfile(
              "selected_profile", nullptr, user_data);
          *field_change = UserData::FieldChange::CONTACT_PROFILE;
        }
      }));
}

TEST_F(ControllerTest, UserDataFormReload) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  base::MockCallback<base::OnceCallback<void(UserData*)>> reload_callback;
  options->reload_data_callback = reload_callback.Get();
  base::MockCallback<
      base::RepeatingCallback<void(UserDataEventField, UserDataEventType)>>
      change_callback;
  options->selected_user_data_changed_callback = change_callback.Get();

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(change_callback, Run(UserDataEventField::CONTACT_EVENT,
                                   UserDataEventType::ENTRY_CREATED));
  EXPECT_CALL(reload_callback, Run);
  controller_->ReloadUserData(UserDataEventField::CONTACT_EVENT,
                              UserDataEventType::ENTRY_CREATED);
}

TEST_F(ControllerTest, SetTermsAndConditions) {
  auto options = std::make_unique<MockCollectUserDataOptions>();

  options->accept_terms_and_conditions_text.assign("Accept T&C");
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::TERMS_AND_CONDITIONS))
      .Times(1);
  controller_->SetTermsAndConditions(TermsAndConditionsState::ACCEPTED);
  EXPECT_THAT(controller_->GetUserData()->terms_and_conditions_,
              Eq(TermsAndConditionsState::ACCEPTED));
}

TEST_F(ControllerTest, SetLoginOption) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_login_choice = true;
  LoginChoice login_choice;
  login_choice.identifier = "guest";
  options->login_choices.push_back(login_choice);

  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::LOGIN_CHOICE))
      .Times(1);
  controller_->SetLoginOption("guest");
  EXPECT_THAT(controller_->GetUserData()->selected_login_choice()->identifier,
              Eq("guest"));
}

TEST_F(ControllerTest, SetShippingAddress) {
  auto options = std::make_unique<MockCollectUserDataOptions>();

  options->request_shipping = true;
  options->shipping_address_name = "shipping_address";
  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(false)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  auto shipping_address = std::make_unique<autofill::AutofillProfile>(
      base::GenerateGUID(), "https://www.example.com");
  autofill::test::SetProfileInfo(shipping_address.get(), "Marion", "Mitchell",
                                 "Morrison", "marion@me.xyz", "Fox",
                                 "123 Zoo St.", "unit 5", "Hollywood", "CA",
                                 "91601", "US", "16505678910");

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::SHIPPING_ADDRESS))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetShippingAddress(
      std::make_unique<autofill::AutofillProfile>(*shipping_address), UNKNOWN);
  EXPECT_THAT(GetUserData()
                  ->selected_address("shipping_address")
                  ->Compare(*shipping_address),
              Eq(0));
}

TEST_F(ControllerTest, SetAdditionalValues) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  ValueProto value1;
  value1.mutable_strings()->add_values("123456789");

  base::OnceCallback<void(UserData*, UserData::FieldChange*)> callback =
      base::BindLambdaForTesting(
          [&](UserData* user_data, UserData::FieldChange* change) {
            ValueProto value2;
            value2.mutable_strings()->add_values("");
            ValueProto value3;
            value3.mutable_strings()->add_values("");
            user_data->SetAdditionalValue("key1", value1);
            user_data->SetAdditionalValue("key2", value2);
            user_data->SetAdditionalValue("key3", value3);
            *change = UserData::FieldChange::ADDITIONAL_VALUES;
          });

  controller_->WriteUserData(std::move(callback));

  testing::InSequence seq;
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                  Property(&UserAction::enabled, Eq(true)))))
      .Times(1);
  controller_->SetCollectUserDataOptions(options.get());

  for (int i = 0; i < 2; ++i) {
    EXPECT_CALL(mock_observer_, OnUserActionsChanged(UnorderedElementsAre(
                                    Property(&UserAction::enabled, Eq(true)))))
        .Times(1);
    EXPECT_CALL(mock_observer_,
                OnUserDataChanged(_, UserData::FieldChange::ADDITIONAL_VALUES))
        .Times(1);
  }
  ValueProto value4;
  value4.mutable_strings()->add_values("value2");
  ValueProto value5;
  value5.mutable_strings()->add_values("value3");
  controller_->SetAdditionalValue("key2", value4);
  controller_->SetAdditionalValue("key3", value5);
  EXPECT_EQ(*controller_->GetUserData()->GetAdditionalValue("key1"), value1);
  EXPECT_EQ(*controller_->GetUserData()->GetAdditionalValue("key2"), value4);
  EXPECT_EQ(*controller_->GetUserData()->GetAdditionalValue("key3"), value5);

  ValueProto value6;
  value6.mutable_strings()->add_values("someValue");
  EXPECT_DCHECK_DEATH(controller_->SetAdditionalValue("key4", value6));
}

TEST_F(ControllerTest, SetOverlayColors) {
  EXPECT_CALL(
      mock_observer_,
      OnOverlayColorsChanged(AllOf(
          Field(&Controller::OverlayColors::background, StrEq("#FF000000")),
          Field(&Controller::OverlayColors::highlight_border,
                StrEq("#FFFFFFFF")))));

  GURL url("http://a.example.com/path");
  controller_->Start(url,
                     std::make_unique<TriggerContext>(
                         /* parameters = */ std::make_unique<ScriptParameters>(
                             base::flat_map<std::string, std::string>{
                                 {"OVERLAY_COLORS", "#FF000000:#FFFFFFFF"}}),
                         TriggerContext::Options()));
}

TEST_F(ControllerTest, EnableTts) {
  EXPECT_CALL(mock_client_, IsSpokenFeedbackAccessibilityServiceEnabled())
      .WillOnce(Return(false));
  EXPECT_CALL(mock_observer_, OnTtsButtonVisibilityChanged(true));

  GURL url("http://a.example.com/path");
  controller_->Start(
      url,
      std::make_unique<TriggerContext>(
          /* parameters = */ std::make_unique<ScriptParameters>(
              base::flat_map<std::string, std::string>{{"ENABLE_TTS", "true"}}),
          TriggerContext::Options()));

  EXPECT_TRUE(controller_->GetTtsButtonVisible());
}

TEST_F(ControllerTest, DoNotEnableTtsWhenAccessibilityEnabled) {
  EXPECT_CALL(mock_client_, IsSpokenFeedbackAccessibilityServiceEnabled())
      .WillOnce(Return(true));
  EXPECT_CALL(mock_observer_, OnTtsButtonVisibilityChanged(true)).Times(0);

  GURL url("http://a.example.com/path");
  controller_->Start(
      url,
      std::make_unique<TriggerContext>(
          /* parameters = */ std::make_unique<ScriptParameters>(
              base::flat_map<std::string, std::string>{{"ENABLE_TTS", "true"}}),
          TriggerContext::Options()));

  EXPECT_FALSE(controller_->GetTtsButtonVisible());
}

TEST_F(ControllerTest, TtsMessageIsSetCorrectlyAtStartup) {
  Start();
  EXPECT_EQ(controller_->GetTtsMessage(), controller_->GetStatusMessage());
  EXPECT_FALSE(controller_->GetTtsMessage().empty());
}

TEST_F(ControllerTest, TtsMessageIsSetCorrectly) {
  // SetStatusMessage should override tts_message
  controller_->SetStatusMessage("message");
  EXPECT_EQ(controller_->GetTtsMessage(), "message");

  controller_->SetTtsMessage("tts_message");
  EXPECT_EQ(controller_->GetTtsMessage(), "tts_message");
  EXPECT_EQ(controller_->GetStatusMessage(), "message");
}

TEST_F(ControllerTest, SetTtsMessageStopsAnyOngoingTts) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::PLAYING);

  EXPECT_CALL(*mock_tts_controller_, Stop());
  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  controller_->SetTtsMessage("tts_message");
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest, SetTtsMessageReEnablesTtsButtonWithNonStickyStateExp) {
  EXPECT_CALL(mock_client_, IsSpokenFeedbackAccessibilityServiceEnabled())
      .WillOnce(Return(false));
  GURL url("http://a.example.com/path");
  controller_->Start(
      url,
      std::make_unique<TriggerContext>(
          /* parameters = */ std::make_unique<ScriptParameters>(
              base::flat_map<std::string, std::string>{{"ENABLE_TTS", "true"}}),
          TriggerContext::Options(
              /* experiment_ids= */ "4624822", /* is_cct= */ false,
              /* onboarding_shown= */ false, /* is_direct_action= */ false,
              /* initial_url= */ "http://a.example.com/path",
              /* is_in_chrome_triggered= */ false)));
  SetTtsButtonStateForTest(TtsButtonState::DISABLED);

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  controller_->SetTtsMessage("tts_message");
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest,
       SetTtsMessageKeepsTtsButtonDisabledWithoutNonStickyStateExp) {
  EXPECT_CALL(mock_client_, IsSpokenFeedbackAccessibilityServiceEnabled())
      .WillOnce(Return(false));
  GURL url("http://a.example.com/path");
  controller_->Start(
      url,
      std::make_unique<TriggerContext>(
          /* parameters = */ std::make_unique<ScriptParameters>(
              base::flat_map<std::string, std::string>{{"ENABLE_TTS", "true"}}),
          TriggerContext::Options()));
  SetTtsButtonStateForTest(TtsButtonState::DISABLED);

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(_)).Times(0);
  controller_->SetTtsMessage("tts_message");
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DISABLED);
}

TEST_F(ControllerTest, TappingTtsButtonInDefaultStateStartsPlayingTts) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::DEFAULT);
  controller_->SetTtsMessage("tts_message");

  EXPECT_CALL(*mock_tts_controller_, Speak("tts_message", kClientLocale));
  controller_->OnTtsButtonClicked();
}

TEST_F(ControllerTest, TappingTtsButtonWhilePlayingDisablesTtsButton) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::PLAYING);

  EXPECT_CALL(mock_observer_,
              OnTtsButtonStateChanged(TtsButtonState::DISABLED));
  EXPECT_CALL(*mock_tts_controller_, Stop());
  controller_->OnTtsButtonClicked();
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DISABLED);
}

TEST_F(ControllerTest, TappingDisabledTtsButtonReEnablesItAndStartsTts) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::DISABLED);
  controller_->SetTtsMessage("tts_message");

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  EXPECT_CALL(*mock_tts_controller_, Speak("tts_message", kClientLocale));
  controller_->OnTtsButtonClicked();
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest, MaybePlayTtsMessageDoesNotStartTtsIfTtsNotEnabled) {
  // tts_enabled_ is false by default
  controller_->SetTtsMessage("tts_message");

  EXPECT_CALL(*mock_tts_controller_, Speak("tts_message", kClientLocale))
      .Times(0);
  controller_->MaybePlayTtsMessage();
}

TEST_F(ControllerTest, MaybePlayTtsMessageStartsPlayingCorrectTtsMessage) {
  EnableTtsForTest();
  controller_->SetStatusMessage("message");
  controller_->SetTtsMessage("tts_message");

  EXPECT_CALL(*mock_tts_controller_, Speak("tts_message", kClientLocale));
  controller_->MaybePlayTtsMessage();

  // Change display strings locale.
  ClientSettingsProto client_settings;
  client_settings.set_display_strings_locale("test-locale");
  controller_->SetClientSettings(client_settings);
  EXPECT_CALL(*mock_tts_controller_, Speak("tts_message", "test-locale"));
  controller_->MaybePlayTtsMessage();
}

TEST_F(ControllerTest, OnTtsEventChangesTtsButtonStateCorrectly) {
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::PLAYING));
  controller_->OnTtsEvent(AutofillAssistantTtsController::TTS_START);
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::PLAYING);

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  controller_->OnTtsEvent(AutofillAssistantTtsController::TTS_END);
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);

  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  controller_->OnTtsEvent(AutofillAssistantTtsController::TTS_ERROR);
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest, EnablingAccessibilityStopsTtsAndHidesTtsButton) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::PLAYING);

  EXPECT_CALL(*mock_tts_controller_, Stop());
  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  EXPECT_CALL(mock_observer_,
              OnTtsButtonVisibilityChanged(/* visibility= */ false));
  controller_->OnSpokenFeedbackAccessibilityServiceChanged(/* enabled= */ true);
  EXPECT_FALSE(controller_->GetTtsButtonVisible());
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest, DisablingAccessibilityShouldNotEnableTts) {
  // TTS is disabled by default.
  EXPECT_FALSE(controller_->GetTtsButtonVisible());

  EXPECT_CALL(mock_observer_,
              OnTtsButtonVisibilityChanged(/* visibility= */ false))
      .Times(0);
  controller_->OnSpokenFeedbackAccessibilityServiceChanged(
      /* enabled= */ false);
  EXPECT_FALSE(controller_->GetTtsButtonVisible());
}

TEST_F(ControllerTest, HidingUiStopsAnyOngoingTts) {
  EnableTtsForTest();
  SetTtsButtonStateForTest(TtsButtonState::PLAYING);

  EXPECT_CALL(*mock_tts_controller_, Stop());
  EXPECT_CALL(mock_observer_, OnTtsButtonStateChanged(TtsButtonState::DEFAULT));
  controller_->SetUiShown(/* shown= */ false);
  EXPECT_EQ(controller_->GetTtsButtonState(), TtsButtonState::DEFAULT);
}

TEST_F(ControllerTest, AddParametersToUserData) {
  auto script_parameters = std::make_unique<ScriptParameters>(
      base::flat_map<std::string, std::string>{{"PARAM_A", "a"}});
  script_parameters->UpdateDeviceOnlyParameters(
      base::flat_map<std::string, std::string>{{"PARAM_B", "b"}});
  GURL url("http://a.example.com/path");
  controller_->Start(
      url, std::make_unique<TriggerContext>(std::move(script_parameters),
                                            TriggerContext::Options()));

  EXPECT_EQ(controller_->GetUserData()
                ->GetAdditionalValue("param:PARAM_A")
                ->strings()
                .values(0),
            "a");
  EXPECT_FALSE(controller_->GetUserData()
                   ->GetAdditionalValue("param:PARAM_A")
                   ->is_client_side_only());
  EXPECT_EQ(controller_->GetUserData()
                ->GetAdditionalValue("param:PARAM_B")
                ->strings()
                .values(0),
            "b");
  EXPECT_TRUE(controller_->GetUserData()
                  ->GetAdditionalValue("param:PARAM_B")
                  ->is_client_side_only());
}

TEST_F(ControllerTest, SetDateTimeRange) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);
  DateProto start_date;
  start_date.set_year(2020);
  start_date.set_month(1);
  start_date.set_day(20);
  controller_->SetDateTimeRangeStartDate(start_date);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->month(),
            1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->day(), 20);

  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);
  controller_->SetDateTimeRangeStartTimeSlot(0);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_timeslot_, 0);

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);
  DateProto end_date;
  end_date.set_year(2020);
  end_date.set_month(1);
  end_date.set_day(25);
  controller_->SetDateTimeRangeEndDate(end_date);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->month(), 1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->day(), 25);

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);
  controller_->SetDateTimeRangeEndTimeSlot(1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_timeslot_, 1);
}

TEST_F(ControllerTest, SetDateTimeRangeStartDateAfterEndDate) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  DateProto date;
  date.set_year(2020);
  date.set_month(1);
  date.set_day(20);
  GetUserData()->date_time_range_start_date_ = date;
  GetUserData()->date_time_range_end_date_ = date;

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);

  date.set_day(21);
  controller_->SetDateTimeRangeStartDate(date);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->month(),
            1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->day(), 21);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_,
            absl::nullopt);
}

TEST_F(ControllerTest, SetDateTimeRangeEndDateBeforeStartDate) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  DateProto date;
  date.set_year(2020);
  date.set_month(1);
  date.set_day(20);
  GetUserData()->date_time_range_start_date_ = date;
  GetUserData()->date_time_range_end_date_ = date;

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);
  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);

  date.set_day(19);
  controller_->SetDateTimeRangeEndDate(date);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->month(), 1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->day(), 19);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_,
            absl::nullopt);
}

TEST_F(ControllerTest, SetDateTimeRangeSameDatesStartTimeAfterEndTime) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  DateProto date;
  date.set_year(2020);
  date.set_month(1);
  date.set_day(20);
  GetUserData()->date_time_range_start_date_ = date;
  GetUserData()->date_time_range_end_date_ = date;
  GetUserData()->date_time_range_end_timeslot_ = 0;

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);

  controller_->SetDateTimeRangeStartTimeSlot(1);
  EXPECT_EQ(*controller_->GetUserData()->date_time_range_start_timeslot_, 1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_timeslot_,
            absl::nullopt);
}

TEST_F(ControllerTest, SetDateTimeRangeSameDatesEndTimeBeforeStartTime) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  DateProto date;
  date.set_year(2020);
  date.set_month(1);
  date.set_day(20);
  GetUserData()->date_time_range_start_date_ = date;
  GetUserData()->date_time_range_end_date_ = date;
  GetUserData()->date_time_range_start_timeslot_ = 1;

  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);
  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);

  controller_->SetDateTimeRangeEndTimeSlot(0);
  EXPECT_EQ(*controller_->GetUserData()->date_time_range_end_timeslot_, 0);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_timeslot_,
            absl::nullopt);
}

TEST_F(ControllerTest, SetDateTimeRangeSameDateValidTime) {
  testing::InSequence seq;

  auto options = std::make_unique<MockCollectUserDataOptions>();
  options->request_date_time_range = true;
  auto* time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("08:00 AM");
  time_slot->set_comparison_value(0);
  time_slot = options->date_time_range.add_time_slots();
  time_slot->set_label("09:00 AM");
  time_slot->set_comparison_value(1);

  DateProto date;
  date.set_year(2020);
  date.set_month(1);
  date.set_day(20);
  GetUserData()->date_time_range_start_date_ = date;
  GetUserData()->date_time_range_end_date_ = date;

  controller_->SetCollectUserDataOptions(options.get());
  EXPECT_CALL(
      mock_observer_,
      OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_START))
      .Times(1);
  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::DATE_TIME_RANGE_END))
      .Times(1);
  controller_->SetDateTimeRangeStartTimeSlot(0);
  controller_->SetDateTimeRangeEndTimeSlot(1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->month(),
            1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_date_->day(), 20);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->year(),
            2020);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->month(), 1);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_end_date_->day(), 20);
  EXPECT_EQ(controller_->GetUserData()->date_time_range_start_timeslot_, 0);
  EXPECT_EQ(*controller_->GetUserData()->date_time_range_end_timeslot_, 1);
}

TEST_F(ControllerTest, WriteUserData) {
  auto options = std::make_unique<MockCollectUserDataOptions>();
  controller_->SetCollectUserDataOptions(options.get());

  EXPECT_CALL(mock_observer_,
              OnUserDataChanged(_, UserData::FieldChange::TERMS_AND_CONDITIONS))
      .Times(1);

  base::OnceCallback<void(UserData*, UserData::FieldChange*)> callback =
      base::BindOnce([](UserData* data, UserData::FieldChange* change) {
        data->terms_and_conditions_ = TermsAndConditionsState::ACCEPTED;
        *change = UserData::FieldChange::TERMS_AND_CONDITIONS;
      });

  controller_->WriteUserData(std::move(callback));
  EXPECT_EQ(GetUserData()->terms_and_conditions_,
            TermsAndConditionsState::ACCEPTED);
}

TEST_F(ControllerTest, ExpandOrCollapseBottomSheet) {
  {
    testing::InSequence seq;
    EXPECT_CALL(mock_observer_, OnCollapseBottomSheet()).Times(1);
    EXPECT_CALL(mock_observer_, OnExpandBottomSheet()).Times(1);
  }
  controller_->CollapseBottomSheet();
  controller_->ExpandBottomSheet();
}

TEST_F(ControllerTest, ShouldPromptActionExpandSheet) {
  // Expect this to be true initially.
  EXPECT_TRUE(controller_->ShouldPromptActionExpandSheet());

  controller_->SetExpandSheetForPromptAction(false);
  EXPECT_FALSE(controller_->ShouldPromptActionExpandSheet());

  controller_->SetExpandSheetForPromptAction(true);
  EXPECT_TRUE(controller_->ShouldPromptActionExpandSheet());
}

TEST_F(ControllerTest, SecondPromptActionShouldDefaultToExpandSheet) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "runnable")
      ->mutable_presentation()
      ->set_autostart(true);
  SetNextScriptResponse(script_response);

  ActionsResponseProto runnable_script;
  // Prompt action 1 which disables auto expand.
  auto* prompt_action = runnable_script.add_actions()->mutable_prompt();
  prompt_action->add_choices()->mutable_chip()->set_text("continue");
  prompt_action->set_disable_force_expand_sheet(true);

  // Prompt action 2 using the default should fall back to auto expand again.
  runnable_script.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("next");

  SetupActionsForScript("runnable", runnable_script);
  Start();

  // The first prompt should not auto expand.
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_FALSE(controller_->ShouldPromptActionExpandSheet());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "continue");

  // Click "continue"
  EXPECT_TRUE(controller_->PerformUserAction(0));

  // The second prompt should fall back to default auto expand again.
  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_TRUE(controller_->ShouldPromptActionExpandSheet());
  ASSERT_THAT(controller_->GetUserActions(), SizeIs(1));
  EXPECT_EQ(controller_->GetUserActions()[0].chip().text, "next");
}

TEST_F(ControllerTest, SetGenericUi) {
  {
    testing::InSequence seq;
    EXPECT_CALL(mock_observer_, OnGenericUserInterfaceChanged(NotNull()));
    EXPECT_CALL(mock_observer_, OnGenericUserInterfaceChanged(nullptr));
  }
  controller_->SetGenericUi(
      std::make_unique<GenericUserInterfaceProto>(GenericUserInterfaceProto()),
      base::DoNothing(), base::DoNothing());
  controller_->ClearGenericUi();
}

TEST_F(ControllerTest, StartPasswordChangeFlow) {
  GURL initialUrl("http://example.com/password");
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(Eq(initialUrl), _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, ""));

  EXPECT_TRUE(controller_->Start(
      initialUrl, std::make_unique<TriggerContext>(
                      /* parameters = */ std::make_unique<ScriptParameters>(
                          base::flat_map<std::string, std::string>{
                              {"PASSWORD_CHANGE_USERNAME", "test_username"}}),
                      TriggerContext::Options())));
  // Initial navigation.
  SimulateNavigateToUrl(GURL("http://b.example.com"));
  EXPECT_EQ(GetUserData()->selected_login_->username, "test_username");
  EXPECT_EQ(GetUserData()->selected_login_->origin,
            initialUrl.DeprecatedGetOriginAsURL());
  EXPECT_EQ(controller_->GetCurrentURL().host(), "b.example.com");
}

TEST_F(ControllerTest, EndPromptWithOnEndNavigation) {
  // A single script, with a prompt action and on_end_navigation enabled.
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  auto* action = actions_response.add_actions()->mutable_prompt();
  action->set_end_on_navigation(true);
  action->add_choices()->mutable_chip()->set_text("ok");

  actions_response.add_actions()
      ->mutable_prompt()
      ->add_choices()
      ->mutable_chip()
      ->set_text("ok 2");

  SetupActionsForScript("script", actions_response);

  std::vector<ProcessedActionProto> processed_actions_capture;
  EXPECT_CALL(*mock_service_, OnGetNextActions(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&processed_actions_capture),
                      RunOnceCallback<5>(net::HTTP_OK, "")));

  Start("http://a.example.com/path");

  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   Field(&Chip::text, StrEq("ok")))));

  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          GURL("http://a.example.com/path"), web_contents()->GetMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Start();
  task_environment()->FastForwardBy(base::Seconds(1));

  // Commit the navigation, which will end the current prompt.
  EXPECT_THAT(processed_actions_capture, SizeIs(0));
  simulator->Commit();

  EXPECT_EQ(AutofillAssistantState::PROMPT, controller_->GetState());
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   Field(&Chip::text, StrEq("ok 2")))));

  EXPECT_TRUE(controller_->PerformUserAction(0));

  EXPECT_THAT(processed_actions_capture, SizeIs(2));
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[0].status());
  EXPECT_EQ(ACTION_APPLIED, processed_actions_capture[1].status());
  EXPECT_TRUE(processed_actions_capture[0].prompt_choice().navigation_ended());
  EXPECT_FALSE(processed_actions_capture[1].prompt_choice().navigation_ended());
}

TEST_F(ControllerTest, CallingShutdownIfNecessaryShutsDownTheFlow) {
  SupportsScriptResponseProto empty;
  SetNextScriptResponse(empty);

  EXPECT_CALL(mock_client_,
              RecordDropOut(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // Note that even if we expect Shutdown to be called with
  // UI_CLOSED_UNEXPECTEDLY, the reported reason in this case would be
  // NO_INITIAL_SCRIPTS since the reason passed as argument in Shutdown is
  // ignore if another reason has been previously reported.
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::UI_CLOSED_UNEXPECTEDLY));
  controller_->ShutdownIfNecessary();
}

TEST_F(ControllerTest, ShutdownDirectlyWhenNeverHadUi) {
  SupportsScriptResponseProto empty;
  SetNextScriptResponse(empty);

  EXPECT_CALL(mock_client_, HasHadUI()).WillOnce(Return(false));
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::NO_INITIAL_SCRIPTS));
  Start("http://a.example.com/path");
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, PauseAndResume) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_tell()->set_message("Hello World");
  auto* action = actions_response.add_actions()->mutable_prompt();
  action->add_choices()->mutable_chip()->set_text("ok");

  SetupActionsForScript("script", actions_response);
  Start("http://a.example.com/path");

  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT));
  EXPECT_THAT(keyboard_states_, ElementsAre(true, true, false));
  EXPECT_THAT(controller_->GetStatusMessage(), StrEq("Hello World"));
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   AllOf(Field(&Chip::text, StrEq("ok")),
                                         Field(&Chip::type, NORMAL_ACTION)))));

  ScriptExecutorListener listener;
  controller_->AddListener(&listener);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Stop"));
  controller_->OnStop("Stop", "Undo");
  EXPECT_EQ(1, listener.pause_count);
  controller_->RemoveListener(&listener);

  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
  EXPECT_THAT(controller_->GetStatusMessage(), StrEq("Stop"));
  EXPECT_THAT(
      controller_->GetUserActions(),
      ElementsAre(Property(&UserAction::chip,
                           AllOf(Field(&Chip::text, StrEq("Undo")),
                                 Field(&Chip::type, HIGHLIGHTED_ACTION)))));

  EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Hello World"));
  EXPECT_TRUE(controller_->PerformUserAction(0));

  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT,
                                   AutofillAssistantState::STOPPED,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT));
  EXPECT_THAT(keyboard_states_,
              ElementsAre(true, true, false, false, true, false));
  EXPECT_THAT(controller_->GetStatusMessage(), StrEq("Hello World"));
  EXPECT_THAT(controller_->GetUserActions(),
              ElementsAre(Property(&UserAction::chip,
                                   AllOf(Field(&Chip::text, StrEq("ok")),
                                         Field(&Chip::type, NORMAL_ACTION)))));
}

TEST_F(ControllerTest, PauseAndNavigate) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_tell()->set_message("Hello World");
  auto* action = actions_response.add_actions()->mutable_prompt();
  action->add_choices()->mutable_chip()->set_text("ok");

  SetupActionsForScript("script", actions_response);
  Start("http://a.example.com/path");

  EXPECT_THAT(states_, ElementsAre(AutofillAssistantState::STARTING,
                                   AutofillAssistantState::RUNNING,
                                   AutofillAssistantState::PROMPT));
  controller_->OnStop("Stop", "Undo");

  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  EXPECT_CALL(mock_client_, Shutdown(Metrics::DropOutReason::NAVIGATION));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("http://b.example.com/path"));
}

TEST_F(ControllerTest, RegularScriptShowsDefaultInitialStatusMessage) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_tell()->set_message("Hello World");

  SetupActionsForScript("script", actions_response);

  testing::InSequence seq;
  EXPECT_CALL(mock_observer_,
              OnStatusMessageChanged(l10n_util::GetStringFUTF8(
                  IDS_AUTOFILL_ASSISTANT_LOADING, u"a.example.com")))
      .Times(1);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged("Hello World")).Times(1);
  Start("http://a.example.com/path");
}

TEST_F(ControllerTest, NotifyObserversOfInitialStatusMessageAndProgressBar) {
  SupportsScriptResponseProto script_response;
  AddRunnableScript(&script_response, "script")
      ->mutable_presentation()
      ->set_autostart(true);
  SetupScripts(script_response);

  ActionsResponseProto actions_response;
  actions_response.add_actions()->mutable_tell()->set_message("script message");
  SetupActionsForScript("script", actions_response);

  ShowProgressBarProto::StepProgressBarConfiguration progress_bar_configuration;
  progress_bar_configuration.add_annotated_step_icons()
      ->mutable_icon()
      ->set_icon(DrawableProto::PROGRESSBAR_DEFAULT_INITIAL_STEP);
  progress_bar_configuration.add_annotated_step_icons()
      ->mutable_icon()
      ->set_icon(DrawableProto::PROGRESSBAR_DEFAULT_DATA_COLLECTION);
  progress_bar_configuration.add_annotated_step_icons()
      ->mutable_icon()
      ->set_icon(DrawableProto::PROGRESSBAR_DEFAULT_PAYMENT);
  progress_bar_configuration.add_annotated_step_icons()
      ->mutable_icon()
      ->set_icon(DrawableProto::PROGRESSBAR_DEFAULT_FINAL_STEP);

  // When setting UI state of the controller before calling |Start|, observers
  // will be notified immediately after |Start|.
  controller_->SetStatusMessage("startup message");
  controller_->SetStepProgressBarConfiguration(progress_bar_configuration);
  controller_->SetProgressActiveStep(1);

  EXPECT_CALL(mock_observer_, OnStepProgressBarConfigurationChanged(
                                  progress_bar_configuration));
  EXPECT_CALL(mock_observer_, OnProgressActiveStepChanged(1));
  testing::Sequence s1;
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged("startup message"))
      .InSequence(s1);
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged("script message"))
      .InSequence(s1);
  Start("http://a.example.com/path");
}

TEST_F(ControllerTest, NotifyRuntimeManagerOnUiStateChange) {
  EXPECT_CALL(*mock_runtime_manager_, SetUIState(UIState::kShown)).Times(1);
  controller_->SetUiShown(true);

  EXPECT_CALL(*mock_runtime_manager_, SetUIState(UIState::kNotShown)).Times(1);
  controller_->SetUiShown(false);
}

TEST_F(ControllerTest, RuntimeManagerDestroyed) {
  mock_runtime_manager_.reset();
  // This method should not crash.
  controller_->SetUiShown(true);
}

TEST_F(ControllerTest, OnGetScriptsFailedWillShutdown) {
  EXPECT_CALL(mock_observer_,
              OnStatusMessageChanged(l10n_util::GetStringFUTF8(
                  IDS_AUTOFILL_ASSISTANT_LOADING, u"initialurl.com")))
      .Times(1);
  EXPECT_CALL(*mock_service_, OnGetScriptsForUrl(_, _, _))
      .WillOnce(RunOnceCallback<2>(net::HTTP_NOT_FOUND, ""));
  EXPECT_CALL(mock_observer_, OnStatusMessageChanged(l10n_util::GetStringUTF8(
                                  IDS_AUTOFILL_ASSISTANT_DEFAULT_ERROR)))
      .Times(1);
  EXPECT_CALL(mock_client_, HasHadUI()).WillOnce(Return(false));
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::GET_SCRIPTS_FAILED))
      .Times(1);

  Start();
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());
}

TEST_F(ControllerTest, Details) {
  // The current controller details, as notified to the observers.
  std::vector<Details> observed_details;

  ON_CALL(mock_observer_, OnDetailsChanged(_))
      .WillByDefault(
          Invoke([&observed_details](const std::vector<Details>& details) {
            observed_details = details;
          }));

  // Details are initially empty.
  EXPECT_THAT(controller_->GetDetails(), IsEmpty());

  // Set 2 details.
  controller_->SetDetails(std::make_unique<Details>(), base::TimeDelta());
  EXPECT_THAT(controller_->GetDetails(), SizeIs(1));
  EXPECT_THAT(observed_details, SizeIs(1));

  // Set 2 details in 1s (which directly clears the current details).
  controller_->SetDetails(std::make_unique<Details>(),
                          base::Milliseconds(1000));
  EXPECT_THAT(controller_->GetDetails(), IsEmpty());
  EXPECT_THAT(observed_details, IsEmpty());

  task_environment()->FastForwardBy(base::Milliseconds(1000));
  EXPECT_THAT(controller_->GetDetails(), SizeIs(1));
  EXPECT_THAT(observed_details, SizeIs(1));

  controller_->AppendDetails(std::make_unique<Details>(),
                             /* delay= */ base::TimeDelta());
  EXPECT_THAT(controller_->GetDetails(), SizeIs(2));
  EXPECT_THAT(observed_details, SizeIs(2));

  // Delay the appending of the details.
  controller_->AppendDetails(std::make_unique<Details>(),
                             /* delay= */ base::Milliseconds(1000));
  EXPECT_THAT(controller_->GetDetails(), SizeIs(2));
  EXPECT_THAT(observed_details, SizeIs(2));

  task_environment()->FastForwardBy(base::Milliseconds(999));
  EXPECT_THAT(controller_->GetDetails(), SizeIs(2));
  EXPECT_THAT(observed_details, SizeIs(2));

  task_environment()->FastForwardBy(base::Milliseconds(1));
  EXPECT_THAT(controller_->GetDetails(), SizeIs(3));
  EXPECT_THAT(observed_details, SizeIs(3));

  // Setting the details clears the timers.
  controller_->AppendDetails(std::make_unique<Details>(),
                             /* delay= */ base::Milliseconds(1000));
  controller_->SetDetails(nullptr, base::TimeDelta());
  EXPECT_THAT(controller_->GetDetails(), IsEmpty());
  EXPECT_THAT(observed_details, IsEmpty());

  task_environment()->FastForwardBy(base::Milliseconds(2000));
  EXPECT_THAT(controller_->GetDetails(), IsEmpty());
  EXPECT_THAT(observed_details, IsEmpty());
}

TEST_F(ControllerTest, OnScriptErrorWillAppendVanishingFeedbackChip) {
  // A script error should show the feedback chip.
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(1)));
  EXPECT_CALL(mock_client_, RecordDropOut(Metrics::DropOutReason::NAVIGATION));
  controller_->OnScriptError("Error", Metrics::DropOutReason::NAVIGATION);
  EXPECT_EQ(AutofillAssistantState::STOPPED, controller_->GetState());

  // The chip should vanish once clicked.
  EXPECT_CALL(mock_observer_, OnUserActionsChanged(SizeIs(0)));
  EXPECT_CALL(mock_client_,
              Shutdown(Metrics::DropOutReason::UI_CLOSED_UNEXPECTEDLY));
  EXPECT_TRUE(controller_->PerformUserAction(0));
}

// The chip should be hidden if and only if the keyboard is visible and the
// focus is on a bottom sheet input text.
TEST_F(ControllerTest, UpdateChipVisibility) {
  InSequence seq;

  UserAction user_action(ChipProto(), true, std::string());
  EXPECT_CALL(mock_observer_,
              OnUserActionsChanged(UnorderedElementsAre(Property(
                  &UserAction::chip, Field(&Chip::visible, Eq(true))))))
      .Times(1);
  auto user_actions = std::make_unique<std::vector<UserAction>>();
  user_actions->emplace_back(std::move(user_action));
  controller_->SetUserActions(std::move(user_actions));

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(_)).Times(0);
  controller_->OnKeyboardVisibilityChanged(true);

  EXPECT_CALL(mock_observer_,
              OnUserActionsChanged(UnorderedElementsAre(Property(
                  &UserAction::chip, Field(&Chip::visible, Eq(false))))))
      .Times(1);
  controller_->OnInputTextFocusChanged(true);

  EXPECT_CALL(mock_observer_,
              OnUserActionsChanged(UnorderedElementsAre(Property(
                  &UserAction::chip, Field(&Chip::visible, Eq(true))))))
      .Times(1);
  controller_->OnKeyboardVisibilityChanged(false);

  EXPECT_CALL(mock_observer_, OnUserActionsChanged(_)).Times(0);
  controller_->OnInputTextFocusChanged(false);
}

class ControllerPrerenderTest : public ControllerTest {
 public:
  ControllerPrerenderTest() {
    feature_list_.InitWithFeatures(
        {blink::features::kPrerender2},
        // Disable the memory requirement of Prerender2 so the test can run on
        // any bot.
        {blink::features::kPrerender2MemoryControls});
  }

  ~ControllerPrerenderTest() override = default;

  base::test::ScopedFeatureList feature_list_;
};

TEST_F(ControllerPrerenderTest, SuccessfulNavigation) {
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  NavigationStateChangeListener listener(controller_.get());
  controller_->AddNavigationListener(&listener);

  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("http://initialurl.com"), web_contents()->GetMainFrame());

  EXPECT_THAT(
      listener.events,
      ElementsAre(
          NavigationState{/* navigating= */ true, /* has_errors= */ false},
          NavigationState{/* navigating= */ false, /* has_errors= */ false}));

  listener.events.clear();

  // Start prerendering a page.
  const GURL prerendering_url("http://initialurl.com?prerendering");
  auto simulator = content::WebContentsTester::For(web_contents())
                       ->AddPrerenderAndStartNavigation(prerendering_url);
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  simulator->Commit();
  EXPECT_FALSE(controller_->IsNavigatingToNewDocument());
  EXPECT_FALSE(controller_->HasNavigationError());

  controller_->RemoveNavigationListener(&listener);

  EXPECT_THAT(listener.events, IsEmpty());
}

}  // namespace autofill_assistant
