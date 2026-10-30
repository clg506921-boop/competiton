#include "rmtc_bot/runtime.hpp"
#include "rmtc_bot/protocol.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <fstream>
#include <thread>
#include <mutex>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
Observation frame(Tick t=0){Observation o;o.tick=t;o.session_id="runtime-1";o.seat="A";o.map={20,20};
  o.position={4,4};o.alive=true;o.hp=100;o.protected_now=false;o.state_complete=true;o.entities_complete=true;o.vision_radius=4;
  return o;}
MonoTime when(int ms){return MonoTime{}+std::chrono::milliseconds(ms);}
RuntimeConfig config(StageMode stage=StageMode::Battle){RuntimeConfig c;c.core.stage=stage;return c;}
RuntimeConfig ai_config(){auto c=config();c.llm={"http://127.0.0.1:8080/v1/chat/completions","test","fixed-fake-token",256,false};return c;}
struct Fake:ModelTransport {
  std::function<HttpReply(const LlmJob&)> call;
  explicit Fake(decltype(call) f):call(std::move(f)){}
  HttpReply send(const LlmConfig&,const LlmJob& job,const CancelCheck&)override{return call(job);}
};
bool until(const std::function<bool()>& p){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<deadline){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return p();}
Entity resource(Cell p,double value){Entity e;e.type=EntityType::Resource;e.position=p;e.id="r";e.value=value;return e;}
Entity enemy(Cell p,double hp=100){Entity e;e.type=EntityType::Robot;e.position=p;e.robot_id="B";e.hp=hp;e.protected_now=false;return e;}
Json decision(){return {{"decision_type","balanced"}};}
Json explosion(const LlmJob& job){std::ifstream file(std::string(RMTC_FIXTURE_DIR)+"/event-output.examples.json");Json fixtures;file>>fixtures;
  auto output=fixtures["cases"][0]["output"];auto& e=output["events"][0];
  e["source_ids"]=Json::array({job.source_id});e["announce_tick"]=0;e["start_tick"]=2;
  return output;
}
}
TEST(RuntimeConfig,EnvironmentOverridesFileWithoutChangingRegisteredDecision){
  auto c=parse_runtime_config({{"stage","1"},{"ai_model","file-model"},{"max_tokens",128},{"ai_quota",3}},
    {{"RMTC_STAGE","2"},{"RMTC_AI_MODEL","env-model"},{"RMTC_AI_MAX_TOKENS","512"}},decision());
  EXPECT_EQ(c.core.stage,StageMode::Battle);EXPECT_EQ(c.llm.model,"env-model");EXPECT_EQ(c.llm.max_tokens,512);
  EXPECT_EQ(c.llm.call_quota,3U);EXPECT_EQ(c.decision_type,DecisionType::Balanced);
}
TEST(RuntimeConfig,RejectsDecisionControlInjectionAndInvalidMetadata){
  for(auto d:{Json{{"decision_type","greedy"}},Json{{"decision_type","balanced"},{"target",{12,12}}},Json::object()})
    EXPECT_THROW(parse_runtime_config(Json::object(),{},d),std::invalid_argument);
  EXPECT_THROW(parse_runtime_config({{"max_tokens",true}},{},decision()),std::invalid_argument);
  EXPECT_THROW(parse_runtime_config({{"stage","2"}},{{"RMTC_AI_QUOTA","-1"}},decision()),std::invalid_argument);
  EXPECT_THROW(parse_runtime_config({{"ai_url",123}},{},decision()),std::invalid_argument);
}
TEST(RuntimeParser,PreservesRespawnDefendingAndScoreCountsAsOptionalTypedValues){
  Json j={{"tick",3},{"session_id","s"},{"self",{{"team","A"},{"position",{4,4}},{"alive",false},{"hp",0},
    {"respawn_in",2},{"defending",false}}},{"map",{{"width",24},{"height",24}}},
    {"score",{{"mission_completed",2},{"invalid_actions",0}}}};
  auto parsed=parse_observation(j.dump(),"A");ASSERT_TRUE(std::holds_alternative<Observation>(parsed));
  const auto& o=std::get<Observation>(parsed);EXPECT_EQ(o.respawn_in,2);EXPECT_EQ(o.defending,false);EXPECT_EQ(o.mission_completed,2);EXPECT_EQ(o.invalid_actions,0);
  j["self"]["respawn_in"]=true;j["score"]["mission_completed"]=-1;j["score"]["invalid_actions"]=1.0;
  auto bad=parse_observation(j.dump(),"A");ASSERT_TRUE(std::holds_alternative<Observation>(bad));
  EXPECT_FALSE(std::get<Observation>(bad).respawn_in);EXPECT_FALSE(std::get<Observation>(bad).mission_completed);EXPECT_FALSE(std::get<Observation>(bad).invalid_actions);
}
TEST(RuntimeIntegration,KnownStageTwoReachUsesPublishedMissionNotFixedCoordinate){
  BotRuntime runtime(config());auto o=frame();o.missions.push_back({"reach-other","reach",{},Cell{4,2}});
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Move);
  EXPECT_EQ(d->action.intent.direction,Dir::Up);EXPECT_EQ(d->reason,"P4_reach_mission");
  EXPECT_EQ(runtime.status().stage,StageMode::Battle);
}
TEST(RuntimeIntegration,FinishedReachDoesNotBlockHighResourceMission){
  BotRuntime runtime(config());auto o=frame();o.missions={{"reach","reach",{},Cell{4,2}},{"high","high_resources",1,{}}};
  o.completed_missions={"reach"};o.entities.push_back(resource({5,4},20));
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Right);EXPECT_EQ(d->reason,"P5_resource_or_collection_mission");
}
TEST(RuntimeIntegration,CurrentLethalAttackMakesDefendBeatMoveTowardResource){
  BotRuntime runtime(config());auto o=frame();o.hp=15;o.entities={enemy({5,4}),resource({4,3},20)};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Defend);EXPECT_EQ(d->reason,"P3_defend");
}
TEST(RuntimeIntegration,FreshDuelDoesNotAssumeOpponentAttacksWhileDefending){
  BotRuntime runtime(config());auto o=frame();o.map={9,9};o.entities={enemy({5,4})};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Attack);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(RuntimeIntegration,HealthyDuelKeepsFiringAfterFirstExchange){
  BotRuntime runtime(config());auto o=frame();o.map={9,9};o.hp=80;o.entities={enemy({5,4})};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Attack);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(RuntimeIntegration,FreshDuelWithSeveralAttackersKeepsAllCurrentThreats){
  BotRuntime runtime(config());auto o=frame();o.map={9,9};o.hp=35;
  auto second=enemy({3,4});second.robot_id="C";o.entities={enemy({5,4}),second};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);
  // Two attacks are lethal without defense; defending against both is survivable.
  // A target's defensive hypothesis must not suppress another robot's attack.
  EXPECT_EQ(d->action.intent.type,ActionType::Defend);
}
TEST(RuntimeIntegration,SimultaneousKillStillAccountsForOwnIncomingDamage){
  BotRuntime runtime(config());auto o=frame();o.hp=5;o.entities={enemy({5,4},5)};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);
  // Both defend and attack die here; attack may still earn simultaneous effective damage.
  EXPECT_EQ(d->action.intent.type,ActionType::Attack);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(RuntimeIntegration,ProtectionAndExplicitStageOneKeepBattleActionsOut){
  for(auto stage:{StageMode::Collect,StageMode::Battle}){BotRuntime runtime(config(stage));auto o=frame();o.protected_now=true;
    o.entities={enemy({5,4},5),resource({4,3},20)};auto d=runtime.process(o,when(0));ASSERT_TRUE(d);
    EXPECT_NE(d->action.intent.type,ActionType::Attack);EXPECT_NE(d->action.intent.type,ActionType::Defend);}
}
TEST(RuntimeIntegration,EventPipelineIsOpaqueAsyncDeduplicatedAndEscapesOnTMinusOne){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob& job){++calls;
    return HttpReply{200,{},explosion(job).dump(),true,false,false};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=frame();o.event_text="opaque body without locally meaningful event words";
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(calls,0); // Request starts only after publish.
  runtime.after_publish(o);ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));
  o.tick=1;d=runtime.process(o,when(500));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P1_event_deadline_escape");
  EXPECT_EQ(d->action.intent.direction,Dir::Up);EXPECT_TRUE(runtime.status().local_success_observed);
  runtime.after_publish(o);std::this_thread::sleep_for(std::chrono::milliseconds(20));EXPECT_EQ(calls,1);
}
TEST(RuntimeIntegration,MalformedSuccessfulEventDoesNotRepeatOrClaimOfficialQualification){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&){++calls;return HttpReply{200,{},"not JSON",true,false,false};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=frame();o.event_text="same opaque notice";runtime.process(o,when(0));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));o.tick=1;runtime.process(o,when(500));runtime.after_publish(o);
  EXPECT_TRUE(runtime.status().local_success_observed);EXPECT_EQ(runtime.status().model_rejected,1U);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));EXPECT_EQ(calls,1);
  o.session_id="runtime-2";o.tick=0;o.event_text.clear();runtime.process(o,when(2000));EXPECT_FALSE(runtime.status().local_success_observed);
}
TEST(RuntimeIntegration,TaskQualificationHasOneQueuedSourceAndNeverBlocksAction){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob& job){EXPECT_TRUE(job.qualification);++calls;
    return HttpReply{200,{},"A concise task summary",true,false,false};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=frame();o.missions={{"reach","reach",{},Cell{5,4}}};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);runtime.after_publish(o);ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));
  o.tick=1;d=runtime.process(o,when(500));ASSERT_TRUE(d);runtime.after_publish(o);EXPECT_TRUE(runtime.status().local_success_observed);EXPECT_EQ(calls,1);
}
TEST(RuntimeQuota,SuccessfulBudgetRemainsSpentAcrossSessionReset){
  std::atomic<int> calls{0};auto c=ai_config();c.llm.call_quota=1;
  auto fake=std::make_unique<Fake>([&](const LlmJob&){++calls;return HttpReply{200,{},"ok",true,false,false};});
  LlmScheduler scheduler(c.llm,std::move(fake));scheduler.begin_session("a",1);
  LlmJob job;job.session_id="a";job.source_id="one";job.generation=1;job.messages=Json::array({{{"role","user"},{"content","task"}}});
  EXPECT_EQ(scheduler.submit(job),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;}));
  scheduler.begin_session("b",2);job.session_id="b";job.source_id="two";job.generation=2;
  EXPECT_EQ(scheduler.submit(job),SubmitResult::Stopped);EXPECT_EQ(calls,1);
}
TEST(RuntimeRecovery,PlannerExceptionCachesCurrentWaitInsteadOfOldTick){
  BotCore core;auto o=frame();auto first=core.process(o,when(0));ASSERT_TRUE(first);
  o.tick=1;auto d=core.process(o,when(100),[](const Observation&,ActionIntent,const PlanContext&)->PlanOverride{
    throw std::runtime_error("injected planner failure");});
  ASSERT_TRUE(d);EXPECT_EQ(d->action.tick,1);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
  EXPECT_EQ(d->reason,"planner_exception_wait");
  auto duplicate=core.process(o,when(101));ASSERT_TRUE(duplicate);EXPECT_TRUE(duplicate->duplicate);EXPECT_EQ(duplicate->action.tick,1);
}
TEST(RuntimeRecovery,ReusedCollectorWorkspaceAdaptsToChangedMapOnNewSession){
  BotRuntime runtime(config(StageMode::Collect));auto o=frame();o.entities={resource({5,4},20)};
  auto first=runtime.process(o,when(0));ASSERT_TRUE(first);EXPECT_EQ(first->action.intent.direction,Dir::Right);
  o.session_id="new-larger-map";o.map={24,24};o.position={22,22};o.entities={resource({22,21},20)};
  auto next=runtime.process(o,when(1000));ASSERT_TRUE(next);EXPECT_TRUE(next->session_changed);EXPECT_EQ(next->action.intent.direction,Dir::Up);
  EXPECT_EQ(next->action.session_id,"new-larger-map");
}
TEST(RuntimeEnvironment,ChiliUsesPossibleEntryAndPostMovePositionWithoutDefenseDiscount){
  std::ifstream file(std::string(RMTC_FIXTURE_DIR)+"/event-output.examples.json");Json fixtures;file>>fixtures;
  Json event;for(const auto& item:fixtures["cases"])if(item.contains("output")&&!item["output"]["events"].empty()&&
      item["output"]["events"][0]["type"]=="chili"&&item["output"]["events"][0]["phase"]=="forecast"){
    event=item["output"]["events"][0];break;}
  ASSERT_FALSE(event.is_null());event["start_tick"]=10;event["end_tick"]=20;event["trigger_end_tick"]=20;
  event["area"]={{"kind","points"},{"at",Json::array({Json::array({4,4}),Json::array({8,4})})}};
  EventMemory memory;memory.reset("runtime-1");ASSERT_TRUE(memory.merge_validated(event,0).applied);
  std::vector<Entity> enemies{enemy({4,3}),enemy({8,3})};double unit=event["params"]["damage"].get<double>();
  EXPECT_DOUBLE_EQ(memory.possible_chili_damage({6,4},10,false,enemies),unit*2);
  EXPECT_DOUBLE_EQ(memory.possible_chili_damage({6,5},10,false,enemies),0);
  EXPECT_DOUBLE_EQ(memory.possible_chili_damage({6,4},20,false,enemies),0);
  EXPECT_DOUBLE_EQ(memory.possible_chili_damage({6,4},10,true,enemies),0);
}
TEST(RuntimeDiagnostics,GatewayErrorCannotEchoCredentialsIntoDiagnosticStatus){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&){++calls;
    return HttpReply{401,"request echoed fixed-fake-token",{},false,false,false};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=frame();runtime.process(o,when(0));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.attempts==1;}));
  ASSERT_TRUE(until([&]{o.tick++;runtime.process(o,when(static_cast<int>(o.tick)*500));return runtime.status().last_http_status==401;}));
  EXPECT_EQ(runtime.status().last_ai_error,"other_error");EXPECT_EQ(runtime.status().last_ai_source,"mission-qualification");
  EXPECT_FALSE(runtime.status().local_success_observed);EXPECT_EQ(calls,1);
}
TEST(RuntimeDiagnostics,NonpositiveOutputLimitDisablesAiWithoutStoppingControl){
  for(auto value:{"0","-1"}){
    auto c=parse_runtime_config({{"stage","2"},{"ai_url","http://127.0.0.1:8080/v1/chat/completions"},
      {"ai_model","test"},{"ai_token","fixed-fake-token"}},{{"RMTC_AI_MAX_TOKENS",value}},decision());
    BotRuntime runtime(c);auto o=frame();o.missions={{"reach","reach",{},Cell{4,2}}};
    auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Up);EXPECT_FALSE(runtime.status().ai_enabled);
  }
}
TEST(RuntimeDiagnostics,ProtocolTokenEnvironmentOverridesFileAndRejectsConflictingAlias){
  auto c=parse_runtime_config({{"ai_token","file-fake"}},{{"RMTC_TEAM_TOKEN","environment-fake"}},decision());
  EXPECT_EQ(c.llm.token,"environment-fake");EXPECT_EQ(c.llm.call_quota,100U);
  EXPECT_THROW(parse_runtime_config(Json::object(),{{"RMTC_TEAM_TOKEN","one"},{"RMTC_AI_TOKEN","different"}},decision()),std::invalid_argument);
}
TEST(RuntimeCollection,SkippedValueDeltaDoesNotInventPickupTypes){
  BotRuntime runtime(config());auto o=frame();o.resource_value=0;o.entities={resource({5,4},10)};
  ASSERT_TRUE(runtime.process(o,when(0)));o.tick=4;o.position={5,4};o.resource_value=30;o.entities.clear();
  ASSERT_TRUE(runtime.process(o,when(2000)));auto s=runtime.status();
  EXPECT_EQ(s.confirmed_normal_min,0);EXPECT_EQ(s.confirmed_high_min,0);EXPECT_EQ(s.collection_gaps,1U);
  ASSERT_TRUE(runtime.process(o,when(2010)));EXPECT_EQ(runtime.status().collection_gaps,1U);
}
TEST(RuntimeCollection,CompletedTypedMissionRestoresLowerBoundAcrossGap){
  BotRuntime runtime(config());auto o=frame();o.resource_value=0;
  o.missions={{"n","normal_resources",2,{}},{"h","high_resources",1,{}},{"v","resource_value",40,{}}};
  ASSERT_TRUE(runtime.process(o,when(0)));o.tick=5;o.resource_value=40;o.completed_missions={"n","h","v"};
  o.entities={resource({5,4},10)};ASSERT_TRUE(runtime.process(o,when(2500)));
  EXPECT_EQ(runtime.status().confirmed_normal_min,2);EXPECT_EQ(runtime.status().confirmed_high_min,1);
  o.tick=6;o.position={5,4};o.resource_value=50;o.entities.clear();ASSERT_TRUE(runtime.process(o,when(3000)));
  EXPECT_EQ(runtime.status().confirmed_normal_min,3);EXPECT_EQ(runtime.status().confirmed_high_min,1);
  ASSERT_TRUE(runtime.process(o,when(3010)));EXPECT_EQ(runtime.status().confirmed_normal_min,3);
  o.tick=0;o.session_id="runtime-2";o.completed_missions.clear();o.resource_value=0;
  ASSERT_TRUE(runtime.process(o,when(5000)));EXPECT_EQ(runtime.status().confirmed_normal_min,0);
  EXPECT_EQ(runtime.status().confirmed_high_min,0);EXPECT_EQ(runtime.status().collection_gaps,0U);
}
TEST(RuntimeCollection,UntypedCompletedValueAndUncompletedTypedTasksDoNotProveCounts){
  BotRuntime runtime(config());auto o=frame();o.resource_value=40;
  o.missions={{"n","normal_resources",2,{}},{"v","resource_value",40,{}}};o.completed_missions={"v"};
  ASSERT_TRUE(runtime.process(o,when(0)));EXPECT_EQ(runtime.status().confirmed_normal_min,0);
  EXPECT_EQ(runtime.status().confirmed_high_min,0);
}
TEST(RuntimeEvents,QueuedNoticeKeepsOriginalObservationTickInPromptAndValidation){
  std::mutex mutex;std::vector<LlmJob> jobs;
  auto fake=std::make_unique<Fake>([&](const LlmJob& job){
    {std::lock_guard<std::mutex> lock(mutex);jobs.push_back(job);}
    return HttpReply{200,{},Json{{"schema_version",3},{"events",Json::array()},
      {"unresolved_source_ids",Json::array({job.source_id})}}.dump(),true,false,false};
  });
  BotRuntime runtime(ai_config(),std::move(fake));auto o=frame();o.event_text="older opaque notice\nnewer opaque notice";
  ASSERT_TRUE(runtime.process(o,when(0)));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));
  o.tick=5;o.event_text.clear();ASSERT_TRUE(runtime.process(o,when(2500)));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==2;}));
  std::lock_guard<std::mutex> lock(mutex);ASSERT_EQ(jobs.size(),2U);ASSERT_TRUE(jobs[1].event_context);
  EXPECT_EQ(jobs[1].event_context->observed_tick,0);
  auto input=Json::parse(jobs[1].messages[1]["content"].get<std::string>());
  EXPECT_EQ(input["announcement"],"older opaque notice");EXPECT_EQ(input["current_tick"],0);
}

