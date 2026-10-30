#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <limits>
#include <thread>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
Observation state(Tick tick=0){Observation o;o.tick=tick;o.session_id="advanced";o.seat="A";o.map={20,20};
  o.position={4,4};o.alive=true;o.hp=100;o.protected_now=false;o.state_complete=true;o.entities_complete=true;o.vision_radius=4;
  return o;}
MonoTime when(Tick tick){return MonoTime{}+std::chrono::milliseconds(tick*500);}
Json event(std::string type,Tick start,Json area,Json params){
  return {{"source_ids",{"e1"}},{"type",type},{"phase","forecast"},{"announce_tick",0},
    {"start_tick",start},{"at_tick",nullptr},{"end_tick",nullptr},{"trigger_end_tick",nullptr},{"effect_end_tick",nullptr},
    {"area",area},{"trigger",type=="world_split"||type=="explosion"?"activation":type=="vulnerability"?"movement":"dwell"},
    {"subject_ids",nullptr},{"related_event_id",nullptr},{"params",params}};
}
Json checked(Json e,std::set<std::string> sources={"e1"},std::set<std::string> known={}){
  auto out=validate_event_semantics(ModelOutput{Json{{"schema_version",3},{"events",Json::array({e})},{"unresolved_source_ids",Json::array()}},{}},
    {{24,24},0,std::move(sources),std::move(known)});
  if(out.accepted.size()!=1)throw std::runtime_error("Advanced fixture invalid");
  return out.accepted[0];
}
Json fortress(){return checked(event("unrivaled",0,{{"kind","square"},{"center",{9,9}},{"size",3}},
  {{"duration_ticks",20},{"required_dwell_ticks",4},{"attack_size",7},{"damage",70},{"defended_damage",10}}));}
