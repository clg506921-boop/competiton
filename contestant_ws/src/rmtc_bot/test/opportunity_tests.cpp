#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <thread>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
Observation frame(Tick tick=0){Observation o;o.tick=tick;o.session_id="opportunity-v05";o.seat="C";o.map={24,24};
  o.position={5,5};o.hp=100;o.alive=true;o.protected_now=false;o.state_complete=true;o.entities_complete=true;o.vision_radius=4;
  return o;}
MonoTime at(Tick tick){return MonoTime{}+std::chrono::milliseconds(tick*500);}
RuntimeConfig config(){RuntimeConfig c;c.core.stage=StageMode::Battle;return c;}
Entity robot(Cell p,double hp=100){Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position=p;e.hp=hp;e.protected_now=false;return e;}
Entity resource(Cell p){Entity e;e.type=EntityType::Resource;e.position=p;e.id="public-resource";e.value=20;return e;}
Json notice(std::string type,Tick start,Json area,Json params){return {{"source_ids",{"n"}},{"type",type},{"phase","forecast"},
  {"announce_tick",0},{"start_tick",start},{"at_tick",nullptr},{"end_tick",nullptr},{"trigger_end_tick",nullptr},
  {"effect_end_tick",nullptr},{"subject_ids",nullptr},{"related_event_id",nullptr},{"area",area},
  {"trigger",type=="angel"?"entry":type=="explosion"?"activation":"dwell"},{"params",params}};}
Json angel(Cell point={5,4},Tick start=0,int duration=20){return notice("angel",start,{{"kind","points"},{"at",{{point.x,point.y}}}},
  {{"duration_ticks",duration},{"entry_window_ticks",duration},{"heal_ticks",10},{"heal_per_tick",10}});}
Json fortress(int dwell=3,int duration=20){return notice("unrivaled",0,{{"kind","square"},{"center",{5,5}},{"size",3}},
  {{"duration_ticks",duration},{"required_dwell_ticks",dwell},{"attack_size",5},{"damage",50},{"defended_damage",10}});}
Json checked(Json e){auto result=validate_event_semantics(ModelOutput{Json{{"schema_version",3},{"events",Json::array({e})},
  {"unresolved_source_ids",Json::array()}},{}},{{24,24},0,{"n"},{}});
  if(result.accepted.size()!=1)throw std::runtime_error("Bad opportunity fixture");
  return result.accepted[0];}
struct Fake:ModelTransport {
  Json event;explicit Fake(Json e):event(std::move(e)){}
  HttpReply send(const LlmConfig&,const LlmJob& job,const CancelCheck&)override{
    auto events=event.is_array()?event:Json::array({event});
    for(auto& e:events)e["source_ids"]=Json::array({job.source_id});
    return {200,{},Json{{"schema_version",3},{"events",events},{"unresolved_source_ids",Json::array()}}.dump(),true,false,false};
  }
};
RuntimeConfig ai_config(){auto c=config();c.llm={"http://127.0.0.1:8080/v1/chat/completions","fake","fixed-fake-token",256,false};return c;}
bool ready(BotRuntime& runtime){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<end){if(runtime.status().usage.successful==1)return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;}
PublicOpportunity healing_offer(){PublicOpportunity e;e.kind=OpportunityKind::Healing;e.start=10;e.until_exclusive=12;
  e.entry_until_exclusive=12;e.heal_ticks=10;e.heal_per_tick=10;return e;}
