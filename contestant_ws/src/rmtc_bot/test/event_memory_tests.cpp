#include "rmtc_bot/event_memory.hpp"
#include "rmtc_bot/core.hpp"
#include <gtest/gtest.h>
#include <fstream>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
Json fixture_event(std::size_t i){
  std::ifstream f(std::string(RMTC_FIXTURE_DIR)+"/event-output.examples.json");Json j;f>>j;
  auto out=j["cases"][i]["output"];
  auto result=validate_event_semantics(ModelOutput{out,{}},{{24,24},0,{"e1"},{"explosion-1","illusion-1","angel-1"}});
  if(result.accepted.empty())throw std::runtime_error("Fixture not valid");
  return result.accepted[0];
}
Observation state(Tick tick,Cell p){Observation o;o.tick=tick;o.session_id="s1";o.seat="A";o.position=p;o.alive=true;return o;}
Json notice(const Json& base,std::string phase,const std::string& id,bool own){
  Json n=base;n["phase"]=phase;n["related_event_id"]=id;
  for(const auto* k:{"start_tick","at_tick","end_tick","trigger_end_tick","effect_end_tick","area","params","trigger"})n[k]=nullptr;
  n["subject_ids"]=own?Json::array({"A"}):Json(nullptr);return n;
}
}
TEST(EventMemory,IllusionCastEndingDoesNotCancelPersonalReversal){
  EventMemory memory;memory.reset("s1");auto event=fixture_event(1);auto m=memory.merge_validated(event,50);ASSERT_TRUE(m.applied);
  memory.observe(state(55,{9,4}));EXPECT_TRUE(memory.self_effect("illusion",55));
  auto end=notice(event,"ended",m.event_id,false);EXPECT_TRUE(memory.merge_validated(end,56).applied);
  EXPECT_TRUE(memory.self_effect("illusion",66));EXPECT_FALSE(memory.self_effect("illusion",67));
  EXPECT_FALSE(memory.known_events(60).empty());
}
TEST(EventMemory,LateInterpretationUsesStoredActivationPosition){
  EventMemory memory;memory.reset("s1");memory.observe(state(55,{9,4}));memory.observe(state(56,{1,1}));
  auto m=memory.merge_validated(fixture_event(1),56);ASSERT_TRUE(m.applied);
  EXPECT_TRUE(memory.self_effect("illusion",60));
}
TEST(EventMemory,AngelSurvivesPointEndAndUsesAnnouncedEightTicks){
  EventMemory memory;memory.reset("s1");auto angel=fixture_event(2);auto m=memory.merge_validated(angel,95);ASSERT_TRUE(m.applied);
  memory.observe(state(110,{2,9}));auto entered=state(111,{3,9});entered.receipt=Receipt{110,ActionType::Move,true,Dir::Right,{}};
  memory.observe(entered);auto effect=memory.self_effect("angel",111);ASSERT_TRUE(effect);EXPECT_EQ(effect->until_exclusive,118);
  EXPECT_TRUE(memory.merge_validated(notice(angel,"ended",m.event_id,false),116).applied);
  EXPECT_TRUE(memory.self_effect("angel",117));EXPECT_FALSE(memory.self_effect("angel",118));
}
TEST(EventMemory,VulnerabilityStartsNextCombatTickAndExpiresAtSharedDeadline){
  EventMemory memory;memory.reset("s1");auto event=fixture_event(7);memory.merge_validated(event,185);
  memory.observe(state(190,{1,1}));auto moved=state(191,{2,1});moved.receipt=Receipt{190,ActionType::Move,true,Dir::Right,{}};memory.observe(moved);
  EXPECT_FALSE(memory.self_effect("vulnerability",190));EXPECT_TRUE(memory.self_effect("vulnerability",191));EXPECT_FALSE(memory.self_effect("vulnerability",215));
}
TEST(EventMemory,UndatedExpiryCannotClearKnownNewerRefresh){
  EventMemory memory;memory.reset("s1");auto event=fixture_event(1);auto m=memory.merge_validated(event,50);memory.observe(state(55,{9,4}));
  auto gain=notice(event,"granted",m.event_id,true);gain["at_tick"]=64;gain["effect_end_tick"]=76;
  EXPECT_TRUE(memory.merge_validated(gain,65).applied);
  auto end=notice(event,"ended",m.event_id,true);EXPECT_FALSE(memory.merge_validated(end,66).applied);EXPECT_TRUE(memory.self_effect("illusion",70));
}
TEST(EventMemory,UnrivaledGrantIsAvailableOnlyAfterDwellSettlement){
  EventMemory memory;memory.reset("s1");auto event=fixture_event(3);auto m=memory.merge_validated(event,145);
  memory.observe(state(150,{9,9}));
  for(Tick action=150;action<154;++action){auto o=state(action+1,{9,9});o.receipt=Receipt{action,ActionType::Wait,false,{},{}};memory.observe(o);}
  EXPECT_FALSE(memory.self_effect("unrivaled",153));EXPECT_TRUE(memory.self_effect("unrivaled",154));
  memory.merge_validated(notice(event,"ended",m.event_id,false),174);EXPECT_FALSE(memory.self_effect("unrivaled",174));
}
TEST(EventMemory,ExplosionProtectionAndSplitWallUsePublicTimes){
  EventMemory memory;memory.reset("s1");memory.merge_validated(fixture_event(0),50);
  EXPECT_FALSE(memory.dangerous({5,5},74,false));EXPECT_TRUE(memory.dangerous({5,5},75,false));EXPECT_FALSE(memory.dangerous({5,5},75,true));
  auto split=fixture_event(4);split["source_ids"]={"e2"};memory.merge_validated(split,195);
  EXPECT_TRUE(memory.dangerous({8,0},200,true));EXPECT_FALSE(memory.dangerous({8,0},210,true));
}
TEST(EventMemory,NewSessionClearsEffectsAndDeduplicatesSources){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(1);EXPECT_TRUE(memory.merge_validated(e,50).applied);EXPECT_FALSE(memory.merge_validated(e,50).applied);
  memory.observe(state(55,{9,4}));ASSERT_TRUE(memory.self_effect("illusion",55));memory.reset("s2");EXPECT_FALSE(memory.self_effect("illusion",55));EXPECT_TRUE(memory.known_events(55).empty());
}
TEST(EventMemory,MultipleDistinctEventsFromOneAnnouncementAreNotCollapsed){
  EventMemory memory;memory.reset("s1");auto a=fixture_event(0),b=a;
  b["area"]["min"]={10,10};b["area"]["max"]={12,12};
  auto first=memory.merge_validated(a,50),second=memory.merge_validated(b,50);
  ASSERT_TRUE(first.applied);ASSERT_TRUE(second.applied);EXPECT_NE(first.event_id,second.event_id);
}
TEST(EventMemory,UpdateNoticeBeforeKnownActivationIsRejected){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(1);auto m=memory.merge_validated(e,50);
  auto activate=notice(e,"activated",m.event_id,false);
  EXPECT_FALSE(memory.merge_validated(activate,54).applied);
}
TEST(EventMemory,LinkedForecastNeverDoublesAnExplosion){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(0);auto first=memory.merge_validated(e,50);ASSERT_TRUE(first.applied);
  e["related_event_id"]=first.event_id;e["source_ids"]={"repeat"};
  auto duplicate=memory.merge_validated(e,51);
  EXPECT_FALSE(duplicate.applied);EXPECT_EQ(duplicate.event_id,first.event_id);
  EXPECT_EQ(memory.known_events(51).size(),1U);
  EXPECT_DOUBLE_EQ(memory.before_move({5,5},75,false).damage,80);
  EXPECT_DOUBLE_EQ(memory.before_move({5,5},75,true).damage,0);
}
TEST(EventMemory,LinkedContradictoryForecastCannotMutateTheKnownHazard){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(0);auto first=memory.merge_validated(e,50);
  e["related_event_id"]=first.event_id;e["params"]["damage"]=160;
  EXPECT_EQ(memory.merge_validated(e,51).reason,"contradictory_forecast");
  EXPECT_DOUBLE_EQ(memory.before_move({5,5},75,false).damage,80);
}
TEST(EventMemory,RecentlyExpiredContextLetsStrictValidatorLinkDelayedClosure){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(0);auto m=memory.merge_validated(e,50);
  auto context=memory.known_events(76);ASSERT_EQ(context.size(),1U);
  EXPECT_EQ(context[0]["event_id"],m.event_id);EXPECT_EQ(context[0]["params"],e["params"]);
  auto ended=notice(e,"ended",m.event_id,false);ended["source_ids"]={"late"};
  auto result=validate_event_semantics(ModelOutput{Json{{"schema_version",3},{"events",Json::array({ended})},{"unresolved_source_ids",Json::array()}},{}},
      {{24,24},76,{"late"},{context[0]["event_id"].get<std::string>()}});
  ASSERT_EQ(result.accepted.size(),1U);EXPECT_TRUE(memory.merge_validated(result.accepted[0],76).applied);
  EXPECT_FALSE(memory.public_active(m.event_id,76));EXPECT_DOUBLE_EQ(memory.before_move({5,5},76,false).damage,0);
  EXPECT_TRUE(memory.known_events(205).empty());
}
TEST(EventMemory,ClosedContextCannotBeReactivatedByDuplicateForecast){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(4);auto m=memory.merge_validated(e,195);
  ASSERT_TRUE(memory.merge_validated(notice(e,"ended",m.event_id,false),205).applied);
  e["related_event_id"]=m.event_id;e["source_ids"]={"repeat"};
  EXPECT_FALSE(memory.merge_validated(e,206).applied);EXPECT_FALSE(memory.split_wall({8,0},206));
  ASSERT_FALSE(memory.known_events(206).empty());EXPECT_EQ(memory.known_events(206)[0]["closed_observed_tick"],205);
}
TEST(EventMemory,ContextKeepsActiveEventsAheadOfBoundedRecentHistory){
  EventMemory memory;memory.reset("s1");auto e=fixture_event(0);
  for(int i=0;i<20;++i){e["source_ids"]={"past-"+std::to_string(i)};e["start_tick"]=i;ASSERT_TRUE(memory.merge_validated(e,0).applied);}
  e["source_ids"]={"future"};e["start_tick"]=100;auto future=memory.merge_validated(e,0);ASSERT_TRUE(future.applied);
  auto known=memory.known_events(30);ASSERT_EQ(known.size(),16U);EXPECT_EQ(known[0]["event_id"],future.event_id);
}
TEST(EventMemory,SplitExemptsOnlyExplicitInitialOwnSpawn){
  EventMemory memory;memory.reset("s1");auto initial=state(0,{8,5});
  initial.map={24,24};initial.state_complete=true;initial.position_reason="initial";memory.observe(initial);
  ASSERT_TRUE(memory.merge_validated(fixture_event(4),195).applied);
  EXPECT_FALSE(memory.split_wall({8,5},200));EXPECT_FALSE(memory.dangerous({8,5},200,false));
  EXPECT_FALSE(memory.before_move({8,5},200,false).forced_respawn);
  EXPECT_TRUE(memory.split_wall({8,6},200));EXPECT_TRUE(memory.before_move({8,6},200,true).forced_respawn);
  EXPECT_FALSE(memory.before_move({8,6},201,false).forced_respawn);
  EXPECT_TRUE(memory.split_wall({8,23},209));EXPECT_FALSE(memory.split_wall({8,23},210));
  EXPECT_FALSE(memory.wall_may_expire({8,5},200,209,210));EXPECT_TRUE(memory.wall_may_expire({8,6},200,209,210));
  auto explosion=fixture_event(0);explosion["start_tick"]=200;
  ASSERT_TRUE(memory.merge_validated(explosion,195).applied);
  EXPECT_TRUE(memory.dangerous({8,5},200,false));EXPECT_DOUBLE_EQ(memory.before_move({8,5},200,false).damage,80);
}
TEST(EventMemory,LateJoinAndRespawnCannotInventAnInitialSpawn){
  for(const auto& reason:{std::string("initial"),std::string("respawn"),std::string("")}){
    EventMemory memory;memory.reset("s1");auto o=state(reason=="initial"?73:0,{8,5});
    o.map={24,24};o.state_complete=true;o.position_reason=reason;memory.observe(o);
    ASSERT_TRUE(memory.merge_validated(fixture_event(4),195).applied);EXPECT_TRUE(memory.split_wall({8,5},200));
  }
}
TEST(EventMemory,NewSessionClearsSpawnAndExpiredSplitDoesNotEraseSpawnWallEvidence){
  EventMemory memory;memory.reset("s1");auto o=state(0,{8,5});o.map={24,24};o.state_complete=true;o.position_reason="initial";
  memory.observe(o);ASSERT_TRUE(memory.merge_validated(fixture_event(4),195).applied);
  World world;world.reset(o.map);auto seen=state(200,{7,5});seen.map=o.map;
  Entity wall;wall.type=EntityType::Obstacle;wall.position={8,5};seen.entities={wall};world.update(seen);
  memory.reconcile_walls(world,210);EXPECT_EQ(world.terrain({8,5}),Terrain::Wall);
  memory.reset("s2");ASSERT_TRUE(memory.merge_validated(fixture_event(4),195).applied);
  EXPECT_TRUE(memory.split_wall({8,5},200));
}