Json grant(Json e,const std::string& id,Json subjects,Tick at,Tick end){
  e["phase"]="granted";e["related_event_id"]=id;e["subject_ids"]=std::move(subjects);
  for(auto key:{"start_tick","end_tick","trigger_end_tick","area","params","trigger"})e[key]=nullptr;
  e["at_tick"]=at;e["effect_end_tick"]=end;return e;
}
Entity robot(Cell p,double hp=100){Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position=p;e.hp=hp;e.protected_now=false;return e;}
Entity wall(Cell p){Entity e;e.type=EntityType::Obstacle;e.position=p;return e;}
RuntimeConfig battle_config(){RuntimeConfig c;c.core.stage=StageMode::Battle;return c;}
struct Fake:ModelTransport {
  std::function<Json(const LlmJob&)> output;
  explicit Fake(decltype(output) f):output(std::move(f)){}
  HttpReply send(const LlmConfig&,const LlmJob& job,const CancelCheck&)override{return {200,{},output(job).dump(),true,false,false};}
};
bool until(const std::function<bool()>& p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<end){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return p();}
RuntimeConfig ai_config(){auto c=battle_config();c.llm={"http://127.0.0.1:8080/v1/chat/completions","local-fake","fixed-fake-token",256,false};return c;}
}
TEST(TemporalEscape,WaitsForGateExpiryInsteadOfTreatingItAsPermanent){
  EscapePlanner planner;
  auto result=planner.plan({4,1},{0,0},0,[](Cell p,Tick t){return p.x==1&&t<8;},
    [](Cell p,Tick t){return p.x==0&&t==10;},12);
  EXPECT_EQ(result.kind,EscapeKind::Route);EXPECT_EQ(result.first_action.type,ActionType::Wait);EXPECT_EQ(result.steps,9);
}
TEST(TemporalEscape,UsesLongerSafeRouteAcrossMultipleEventDeadlines){
  EscapePlanner planner;
  auto result=planner.plan({10,2},{0,0},0,[](Cell,Tick){return false;},[](Cell p,Tick t){
    return (t==12&&p!=Cell{9,1})||(t==1&&p==Cell{1,0});},16);
  EXPECT_EQ(result.kind,EscapeKind::Route);EXPECT_EQ(result.first_action.direction,Dir::Down);EXPECT_EQ(result.steps,10);
}
TEST(TemporalEscape,SearchBudgetAndTickMaximumAreBounded){
  EscapePlanner planner;auto always=[](Cell,Tick){return true;};auto open=[](Cell,Tick){return false;};
  EXPECT_EQ(planner.plan({256,256},{0,0},0,open,always).kind,EscapeKind::BudgetExceeded);
  auto max=std::numeric_limits<Tick>::max();auto result=planner.plan({3,3},{1,1},max-1,open,
    [=](Cell p,Tick t){return p==Cell{1,1}&&t==max;});
  EXPECT_EQ(result.kind,EscapeKind::Route);EXPECT_EQ(result.first_action.direction,Dir::Up);EXPECT_EQ(result.steps,1);
  EXPECT_EQ(tick_after(max-1,2),max);
}
TEST(ExpiredWalls,PreservesStaticAndPostExpiryEvidenceButForgetsAmbiguousEventWall){
  World world;world.reset({4,4});auto o=state();o.map={4,4};o.position={0,0};o.vision_radius.reset();o.entities_complete=false;
  o.entities={wall({1,1})};world.update(o);
  o.tick=10;o.entities={wall({1,1}),wall({1,2}),wall({2,2})};world.update(o);
  o.tick=21;o.entities={wall({2,2})};world.update(o);
  EventMemory memory;memory.reset(o.session_id);
  auto e=checked(event("world_split",10,{{"kind","cross"},{"at",{1,2}}},{{"duration_ticks",10}}));
  ASSERT_TRUE(memory.merge_validated(e,0).applied);
  EXPECT_FALSE(memory.wall_may_expire({1,1},world.first_wall_seen({1,1}),world.last_wall_seen({1,1}),20));
  EXPECT_TRUE(memory.wall_may_expire({1,2},world.first_wall_seen({1,2}),world.last_wall_seen({1,2}),20));
  memory.reconcile_walls(world,21);
  EXPECT_EQ(world.terrain({1,1}),Terrain::Wall);EXPECT_EQ(world.terrain({1,2}),Terrain::Unknown);EXPECT_EQ(world.terrain({2,2}),Terrain::Wall);
}
TEST(PublicEffects,MultiSeatGrantAndExpiryRemainIsolatedFromSelf){
  EventMemory memory;memory.reset("advanced");memory.observe(state());auto base=fortress();auto m=memory.merge_validated(base,0);ASSERT_TRUE(m.applied);
  auto g=checked(grant(base,m.event_id,Json::array({"B","C"}),4,20),{"e1"},{m.event_id});
  ASSERT_TRUE(memory.merge_validated(g,5).applied);
  EXPECT_FALSE(memory.known_effect("B","unrivaled",4));EXPECT_TRUE(memory.known_effect("B","unrivaled",5));EXPECT_TRUE(memory.known_effect("C","unrivaled",5));
  EXPECT_FALSE(memory.self_effect("unrivaled",5));EXPECT_FALSE(memory.known_effect("B","unrivaled",20));
  g["phase"]="ended";g["subject_ids"]=Json::array({"B"});g["at_tick"]=6;g["effect_end_tick"]=nullptr;
  ASSERT_TRUE(memory.merge_validated(checked(g,{"e1"},{m.event_id}),6).applied);
  EXPECT_FALSE(memory.known_effect("B","unrivaled",6));EXPECT_TRUE(memory.known_effect("C","unrivaled",6));
  g["subject_ids"]=nullptr;g["at_tick"]=7;ASSERT_TRUE(memory.merge_validated(checked(g,{"e1"},{m.event_id}),7).applied);
  EXPECT_FALSE(memory.known_effect("C","unrivaled",7));
}
TEST(PublicEffects,ImmediateRespawnResetsUnfinishedFortressDwell){
  EventMemory memory;memory.reset("advanced");memory.merge_validated(fortress(),0);auto o=state();o.position={9,9};memory.observe(o);
  for(Tick t=1;t<=4;++t){o.tick=t;o.receipt=Receipt{t-1,ActionType::Wait,false,{},{}};
    if(t==4)o.position_reason="respawn";
    memory.observe(o);}
  EXPECT_FALSE(memory.self_effect("unrivaled",4));o.position_reason.reset();
  for(Tick t=5;t<=7;++t){o.tick=t;o.receipt=Receipt{t-1,ActionType::Wait,false,{},{}};memory.observe(o);}
  EXPECT_TRUE(memory.self_effect("unrivaled",7));
}
TEST(PublicEffects,IncompleteStandaloneActivationFailsWithoutCreatingHazard){
  EventMemory memory;memory.reset("advanced");auto e=fortress();e["phase"]="activated";e["params"]=nullptr;
  EXPECT_EQ(memory.merge_validated(e,0).reason,"incomplete_independent_event");EXPECT_TRUE(memory.known_events(0).empty());
}
TEST(CombatParameters,AnnouncedRangeAndSubjectMultiplierAreUsed){
  Combatant self{"A",{4,4},100,false,false,false,{}};Combatant target{"B",{7,7},100,false,false,true,3};
  CombatParameters p;p.unrivaled_size=7;p.unrivaled_damage=70;
  auto attack=estimate_attack(self,Dir::Up,{target},p,true,false);ASSERT_EQ(attack.targets.size(),1U);EXPECT_DOUBLE_EQ(attack.targets[0].nominal_damage,210);
  p.unrivaled_size=5;EXPECT_TRUE(estimate_attack(self,Dir::Up,{target},p,true,false).targets.empty());
}
TEST(AdvancedRuntime,ResourceCapStillExploresForUnfinishedCollectionMission){
  BotRuntime runtime(battle_config());auto o=state();o.resource_value=120;o.missions={{"missing-high","high_resources",1,{}}};
  auto d=runtime.process(o,when(0));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P8_explore_unknown");EXPECT_EQ(d->action.intent.type,ActionType::Move);
}
TEST(AdvancedRuntime,KnownEnemyUnrivaledProtectsAgainstDiagonalAttack){
  auto fake=std::make_unique<Fake>([](const LlmJob& job){auto context=Json::parse(job.messages[1]["content"].get<std::string>());Json e;
    if(context["known_events"].empty())e=fortress();
    else e=grant(fortress(),context["known_events"][0]["event_id"].get<std::string>(),Json::array({"B"}),4,20);
    e["source_ids"]=Json::array({job.source_id});
    return Json{{"schema_version",3},{"events",Json::array({e})},{"unresolved_source_ids",Json::array()}};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=state(4);o.hp=15;o.entities={robot({5,5})};o.event_text="opaque forecast";
  o.missions={{"reach","reach",{},Cell{4,3}}};runtime.process(o,when(4));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));
  o.tick=5;o.event_text="opaque grant";runtime.process(o,when(5));runtime.after_publish(o);
  ASSERT_TRUE(until([&]{return runtime.status().usage.successful==2;}));
  o.tick=6;auto d=runtime.process(o,when(6));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Defend);EXPECT_EQ(d->reason,"P3_defend");
}
TEST(AdvancedRuntime,LateBlastInterpretationCancelsAttackBeforeCombatDeath){
  auto fake=std::make_unique<Fake>([](const LlmJob& job){auto e=event("explosion",2,
      {{"kind","rect"},{"min",{3,3}},{"max",{6,6}}},{{"damage",80},{"respects_protection",true}});
    e["source_ids"]=Json::array({job.source_id});return Json{{"schema_version",3},{"events",Json::array({e})},{"unresolved_source_ids",Json::array()}};});
  BotRuntime runtime(ai_config(),std::move(fake));auto o=state();o.hp=70;o.event_text="opaque delayed blast";o.entities={robot({5,4},5)};
  runtime.process(o,when(0));runtime.after_publish(o);ASSERT_TRUE(until([&]{return runtime.status().usage.successful==1;}));
  o.tick=2;auto d=runtime.process(o,when(2));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);EXPECT_EQ(d->reason,"P0_pre_move_event_wait");
}