PublicOpportunity fort_offer(){PublicOpportunity e;e.kind=OpportunityKind::Fortress;e.start=0;e.until_exclusive=20;
  e.required_dwell=10;return e;}
}
TEST(OpportunityTiming,HealingValueUsesEntryWindowButContinuesBeyondPointExpiry){
  auto e=healing_offer();auto value=estimate_healing_opportunity(e,10,2,30,{},500);ASSERT_TRUE(value);
  EXPECT_EQ(value->benefit_from,11);EXPECT_EQ(value->until_exclusive,21);EXPECT_DOUBLE_EQ(value->incremental_hp,70);
  EXPECT_FALSE(estimate_healing_opportunity(e,10,3,30,{},500));
}
TEST(OpportunityTiming,ExistingHealingPreventsWastefulRefreshAndWeakerReplacement){
  auto e=healing_offer();EXPECT_FALSE(estimate_healing_opportunity(e,10,1,100,{},500));
  EXPECT_FALSE(estimate_healing_opportunity(e,10,1,20,HealingCoverage{20,10},500));
  e.heal_per_tick=2;EXPECT_FALSE(estimate_healing_opportunity(e,10,2,20,HealingCoverage{20,10},500));
}
TEST(OpportunityTiming,FinalActionHealingHasNoLaterControlBenefitAndSoftHorizonDoesNotStopPlay){
  auto e=healing_offer();e.start=490;e.until_exclusive=e.entry_until_exclusive=520;
  EXPECT_FALSE(estimate_healing_opportunity(e,499,1,10,{},500));
  EXPECT_TRUE(estimate_healing_opportunity(e,500,1,10,{},500));
}
TEST(OpportunityTiming,FortressNeedsAnAttackTickAfterGrantAndReusesOnlyCurrentDwell){
  auto e=fort_offer();e.until_exclusive=10;
  EXPECT_FALSE(estimate_fortress_opportunity(e,0,1,1,500));
  e.until_exclusive=11;auto value=estimate_fortress_opportunity(e,0,1,1,500);ASSERT_TRUE(value);EXPECT_EQ(value->benefit_from,10);
  e.until_exclusive=20;e.confirmed_dwell=9;
  value=estimate_fortress_opportunity(e,5,0,1,500);ASSERT_TRUE(value);EXPECT_EQ(value->control_ticks,1);EXPECT_EQ(value->benefit_from,6);
  value=estimate_fortress_opportunity(e,5,1,1,500);ASSERT_TRUE(value);EXPECT_EQ(value->control_ticks,10);
}
TEST(OpportunityTiming,NonfiniteAndOverflowInputsDoNotCreateOpportunity){
  auto e=healing_offer();EXPECT_FALSE(estimate_healing_opportunity(e,10,1,std::numeric_limits<double>::quiet_NaN(),{},500));
  auto maximum=std::numeric_limits<Tick>::max();e.start=maximum-1;e.until_exclusive=e.entry_until_exclusive=maximum;
  EXPECT_FALSE(estimate_healing_opportunity(e,maximum-1,1,10,{},500));
  auto f=fort_offer();f.until_exclusive=maximum;EXPECT_FALSE(estimate_fortress_opportunity(f,maximum-1,2,1,500));
}
TEST(OpportunityMemory,OfferProgressResetsAcrossGapAndStopsAfterConfirmedGrant){
  EventMemory memory;auto o=frame();memory.reset(o.session_id);memory.observe(o);ASSERT_TRUE(memory.merge_validated(checked(fortress()),0).applied);
  o.tick=1;o.receipt=Receipt{0,ActionType::Wait,false,{},{}};memory.observe(o);
  auto offers=memory.opportunities(o.map,o.position,o.tick);ASSERT_EQ(offers.size(),1U);EXPECT_EQ(offers[0].confirmed_dwell,1);
  o.tick=3;o.receipt=Receipt{2,ActionType::Wait,false,{},{}};memory.observe(o);
  offers=memory.opportunities(o.map,o.position,o.tick);ASSERT_EQ(offers.size(),1U);EXPECT_EQ(offers[0].confirmed_dwell,0);
  for(Tick t=4;t<=6;++t){o.tick=t;o.receipt=Receipt{t-1,ActionType::Wait,false,{},{}};memory.observe(o);}
  EXPECT_TRUE(memory.self_effect("unrivaled",6));EXPECT_TRUE(memory.opportunities(o.map,o.position,6).empty());
}
TEST(OpportunityRuntime,LowHpUsesInterpretedHealingPointWithoutBlockingControl){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(angel()));auto o=frame();o.hp=20;o.event_text="opaque-a";
  ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
  o.tick=1;auto d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P6_healing_entry");EXPECT_EQ(d->action.intent.direction,Dir::Up);
}
TEST(OpportunityRuntime,StandingOnHealingPointLeavesBeforeRefreshAndFullHpDoesNotFarmIt){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(angel({5,5})));auto o=frame();o.hp=20;o.event_text="opaque-b";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
  // HTTP/worker success is not control consumption. Under instrumentation the
  // bounded merge budget may defer a queued result; assert the strategy only
  // after an event actually merged, while keeping this car on the point.
  std::optional<Decision> d;
  for(Tick tick=1;tick<=8;++tick){o.tick=tick;d=runtime.process(o,at(tick));if(runtime.status().events_applied)break;}
  ASSERT_GT(runtime.status().events_applied,0U);ASSERT_TRUE(d);
  EXPECT_EQ(d->reason,"P6_healing_entry");EXPECT_EQ(d->action.intent.type,ActionType::Move);
  ++o.tick;o.hp=100;d=runtime.process(o,at(o.tick));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P6_healing_entry");
}
TEST(OpportunityRuntime,ResourceAndReachPrioritiesRemainAheadOfOptionalHealing){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(angel()));auto o=frame();o.hp=20;o.event_text="opaque-c";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=1;o.entities={resource({6,5})};
  auto d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P5_resource_or_collection_mission");EXPECT_EQ(d->action.intent.direction,Dir::Right);
  o.tick=2;o.missions={{"reach","reach",{},Cell{5,6}}};d=runtime.process(o,at(2));ASSERT_TRUE(d);
  EXPECT_EQ(d->reason,"P4_reach_mission");EXPECT_EQ(d->action.intent.direction,Dir::Down);
}
TEST(OpportunityRuntime,ConfirmedFortressDwellingEndsWhenBuffIsGranted){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(fortress()));auto o=frame();o.event_text="opaque-d";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
  for(Tick t=1;t<=2;++t){o.tick=t;o.receipt=Receipt{t-1,ActionType::Wait,false,{},{}};
    auto d=runtime.process(o,at(t));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P6_fortress_dwell");EXPECT_EQ(d->action.intent.type,ActionType::Wait);}
  o.tick=3;o.receipt=Receipt{2,ActionType::Wait,false,{},{}};auto d=runtime.process(o,at(3));ASSERT_TRUE(d);
  EXPECT_NE(d->reason,"P6_fortress_dwell");
}
TEST(PursuitRuntime,BalancedApproachesFiringCellWhileCollectAvoidsOrdinaryCounterfire){
  auto o=frame();o.missions={{"valuable-hit","effective_damage",10,{}}};o.entities={robot({8,5})};BotRuntime balanced(config());auto d=balanced.process(o,at(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->reason,"P7_nearby_firing_position");
  // The nearest admissible firing cells are compared first, so a farther safe cell
  // cannot continually postpone entering range. Moving still pays next-position risk.
  EXPECT_EQ(d->action.intent.direction,Dir::Right);
  auto c=config();c.decision_type=DecisionType::Collect;BotRuntime collect(c);d=collect.process(o,at(0));ASSERT_TRUE(d);
  EXPECT_NE(d->reason,"P7_nearby_firing_position");
}
TEST(PursuitRuntime,ProtectedUnknownAndDamageCappedTargetsDoNotProducePursuitGain){
  auto o=frame();o.entities={robot({8,5})};o.entities[0].protected_now.reset();BotRuntime unknown(config());
  auto d=unknown.process(o,at(0));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P7_nearby_firing_position");
  o.entities[0].protected_now=false;o.effective_damage=500;BotRuntime capped(config());d=capped.process(o,at(0));ASSERT_TRUE(d);
  EXPECT_NE(d->reason,"P7_nearby_firing_position");
}
TEST(PursuitRuntime,ResourcePriorityAndFinalAttackDeadlinePreventWastefulChase){
  auto o=frame();o.entities={robot({8,5}),resource({5,6})};BotRuntime resource_first(config());auto d=resource_first.process(o,at(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->reason,"P5_resource_or_collection_mission");
  o.entities={robot({8,5})};o.tick=499;BotRuntime deadline(config());d=deadline.process(o,at(499));ASSERT_TRUE(d);
  EXPECT_NE(d->reason,"P7_nearby_firing_position");
}
TEST(PursuitRuntime,LosingPublicSightAbandonsOldEnemyPosition){
  BotRuntime runtime(config());auto o=frame();o.missions={{"valuable-hit","effective_damage",10,{}}};o.entities={robot({8,5})};auto d=runtime.process(o,at(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->reason,"P7_nearby_firing_position");o.tick=1;o.position={6,5};o.entities.clear();
  d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P7_nearby_firing_position");
}
TEST(OpportunitySafety,KnownExplosionOnArrivalRejectsOptionalHealingTarget){
  auto blast=notice("explosion",2,{{"kind","points"},{"at",{{5,4}}}},{{"damage",80},{"respects_protection",true}});
  // Explosions use a rectangular area in the output contract.
  blast["area"]={{"kind","rect"},{"min",{5,4}},{"max",{5,4}}};
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(Json::array({angel(),blast})));auto o=frame();o.hp=20;o.event_text="opaque-danger";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=1;
  auto d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P6_healing_entry");
  EXPECT_FALSE(d->action.intent.type==ActionType::Move&&d->action.intent.direction==Dir::Up);
}
TEST(OpportunitySafety,FortressDwellCanContinueWithDefendAgainstCurrentLethalAttack){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(fortress()));auto o=frame();o.event_text="opaque-defend";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=1;o.hp=15;o.entities={robot({6,5})};
  o.receipt=Receipt{0,ActionType::Wait,false,{},{}};
  auto d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Defend);
  EXPECT_EQ(d->reason,"P6_fortress_defend");
}
TEST(PursuitSafety,InterpretedReflectionDoesNotCreateAnInvitingAttackPosition){
  auto reflection=notice("reflection",0,{{"kind","global"}},{{"duration_ticks",20},{"reflect_damage",20}});
  reflection["trigger"]="attack_defender";
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(reflection));auto o=frame();o.entities={robot({8,5})};o.event_text="opaque-reflection";
  runtime.process(o,at(0));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=1;
  auto d=runtime.process(o,at(1));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P7_nearby_firing_position");
}
TEST(PursuitContinuity,StaticPublicTargetIsApproachedThenAttackedWithoutCircling){
  BotRuntime runtime(config());auto o=frame();o.missions={{"valuable-hit","effective_damage",10,{}}};o.entities={robot({9,5})};
  for(Tick t=0;t<2;++t){o.tick=t;o.position={5+static_cast<int>(t),5};
    if(t)o.receipt=Receipt{t-1,ActionType::Move,true,Dir::Right,{}};
    auto d=runtime.process(o,at(t));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P7_nearby_firing_position");
    EXPECT_EQ(d->action.intent.direction,Dir::Right);}
  o.tick=2;o.position={7,5};o.receipt=Receipt{1,ActionType::Move,true,Dir::Right,{}};
  auto d=runtime.process(o,at(2));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Attack);
  EXPECT_EQ(d->action.intent.direction,Dir::Right);EXPECT_EQ(d->reason,"P2_net_combat_attack");
}