TEST(Configuration,DiagnosticAndAnnouncedClockRequireNoAi){
  auto c=parse_runtime_config(Json{{"stage","0"},{"max_ticks",50},{"tick_ms",200}}, {},Json{{"decision_type","balanced"}});
  EXPECT_EQ(c.core.stage,StageMode::Diagnostic);EXPECT_TRUE(c.core.wait_only);EXPECT_EQ(c.core.tick_ms,200);
  BotRuntime runtime(c);auto o=frame();o.effective_damage=540.5;
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
  EXPECT_EQ(runtime.status().stage,StageMode::Diagnostic);runtime.after_publish(o);EXPECT_FALSE(runtime.status().ai_enabled);
  o.tick=1;o.effective_damage=480.5;d=runtime.process(o,when(200));ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Wait);
}

TEST(RuntimeIntegration,OfficialQualificationPromptMatchesJsonOutputContract){
 auto c=ai_config();c.llm.model="deepseek-flash";std::mutex lock;Json sent;std::atomic<bool> called{false};
 auto fake=std::make_unique<Fake>([&](const LlmJob& job){EXPECT_TRUE(job.qualification);{std::lock_guard<std::mutex> guard(lock);sent=make_chat_request(c.llm,job);}called=true;return HttpReply{200,{},"{}",true,false,false};});
 BotRuntime runtime(c,std::move(fake));auto o=frame();o.missions={{"go","reach",1,Cell{5,4}}};ASSERT_TRUE(runtime.process(o,when(0)));runtime.after_publish(o);ASSERT_TRUE(until([&]{return called.load();}));std::lock_guard<std::mutex> guard(lock);EXPECT_EQ(sent["thinking"]["type"],"disabled");EXPECT_EQ(sent["response_format"]["type"],"json_object");const auto system=sent["messages"][0]["content"].get<std::string>();EXPECT_NE(system.find("JSON"),std::string::npos);EXPECT_NE(system.find("summary"),std::string::npos);
}
