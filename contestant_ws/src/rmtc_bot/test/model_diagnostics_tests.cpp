#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <fstream>
#include <thread>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
Json fixture(std::size_t index=0){
  std::ifstream file(std::string(RMTC_FIXTURE_DIR)+"/event-output.examples.json");
  if(!file)throw std::runtime_error("Missing reviewed fixtures");
  Json all;file>>all;return all["cases"][index]["output"];
}
EventContext context(){return {{24,24},50,{"e1"},{"known-1"}};}
HttpReply chat(const Json& content,const Json& finish="stop"){
  return decode_chat_completion(200,Json{{"choices",Json::array({{{"finish_reason",finish},{"message",{{"content",content}}}}})}}.dump());
}
LlmResult inspect(std::string text,const Json& finish="stop"){
  LlmResult result;result.job.event_context=context();result.reply=chat(text,finish);inspect_model_reply(result);return result;
}
std::size_t count(const LlmResult& r,EventIssue issue){return r.diagnostics.rejected[static_cast<std::size_t>(issue)];}
bool until(const std::function<bool()>& predicate){
  const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(std::chrono::steady_clock::now()<limit){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  return predicate();
}
struct Fake:ModelTransport {
  std::function<HttpReply(const LlmJob&,const CancelCheck&)> call;
  explicit Fake(decltype(call) f):call(std::move(f)){}
  HttpReply send(const LlmConfig&,const LlmJob& job,const CancelCheck& cancel)override{return call(job,cancel);}
};
LlmConfig config(){LlmConfig c{"http://127.0.0.1:8080/v1/chat/completions","offline","fake-token-only",256,false};c.call_quota=1;return c;}
LlmJob job(){LlmJob j;j.session_id="diag";j.generation=1;j.source_id="e1";j.event_context=context();
  j.messages=Json::array({{{"role","user"},{"content","opaque public data"}}});return j;}
Observation frame(Tick tick){Observation o;o.tick=tick;o.session_id="diag";o.seat="A";o.map={24,24};o.position={4,4};
  o.alive=true;o.hp=100;o.protected_now=false;o.state_complete=o.entities_complete=true;o.vision_radius=4;
  Entity e;e.type=EntityType::Resource;e.position={5,4};e.id="r";e.value=10;o.entities.push_back(e);return o;}
MonoTime at(Tick tick){return MonoTime{}+std::chrono::milliseconds(tick*500);}
}
TEST(ModelDiagnostics,FinishReasonIsAnAllowlistedValueNotProviderText){
  for(auto item:{std::pair<Json,CompletionFinish>{"stop",CompletionFinish::Stop},{"length",CompletionFinish::Length},
      {"content_filter",CompletionFinish::ContentFilter},{"Bearer private-example-value",CompletionFinish::Other},
      {nullptr,CompletionFinish::Unknown},{false,CompletionFinish::Unknown}}){
    auto reply=chat("test",item.first);EXPECT_EQ(reply.finish,item.second);
    EXPECT_EQ(std::string(completion_finish_name(reply.finish)).find("private"),std::string::npos);
    EXPECT_GT(reply.response_bytes,0U);
  }
}
TEST(ModelDiagnostics,MissingOrTypedContentIsDistinctFromJsonParseFailure){
  LlmResult result;result.job=job();result.reply=chat(Json::array());inspect_model_reply(result);
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::EmptyContent);EXPECT_EQ(result.reply.error_code,"missing_model_content");
  result.reply=chat("not-json");inspect_model_reply(result);
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::ParseRejected);EXPECT_EQ(result.diagnostics.parse_detail,"model_output.objects");
}
TEST(ModelDiagnostics,LengthAndFilterRejectEvenAnApparentlyCompleteJsonPrefix){
  for(const char* reason:{"length","content_filter"}){
    auto result=inspect(fixture().dump(),reason);EXPECT_FALSE(result.interpreted);
    EXPECT_EQ(result.output_error,"model_output_incomplete");EXPECT_EQ(result.diagnostics.accepted,0U);
    EXPECT_EQ(result.diagnostics.state,std::string(reason)=="length"?ModelOutputState::Truncated:ModelOutputState::Filtered);
  }
}
TEST(ModelDiagnostics,MalformedAndDuplicateJsonHaveDifferentSafeReasons){
  auto incomplete=inspect("{\"schema_version\":3,\"events\":[");
  EXPECT_EQ(incomplete.diagnostics.parse_detail,"model_output.objects");
  auto duplicate=inspect("{\"schema_version\":3,\"schema_version\":3,\"events\":[],\"unresolved_source_ids\":[]}");
  EXPECT_EQ(duplicate.diagnostics.parse_detail,"model_output.duplicate_key");
  EXPECT_EQ(duplicate.diagnostics.state,ModelOutputState::ParseRejected);
}
TEST(ModelDiagnostics,BomWrappedOutputCountsDroppedKeysWithoutLoggingTheirNames){
  auto data=fixture();data["Bearer private-example-value"]="private-example-value";
  for(const char* key:{"at_tick","end_tick","trigger_end_tick","effect_end_tick","subject_ids","related_event_id"})data["events"][0].erase(key);
  auto result=inspect(std::string("\xEF\xBB\xBF")+"```json\n"+data.dump()+"\n```");
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::Validated);EXPECT_EQ(result.diagnostics.accepted,1U);
  EXPECT_EQ(result.diagnostics.ignored_fields,1U);EXPECT_TRUE(result.diagnostics.first_rejection_field.empty());
  ASSERT_TRUE(result.interpreted);EXPECT_TRUE(result.interpreted->accepted[0]["at_tick"].is_null());
}
TEST(ModelDiagnostics,EnvelopeAndMissingRequiredParamHaveUsefulSchemaOwnedPaths){
  auto data=fixture();data.erase("schema_version");auto result=inspect(data.dump());
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::EnvelopeRejected);EXPECT_EQ(count(result,EventIssue::Envelope),1U);
  EXPECT_EQ(result.diagnostics.first_rejection_field,"root.schema_version");
  data=fixture();data["events"][0]["params"].erase("damage");result=inspect(data.dump());
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::ItemsRejected);EXPECT_EQ(count(result,EventIssue::Schema),1U);
  EXPECT_EQ(result.diagnostics.first_rejection_field,"root.events[0].params.damage");
}
TEST(ModelDiagnostics,TypedTickIsRejectedInsteadOfCoerced){
  auto data=fixture();data["events"][0]["start_tick"]="75";auto result=inspect(data.dump());
  EXPECT_EQ(count(result,EventIssue::Schema),1U);EXPECT_EQ(result.diagnostics.accepted,0U);
  EXPECT_EQ(result.diagnostics.first_rejection_field,"root.events[0].start_tick");
}
TEST(ModelDiagnostics,SourceLinkGeometryAndTimeAreSeparateRejectionCategories){
  for(auto issue:{EventIssue::Source,EventIssue::Link,EventIssue::Geometry,EventIssue::Time}){
    auto data=fixture();auto& e=data["events"][0];
    if(issue==EventIssue::Source)e["source_ids"]={"foreign-private-example"};
    if(issue==EventIssue::Link)e["related_event_id"]="foreign-private-example";
    if(issue==EventIssue::Geometry)e["area"]["max"]={24,24};
    if(issue==EventIssue::Time)e["announce_tick"]=76;
    auto result=inspect(data.dump());EXPECT_EQ(result.diagnostics.state,ModelOutputState::ItemsRejected);
    EXPECT_EQ(count(result,issue),1U);EXPECT_EQ(result.diagnostics.accepted,0U);
    EXPECT_EQ(result.diagnostics.first_rejection_field.find("private-example"),std::string::npos);
  }
}
TEST(ModelDiagnostics,GoodItemSurvivesAndBadItemIsCountedAsPartialAcceptance){
  auto data=fixture();auto bad=data["events"][0];bad["area"]["max"]={24,24};data["events"].push_back(bad);
  auto result=inspect(data.dump());EXPECT_EQ(result.diagnostics.state,ModelOutputState::PartiallyAccepted);
  EXPECT_EQ(result.diagnostics.accepted,1U);EXPECT_EQ(count(result,EventIssue::Geometry),1U);
}
TEST(ModelDiagnostics,UnknownFactsRemainUnresolvedWithoutInventingAnEvent){
  Json data={{"schema_version",3},{"events",Json::array()},{"unresolved_source_ids",{"e1"}}};
  auto result=inspect(data.dump());EXPECT_EQ(result.diagnostics.state,ModelOutputState::Unresolved);
  EXPECT_EQ(result.diagnostics.unresolved,1U);EXPECT_EQ(result.diagnostics.accepted,0U);
  data["unresolved_source_ids"]={"foreign"};result=inspect(data.dump());
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::ItemsRejected);EXPECT_EQ(count(result,EventIssue::Source),1U);
  EXPECT_EQ(result.diagnostics.unresolved,0U);
}
TEST(ModelDiagnostics,SecondBadItemReportsItsOriginalArrayIndex){
  auto data=fixture();auto bad=data["events"][0];bad["params"].erase("damage");data["events"].push_back(bad);
  auto result=inspect(data.dump());EXPECT_EQ(result.diagnostics.state,ModelOutputState::PartiallyAccepted);
  EXPECT_EQ(result.diagnostics.accepted,1U);EXPECT_EQ(count(result,EventIssue::Schema),1U);
  EXPECT_EQ(result.diagnostics.first_rejection_field,"root.events[1].params.damage");
}
TEST(ModelDiagnostics,TransportAndQualificationAreNotReportedAsEventValidation){
  LlmResult result;result.job=job();result.reply={0,"client_timeout",{},false,true,false};inspect_model_reply(result);
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::TransportError);EXPECT_FALSE(result.interpreted);
  result.job.event_context.reset();result.reply=chat("one short mission sentence");inspect_model_reply(result);
  EXPECT_EQ(result.diagnostics.state,ModelOutputState::NotRequested);EXPECT_TRUE(result.output_error.empty());
}
TEST(ModelDiagnostics,WorkerMeasuresRequestAndReleasesResponseTextBeforeControlReceivesIt){
  auto fake=std::make_unique<Fake>([](const LlmJob&,const CancelCheck&){
    std::this_thread::sleep_for(std::chrono::milliseconds(10));return chat(fixture().dump());});
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("diag",1);ASSERT_EQ(scheduler.submit(job()),SubmitResult::Accepted);
  std::vector<LlmResult> replies;ASSERT_TRUE(until([&]{replies=scheduler.take_results();return !replies.empty();}));
  ASSERT_EQ(replies.size(),1U);EXPECT_GE(replies[0].reply.request_ms,5.0);EXPECT_GT(replies[0].reply.response_bytes,0U);
  EXPECT_TRUE(replies[0].reply.content.empty());EXPECT_EQ(replies[0].diagnostics.accepted,1U);
}
TEST(ModelDiagnostics,ThrownTransportProducesAnObservableUncertainResultInsteadOfKillingWorkerSilently){
  auto fake=std::make_unique<Fake>([](const LlmJob&,const CancelCheck&)->HttpReply{throw std::runtime_error("private-example-value");});
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("diag",1);ASSERT_EQ(scheduler.submit(job()),SubmitResult::Accepted);
  std::vector<LlmResult> replies;ASSERT_TRUE(until([&]{replies=scheduler.take_results();return !replies.empty();}));
  ASSERT_EQ(replies.size(),1U);EXPECT_EQ(replies[0].reply.error_code,"transport_exception");
  EXPECT_EQ(replies[0].diagnostics.state,ModelOutputState::TransportError);EXPECT_EQ(scheduler.usage().uncertain_reserved,1U);
}
TEST(ModelDiagnostics,SlowInterpretationAllowsControlAndExpiredExplosionCannotBecomeANewHazard){
  std::atomic<bool> started{false},release{false};std::atomic<int> calls{0};
  auto fake=std::make_unique<Fake>([&](const LlmJob& request,const CancelCheck& cancel){
    ++calls;started=true;while(!release&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto data=fixture();data["events"][0]["source_ids"]={request.source_id};data["events"][0]["announce_tick"]=0;data["events"][0]["start_tick"]=2;
    return chat(data.dump());});
  RuntimeConfig cfg;cfg.core.stage=StageMode::Battle;cfg.llm=config();BotRuntime runtime(cfg,std::move(fake));
  auto o=frame(0);o.event_text="opaque data supplied to the model";ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return started.load();}));
  for(Tick tick=1;tick<9;++tick){o.tick=tick;auto decision=runtime.process(o,at(tick));ASSERT_TRUE(decision);
    EXPECT_NE(decision->reason,"P1_event_deadline_escape");runtime.after_publish(o);EXPECT_EQ(runtime.status().events_validated,0U);}
  release=true;ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));o.tick=10;auto decision=runtime.process(o,at(10));ASSERT_TRUE(decision);
  EXPECT_NE(decision->reason,"P1_event_deadline_escape");EXPECT_EQ(runtime.status().events_validated,1U);
  EXPECT_EQ(runtime.status().events_applied,1U);EXPECT_EQ(runtime.status().last_event_observed_tick,0);EXPECT_EQ(runtime.status().last_event_consumed_tick,10);
  EXPECT_EQ(calls,1);EXPECT_TRUE(runtime.status().local_success_observed);
}
TEST(ModelDiagnostics,NewSessionCannotConsumeHeldOldReplyOrItsDiagnosticCounters){
  std::atomic<bool> started{false};
  auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck& cancel){started=true;
    while(!cancel()){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return chat(fixture().dump(),"length");});
  RuntimeConfig cfg;cfg.core.stage=StageMode::Battle;cfg.llm=config();BotRuntime runtime(cfg,std::move(fake));
  auto o=frame(0);o.event_text="opaque old source";ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return started.load();}));o=frame(0);o.session_id="fresh-session";
  // A new source is accepted after two silent 500ms periods of the old session.
  ASSERT_TRUE(runtime.process(o,at(2)));
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));o.tick=1;ASSERT_TRUE(runtime.process(o,at(3)));
  EXPECT_EQ(runtime.status().truncated_responses,0U);EXPECT_EQ(runtime.status().events_validated,0U);
  EXPECT_EQ(runtime.status().last_event_observed_tick,-1);EXPECT_FALSE(runtime.status().local_success_observed);
}
TEST(ModelDiagnostics,PromptSpecifiesEnvelopeRequiredFactsAndTriggerWithoutProviderOptions){
  auto prompt=make_event_prompt("e1","unchanged opaque announcement",50,{24,24},Json::array());
  const auto system=prompt[0]["content"].get<std::string>();const auto data=Json::parse(prompt[1]["content"].get<std::string>());
  EXPECT_NE(system.find("\"schema_version\":3"),std::string::npos);EXPECT_NE(system.find("reflection=attack_defender"),std::string::npos);
  EXPECT_NE(system.find("unresolved_source_ids=[source_id]"),std::string::npos);EXPECT_EQ(data["announcement"],"unchanged opaque announcement");
  EXPECT_EQ(prompt.dump().find("thinking"),std::string::npos);
}