TEST(OpportunityArea,PublicFortressElevenToThirteenIncludesExactlyNineCells){
  EventMemory memory;auto o=frame();o.position={0,0};memory.reset(o.session_id);memory.observe(o);
  auto event=fortress();event["area"]={{"kind","square"},{"center",{12,12}},{"size",3}};
  ASSERT_TRUE(memory.merge_validated(checked(event),0).applied);
  auto offers=memory.opportunities(o.map,o.position,0);ASSERT_EQ(offers.size(),1U);
  EXPECT_EQ(offers[0].targets.size(),9U);
  for(int y=11;y<=13;++y)for(int x=11;x<=13;++x)
    EXPECT_NE(std::find(offers[0].targets.begin(),offers[0].targets.end(),Cell{x,y}),offers[0].targets.end());
}

TEST(OpportunityMemory,ConfirmedForecastOffersTravelWithoutPrematureEffect){
  EventMemory memory;auto o=frame();memory.reset(o.session_id);memory.observe(o);
  auto f=fortress();f["start_tick"]=10;ASSERT_TRUE(memory.merge_validated(checked(f),0).applied);
  auto offers=memory.opportunities(o.map,o.position,1);ASSERT_EQ(offers.size(),1U);EXPECT_EQ(offers[0].start,10);
  EXPECT_EQ(offers[0].confirmed_dwell,0);EXPECT_FALSE(memory.public_active(offers[0].event_id,1));
  EXPECT_FALSE(memory.self_effect("unrivaled",1));
}
TEST(OpportunityTiming,ForecastFortressCountsTravelWaitAndActiveDwellSeparately){
  auto e=fort_offer();e.start=10;e.until_exclusive=20;e.required_dwell=3;e.confirmed_dwell=2;
  auto value=estimate_fortress_opportunity(e,5,2,0,500);ASSERT_TRUE(value);EXPECT_EQ(value->benefit_from,13);EXPECT_EQ(value->control_ticks,8);
  value=estimate_fortress_opportunity(e,5,0,0,500);ASSERT_TRUE(value);EXPECT_EQ(value->benefit_from,13);
  e.until_exclusive=13;EXPECT_FALSE(estimate_fortress_opportunity(e,5,2,0,500));
  e.start=100;e.until_exclusive=120;EXPECT_FALSE(estimate_fortress_opportunity(e,5,2,0,500));
}
TEST(OpportunityTiming,ForecastHealingEntryMustOccurDuringThePublicEntryWindow){
  auto e=healing_offer();EXPECT_FALSE(estimate_healing_opportunity(e,5,5,20,{},500));
  auto value=estimate_healing_opportunity(e,5,6,20,{},500);ASSERT_TRUE(value);EXPECT_EQ(value->benefit_from,10);
}
TEST(OpportunityRuntime,DamageCapDoesNotDiscardInterpretedFortress){
  for(double damage:{500.,1000.}){
    BotRuntime runtime(ai_config(),std::make_unique<Fake>(fortress()));auto o=frame();o.effective_damage=damage;o.event_text="opaque-capped-fortress";
    ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
    std::optional<Decision> d;for(Tick t=1;t<=8;++t){o.tick=t;d=runtime.process(o,at(t));if(runtime.status().events_applied)break;}
    ASSERT_GT(runtime.status().events_applied,0U);ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P6_fortress_dwell");EXPECT_EQ(d->action.intent.type,ActionType::Wait);
  }
}
TEST(OpportunityRuntime,ForecastFortressHasExplicitPreparationWithoutClaimingGrantedBuff){
  auto f=fortress();f["start_tick"]=10;
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(f));auto o=frame();o.event_text="opaque-future-fortress";
  ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
  std::optional<Decision> d;for(Tick t=1;t<=8;++t){o.tick=t;d=runtime.process(o,at(t));if(runtime.status().events_applied)break;}
  ASSERT_TRUE(d);ASSERT_GT(runtime.status().events_applied,0U);EXPECT_EQ(d->reason,"P6_fortress_prepare");
}
TEST(OpportunityRuntime,ExpiredOrTooLateFortressIsNotWorthWaitingFor){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(fortress(3,4)));auto o=frame();o.event_text="opaque-expiring-fortress";
  ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=2;
  auto d=runtime.process(o,at(2));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P6_fortress_dwell");EXPECT_NE(d->reason,"P6_fortress_prepare");
}

TEST(OpportunitySafety,FutureFortressPreparationLeavesKnownExplosionCell){
  auto f=fortress();f["start_tick"]=10;
  auto blast=notice("explosion",5,{{"kind","rect"},{"min",{5,5}},{"max",{5,5}}},{{"damage",100},{"respects_protection",true}});
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(Json::array({f,blast})));auto o=frame();o.event_text="opaque-future-danger";
  ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));o.tick=1;
  auto d=runtime.process(o,at(1));ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Move);
}

TEST(OpportunityArea,RulesV12ShiftedFortressPreservesAnnouncedRegionAndStandardParameters){
  for(Cell center:std::vector<Cell>{{3,3},{12,12},{20,20}}){
    EventMemory memory;auto o=frame();o.position={0,0};memory.reset(o.session_id);memory.observe(o);
    auto e=fortress(10,50);e["area"]={{"kind","square"},{"center",{center.x,center.y}},{"size",3}};
    ASSERT_TRUE(memory.merge_validated(checked(e),0).applied);auto offers=memory.opportunities(o.map,o.position,0);ASSERT_EQ(offers.size(),1U);
    EXPECT_EQ(offers[0].targets.size(),9U);EXPECT_EQ(offers[0].required_dwell,10);EXPECT_EQ(offers[0].until_exclusive,50);
    for(int y=center.y-1;y<=center.y+1;++y)for(int x=center.x-1;x<=center.x+1;++x)
      EXPECT_NE(std::find(offers[0].targets.begin(),offers[0].targets.end(),Cell{x,y}),offers[0].targets.end());
  }
}

TEST(OpportunityRuntime,GuaranteedSafeImmediateKillBeatsOptionalFortressAfterCap){
  BotRuntime runtime(ai_config(),std::make_unique<Fake>(fortress()));auto o=frame();o.effective_damage=1000;o.entities={robot({6,5},5)};o.event_text="opaque-guaranteed-kill";
  ASSERT_TRUE(runtime.process(o,at(0)));runtime.after_publish(o);ASSERT_TRUE(ready(runtime));
  std::optional<Decision> d;for(Tick t=1;t<=8;++t){o.tick=t;d=runtime.process(o,at(t));if(runtime.status().events_applied)break;}
  ASSERT_GT(runtime.status().events_applied,0U);ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Attack);EXPECT_EQ(d->reason,"P2_net_combat_attack");
}
