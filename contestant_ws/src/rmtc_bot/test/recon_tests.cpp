#include "rmtc_bot/recon.hpp"
#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <set>
using namespace rmtc;
namespace {
Observation frame(Tick tick=100,Cell position={4,4}){Observation o;o.session_id="recon";o.seat="A";o.tick=tick;
  o.map={24,24};o.position=position;o.alive=true;o.hp=100;o.protected_now=false;
  o.state_complete=o.entities_complete=true;o.vision_radius=4;o.effective_damage=200;o.resource_value=120;return o;}
World explored(){World world;world.reset({24,24});Tick tick=0;
  for(int y:{4,12,20})for(int x:{4,12,20}){world.update(frame(tick,{x,y}));tick+=10;}
  world.update(frame());return world;}
ReconDistance distances(Cell from){return [from](Cell p)->std::optional<int>{return std::abs(p.x-from.x)+std::abs(p.y-from.y);};}
const ReconFeasible safe=[](Cell,int){return true;};
std::optional<Decision> fill_runtime(BotRuntime& bot,Observation final){Tick tick=0;
  for(int y:{4,12,20})for(int x:{4,12,20}){
    auto o=frame(tick,{x,y});bot.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));tick+=10;
  }
  return bot.process(final,MonoTime{}+std::chrono::milliseconds(final.tick*500));
}
}
TEST(ReconVisibility,OnlyValidOwnSightRefreshesObservationTimes){
  World world;world.reset({24,24});auto o=frame(10);world.update(o);
  EXPECT_EQ(world.last_observed({0,0}),10);EXPECT_EQ(world.last_observed({9,4}),-1);
  o.tick=11;o.entities_complete=false;world.update(o);EXPECT_EQ(world.last_observed({0,0}),10);EXPECT_EQ(world.last_observed({4,4}),11);
  Entity e;e.type=EntityType::Robot;e.position={9,4};e.robot_id="B";o.entities={e};o.tick=12;world.update(o);
  EXPECT_EQ(world.last_observed({9,4}),12);
  o.tick=20;o.alive=false;o.entities_complete=true;world.update(o);EXPECT_EQ(world.last_observed({4,4}),12);EXPECT_EQ(world.last_observed({0,0}),10);
}
TEST(ReconVisibility,MapResetDiscardsPreviousVisibilityTimes){
  auto world=explored();ASSERT_GE(world.last_observed({20,20}),0);world.reset({20,20});
  EXPECT_EQ(world.last_observed({4,4}),-1);EXPECT_EQ(world.last_observed({20,20}),-1);
}
TEST(ReconPlanner,ObservedButStaleAreaProvidesBoundedInformationOpportunity){
  auto world=explored();auto o=frame();auto goal=select_combat_recon(o,world,500,distances(o.position),safe);
  ASSERT_TRUE(goal);EXPECT_GT(goal->steps,0);EXPECT_LE(goal->steps,12);EXPECT_GT(goal->stale_cells,0);
  EXPECT_GT(goal->estimated_rate,0);EXPECT_LE(goal->estimated_rate,.25);EXPECT_NE(goal->position,o.position);
}
TEST(ReconPlanner,UnknownTerrainIsLeftToNormalExploration){
  World world;world.reset({24,24});auto o=frame();world.update(o);
  EXPECT_FALSE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,RawTiebreakStillNeedsPublicInformationAfterMainScoreCap){
  auto world=explored();auto o=frame();o.effective_damage=500;
  EXPECT_TRUE(select_combat_recon(o,world,500,distances(o.position),safe));
  o.effective_damage=520;EXPECT_TRUE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,UnfinishedStructuredDamageMissionCanJustifyInformationRefreshBeyondDamageCap){
  auto world=explored();auto o=frame();o.effective_damage=520;o.missions={{"damage-more","effective_damage",600,{}}};
  EXPECT_TRUE(select_combat_recon(o,world,500,distances(o.position),safe));
  o.completed_missions={"damage-more"};EXPECT_TRUE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,LeavesOneActionForAFutureAttackButSoftHorizonDoesNotInventTerminalState){
  auto world=explored();auto o=frame(499);world.update(o);
  EXPECT_FALSE(select_combat_recon(o,world,500,distances(o.position),safe));
  o.tick=500;EXPECT_TRUE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,UnsafeCandidatesAreRejectedWithAtMostEightRouteChecks){
  auto world=explored();auto o=frame();int checks=0;
  EXPECT_FALSE(select_combat_recon(o,world,500,distances(o.position),[&](Cell,int){++checks;return false;}));
  EXPECT_GT(checks,0);EXPECT_LE(checks,8);
  checks=0;auto goal=select_combat_recon(o,world,500,distances(o.position),[&](Cell,int){return ++checks==2;});
  ASSERT_TRUE(goal);EXPECT_EQ(checks,2);
}
TEST(ReconPlanner,UnreachableOrInvalidPerceptionCannotGenerateAReconGoal){
  auto world=explored();auto o=frame();
  EXPECT_FALSE(select_combat_recon(o,world,500,[](Cell)->std::optional<int>{return {};},safe));
  for(int kind=0;kind<5;++kind){auto bad=o;
    if(kind==0)bad.effective_damage.reset();
    if(kind==1)bad.entities_complete=false;
    if(kind==2)bad.alive=false;
    if(kind==3)bad.vision_radius=0;
    if(kind==4)bad.map={20,20};
    EXPECT_FALSE(select_combat_recon(bad,world,500,distances(bad.position),safe));
  }
}
TEST(ReconPlanner,EntireMapCurrentlyVisibleNeedsNoReconMovement){
  World world;world.reset({5,5});auto o=frame();o.map={5,5};o.position={2,2};world.update(o);
  EXPECT_FALSE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,StaleWallsAloneDoNotCreateACombatInformationOpportunity){
  World world;world.reset({7,7});auto o=frame(10,{3,3});o.map={7,7};
  for(int y=0;y<7;++y)for(int x=0;x<7;++x)if(x<2||x>4||y<2||y>4){
    Entity e;e.type=EntityType::Obstacle;e.position={x,y};o.entities.push_back(e);
  }
  world.update(o);o.tick=100;o.vision_radius=1;o.entities.clear();world.update(o);
  EXPECT_FALSE(select_combat_recon(o,world,500,distances(o.position),safe));
}
TEST(ReconPlanner,RepeatedDecisionsRefreshCoverageInsteadOfTwoCellOscillation){
  auto world=explored();auto o=frame();std::vector<Cell> positions{o.position};std::set<int> visited;
  for(int k=0;k<20;++k){auto goal=select_combat_recon(o,world,500,distances(o.position),safe);ASSERT_TRUE(goal);
    auto current_distance=*distances(o.position)(goal->position);bool moved=false;
    for(auto dir:kSearchOrder){auto next=step(o.position,dir);
      if(o.map.contains(next)&&*distances(next)(goal->position)<current_distance){o.position=next;moved=true;break;}}
    ASSERT_TRUE(moved);visited.insert(o.map.index(o.position));positions.push_back(o.position);
    if(positions.size()>=3){EXPECT_NE(positions.back(),positions[positions.size()-3]);}
    ++o.tick;world.update(o);
  }
  EXPECT_GE(visited.size(),8U);
}
TEST(ReconRuntime,BalancedCanRefreshKnownMapWhileCollectAndStageOneKeepTheirBoundary){
  for(int variant=0;variant<3;++variant){RuntimeConfig config;config.core.stage=variant==2?StageMode::Collect:StageMode::Battle;
    if(variant==1)config.decision_type=DecisionType::Collect;
    BotRuntime runtime(config);auto decision=fill_runtime(runtime,frame());ASSERT_TRUE(decision);
    if(variant==0){EXPECT_EQ(decision->reason,"P8_refresh_combat_visibility");EXPECT_EQ(decision->action.intent.type,ActionType::Move);}
    else EXPECT_NE(decision->reason,"P8_refresh_combat_visibility");
  }
}
TEST(ReconRuntime,VisibleCombatResourceAndUnknownFrontierRetainTheirPriority){
  for(int kind=0;kind<3;++kind){RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();
    if(kind<2){Entity e;e.position={5,4};e.id="r";e.value=10;e.type=kind==0?EntityType::Robot:EntityType::Resource;
      e.robot_id="B";e.hp=100;e.protected_now=false;o.entities={e};if(kind==1)o.resource_value=0;}
    auto decision=kind==2?runtime.process(o,MonoTime{}):fill_runtime(runtime,o);ASSERT_TRUE(decision);
    EXPECT_NE(decision->reason,"P8_refresh_combat_visibility");
  }
}

TEST(ReconRuntime,CappedCombatStillMovesToRefreshStaleCoverageAfterTickTwoHundred){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);
  auto o=frame(201);o.effective_damage=570;auto d=fill_runtime(runtime,o);ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_EQ(d->reason,"P8_refresh_tiebreak_visibility");
}
TEST(ReconRuntime,LowHpCanTakeANetPositiveMutualKillRatherThanOscillating){
  RuntimeConfig config;config.core.stage=StageMode::Battle;
  for(double damage:{200.,570.}){BotRuntime runtime(config);auto o=frame();o.hp=20;o.effective_damage=damage;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={5,4};enemy.hp=20;enemy.protected_now=false;o.entities={enemy};
    auto d=runtime.process(o,MonoTime{});ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Attack);
    EXPECT_EQ(d->action.intent.direction,Dir::Right);EXPECT_EQ(d->reason,"P2_net_combat_attack");
  }
}
TEST(ReconRuntime,RespawnProtectionIsRespectedAndTickTwoHundredIsNotATerminalBoundary){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);
  for(Tick t=198;t<=204;++t){auto o=frame(t);o.position={4,4};o.protected_now=t==200||t==201;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={5,4};enemy.hp=10;enemy.protected_now=t==200||t==201;o.entities={enemy};
    auto d=runtime.process(o,MonoTime{}+std::chrono::milliseconds(t*500));ASSERT_TRUE(d);
    if(*o.protected_now)EXPECT_NE(d->action.intent.type,ActionType::Attack);
    else EXPECT_EQ(d->action.intent.type,ActionType::Attack);
  }
}

TEST(ReconRuntime,UnproductiveAlternatingRouteChangesDirectionInsteadOfReturningAgain){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);
  for(Tick tick=100;tick<=103;++tick){auto o=frame(tick,{4,tick%2?5:4});o.map={9,9};o.effective_damage=200;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,4};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    auto d=runtime.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));ASSERT_TRUE(d);
    if(tick==103){EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_NE(d->action.intent.direction,Dir::Up);}
  }
}
TEST(ReconRuntime,RepeatedObservedLosingDeathsDoNotImmediatelyReenterTheSameFiringCell){
  for(bool profitable:{false,true}){
    RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);
    for(Tick tick=100;tick<=107;++tick){auto o=frame(tick,{4,4});o.map={9,9};
      const int lives=tick>=104?2:tick>=101?1:0;o.effective_damage=250+(profitable?20:-60)*lives;
      if(tick==100||tick==103)o.position={5,4};
      if(tick==101||tick==104){o.position={5,4};o.alive=false;o.hp=0;}
      if(tick==102||tick==105||tick==106)o.protected_now=true;
      if(tick==107){Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,4};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};}
      auto d=runtime.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));ASSERT_TRUE(d);
      if(tick==107){if(profitable)EXPECT_EQ(d->action.intent.direction,Dir::Right);else EXPECT_NE(d->action.intent.direction,Dir::Right);}
    }
  }
}

TEST(ReconRuntime,FarVisibleEnemyDoesNotDisableStalePublicRecon){
  for(double damage:{200.,1008.})for(Cell p:std::vector<Cell>{{0,0},{8,0},{0,8},{8,8}}){
    RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    auto o=frame(381,{4,4});o.effective_damage=damage;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position=p;enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Move);
    EXPECT_EQ(d->reason,damage>=500?"P8_refresh_tiebreak_visibility":"P8_refresh_combat_visibility");
  }
}
TEST(ReconRuntime,ExistingNearbyFiringOpportunityRetainsPriority){
  for(double damage:{200.,1008.}){
    RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    auto o=frame(381,{4,4});o.effective_damage=damage;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,5};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Move);
    EXPECT_EQ(d->reason,damage>=500?"P7_tiebreak_firing_position":"P7_nearby_firing_position");
  }
}
TEST(ReconRuntime,ResourceAndReachGoalsStillBeatFarEnemyRecon){
  for(bool resource:{false,true}){
    RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    auto o=frame(381,{4,4});o.effective_damage=1008;o.resource_value=0;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={0,0};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    if(resource){Entity e;e.type=EntityType::Resource;e.position={4,5};e.id="new-public-resource";e.value=10;o.entities.push_back(e);}
    else o.missions={{"public-reach","reach",{},Cell{4,5}}};
    auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_EQ(d->action.intent.direction,Dir::Down);
    EXPECT_EQ(d->reason,resource?"P5_resource_or_collection_mission":"P4_reach_mission");
  }
}
TEST(ReconRuntime,FallbackDoesNotForceAReconMoveWithoutRemainingTime){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  auto o=frame(499,{4,4});o.effective_damage=1008;
  Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={0,0};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
  auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_NE(d->reason,"P8_refresh_tiebreak_visibility");
}
TEST(ReconRuntime,CurrentlyVisibleMapDoesNotInventReconValue){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  auto o=frame(381,{4,4});o.map={9,9};o.effective_damage=1008;
  Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={0,0};enemy.hp=100;enemy.protected_now=true;o.entities={enemy};
  auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(381*500));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
}

TEST(ReconRuntime,BlockedStepInsideThreeTickPursuitLoopChoosesAnExit){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(Tick tick=100;tick<=105;++tick){auto o=frame(tick,{4,tick%3==0?5:4});o.map={9,9};
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,4};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));ASSERT_TRUE(d);
    if(tick==105){EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_NE(d->action.intent.direction,Dir::Up);
      EXPECT_NE(d->reason,"P7_nearby_firing_position");}
  }
}
TEST(ReconRuntime,LoopWithDamageProgressKeepsItsFiringOpportunity){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(Tick tick=100;tick<=105;++tick){auto o=frame(tick,{4,tick%3==0?5:4});o.map={9,9};o.effective_damage=200+(tick-100)*20;
    Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,4};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));ASSERT_TRUE(d);
    if(tick==105)EXPECT_EQ(d->reason,"P7_nearby_firing_position");
  }
}
TEST(ReconRuntime,GapOrSessionChangeDiscardsShortCycleEvidence){
  for(bool session:{false,true}){RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    for(Tick i=0;i<=5;++i){auto o=frame(100+i+(i==5&&!session?1:0),{4,(100+i)%3==0?5:4});o.map={9,9};if(i==5&&session)o.session_id="new-session";
      Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={7,4};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};
      auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
      if(i==5)EXPECT_EQ(d->reason,"P7_nearby_firing_position");
    }
  }
}

namespace {
Observation diagonal_frame(Tick i){auto o=frame(100+i,i==0?Cell{4,4}:i==1?Cell{5,4}:Cell{5,5});
  Entity enemy;enemy.type=EntityType::Robot;enemy.robot_id="B";enemy.position={o.position.x+1,o.position.y+1};enemy.hp=100;enemy.protected_now=false;o.entities={enemy};return o;}
}
TEST(PursuitAlignment,RepeatedMovingDiagonalUsesOneSafePauseThenTakesAlignedShot){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(Tick i=0;i<3;++i){auto o=diagonal_frame(i);auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
    if(i==2){EXPECT_EQ(d->action.intent.type,ActionType::Wait);EXPECT_EQ(d->reason,"P7_break_diagonal_lockstep");}}
  auto o=diagonal_frame(3);o.entities[0].position={6,5};auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));
  ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Attack);EXPECT_EQ(d->reason,"P2_net_combat_attack");
}
TEST(PursuitAlignment,MissingEnemyProgressOrTickGapDoesNotCausePassiveWaiting){
  for(int variant=0;variant<3;++variant){RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    for(Tick i=0;i<3;++i){auto o=diagonal_frame(i);if(variant==0&&i==2)o.entities.clear();if(variant==1)o.effective_damage=200+20*i;if(variant==2&&i==2)++o.tick;
      auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);if(i==2)EXPECT_NE(d->reason,"P7_break_diagonal_lockstep");}
  }
}
TEST(PursuitAlignment,ResourceAndReachStillBeatAnOptionalPause){
  for(bool resource:{false,true}){RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    for(Tick i=0;i<3;++i){auto o=diagonal_frame(i);if(i==2){
        if(resource){Entity e;e.type=EntityType::Resource;e.position={5,6};e.id="new-resource";e.value=10;o.entities.push_back(e);}
        else o.missions={{"new-reach","reach",{},Cell{5,6}}};}
      auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
      if(i==2){EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_EQ(d->reason,resource?"P5_tiebreak_resource":"P4_reach_mission");}}
  }
}
TEST(PursuitAlignment,PauseDoesNotOverrideResponseToLethalCurrentAttack){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(Tick i=0;i<3;++i){auto o=diagonal_frame(i);if(i==2){o.hp=10;Entity enemy=o.entities.front();enemy.robot_id="C";enemy.position={6,5};o.entities.push_back(enemy);}
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);if(i==2){EXPECT_NE(d->action.intent.type,ActionType::Wait);EXPECT_NE(d->reason,"P7_break_diagonal_lockstep");}}
}

TEST(PursuitAlignment,ChangingDiagonalOrientationAndBlockedStepStillBreaksPursuit){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(Tick i=0;i<3;++i){auto o=frame(100+i,{4,i==2?5:4});Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={5,i==2?4:5};e.hp=100;e.protected_now=false;o.entities={e};
    if(i==1)o.receipt=Receipt{100,ActionType::Move,false,{},std::string("E_BLOCKED")};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
    if(i==2){EXPECT_EQ(d->action.intent.type,ActionType::Wait);EXPECT_EQ(d->reason,"P7_break_diagonal_lockstep");}}
}
TEST(PursuitAlignment,NewTargetOrEntirelyStationaryHistoryDoesNotTriggerPause){
  for(bool different:{false,true}){RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
    for(Tick i=0;i<3;++i){auto o=diagonal_frame(i);if(!different){o.position={4,4};o.entities[0].position={5,5};}else if(i==2)o.entities[0].robot_id="C";
      auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);if(i==2)EXPECT_NE(d->reason,"P7_break_diagonal_lockstep");}}
}

TEST(ReconRuntime,InferiorLethalFiringOffersDoNotSuppressSafeInformationRoute){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);auto o=frame(300,{4,2});o.hp=20;o.effective_damage=640;
  Entity b;b.type=EntityType::Robot;b.robot_id="B";b.position={6,1};b.hp=100;b.protected_now=false;
  Entity c=b;c.robot_id="C";c.position={3,1};o.entities={b,c};
  auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P8_refresh_tiebreak_visibility");EXPECT_EQ(d->action.intent.type,ActionType::Move);
  EXPECT_NE(d->action.intent.direction,Dir::Up);EXPECT_NE(d->action.intent.direction,Dir::Left);
}

TEST(TerrainRouting,KnownDeadEndRequiresTheReachableCorridor){
  RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);
  for(auto item:std::vector<std::pair<Tick,Cell>>{{0,{4,3}},{10,{6,3}}}){auto o=frame(item.first,item.second);o.map={8,8};o.vision_radius=1;o.resource_value=0;o.entities.clear();
    if(item.second==Cell{4,3}){Entity e;e.type=EntityType::Obstacle;e.position={3,2};o.entities.push_back(e);}ASSERT_TRUE(bot.process(o,MonoTime{}+std::chrono::milliseconds(item.first*500)));}
  auto o=frame(300,{2,2});o.map={8,8};o.vision_radius=1;o.resource_value=0;Entity e;e.type=EntityType::Obstacle;e.position={3,2};o.entities={e};e.position={2,1};o.entities.push_back(e);o.missions={{"go","reach",1,Cell{6,2}}};
  auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(150000));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P4_reach_mission");EXPECT_EQ(d->action.intent.direction,Dir::Down);
}
TEST(CounterEntry,AcceptsOnlyAnObservableFavorableOneShotTrade){
  for(auto hp:std::vector<std::pair<double,double>>{{5,5},{10,20},{20,20}}){RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto o=frame(300,{5,5});o.hp=hp.first;o.effective_damage=600;
    Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={6,6};e.hp=hp.second;e.protected_now=false;o.entities={e};auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P7_tiebreak_firing_position_counter_entry");EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_TRUE(d->action.intent.direction==Dir::Right||d->action.intent.direction==Dir::Down);}
}
TEST(CounterEntry,DoesNotTradeDownAgainstTinyHpOrChargeHealthyEnemies){
  for(auto hp:std::vector<std::pair<double,double>>{{20,5},{20,10},{20,30},{20,100}}){RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto o=frame(300,{5,5});o.hp=hp.first;o.effective_damage=600;
    Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={6,6};e.hp=hp.second;e.protected_now=false;o.entities={e};auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->reason.find("counter_entry"),std::string::npos);if(d->action.intent.type==ActionType::Move){EXPECT_NE(d->action.intent.direction,Dir::Right);EXPECT_NE(d->action.intent.direction,Dir::Down);}}
}
TEST(CounterEntry,DoesNotRelaxRiskWithNarrowVisionOrMultipleOpponents){
  for(int mode=0;mode<4;++mode){RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto o=frame(300,{5,5});o.hp=20;o.effective_damage=600;o.vision_radius=mode<3?mode+1:4;
    Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={6,6};e.hp=20;e.protected_now=false;o.entities={e};if(mode==3){e.robot_id="C";e.position={5,7};o.entities.push_back(e);}
    auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->reason.find("counter_entry"),std::string::npos);if(d->action.intent.type==ActionType::Move){EXPECT_NE(d->action.intent.direction,Dir::Right);EXPECT_NE(d->action.intent.direction,Dir::Down);}}
}

TEST(CounterEntry,LeavesVisibleResourceAndReachObjectiveRiskUnchanged){
  for(bool resource:{false,true}){RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto o=frame(300,{5,5});o.hp=20;o.effective_damage=600;
    Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={6,6};e.hp=20;e.protected_now=false;o.entities={e};
    if(resource){e.type=EntityType::Resource;e.id="public";e.position={6,5};e.value=10;o.entities.push_back(e);}else o.missions={{"near","reach",1,Cell{6,5}}};
    auto d=fill_runtime(bot,o);ASSERT_TRUE(d);EXPECT_EQ(d->reason.find("counter_entry"),std::string::npos);EXPECT_TRUE(d->reason=="P4_reach_mission"||d->reason=="P5_tiebreak_resource");
  }
}

namespace {
Observation corridor_frame(Tick tick,Cell position){
  auto o=frame(tick,position);o.map={9,5};o.resource_value=0;o.effective_damage=0;o.entities.clear();
  for(int y=0;y<5;++y)for(int x=0;x<9;++x)if(y!=2&&std::abs(x-position.x)<=4&&std::abs(y-position.y)<=4){Entity w;w.type=EntityType::Obstacle;w.position={x,y};o.entities.push_back(w);}
  o.missions={{"corridor_goal","reach",1,Cell{7,2}}};return o;
}
}
TEST(TerrainRouting,RemoteOccupantDoesNotEraseAFutureReachRoute){
 RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto seed=corridor_frame(0,{4,2});ASSERT_TRUE(bot.process(seed,MonoTime{}));
 auto o=corridor_frame(10,{1,2});Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={5,2};e.hp=100;e.protected_now=false;o.entities.push_back(e);
 auto d=bot.process(o,MonoTime{}+std::chrono::seconds(5));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P4_reach_mission");EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(TerrainRouting,ImmediateOccupantStillBlocksTheActualMove){
 RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);ASSERT_TRUE(bot.process(corridor_frame(0,{4,2}),MonoTime{}));auto o=corridor_frame(10,{1,2});Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={2,2};e.hp=100;e.protected_now=false;o.entities.push_back(e);
 auto d=bot.process(o,MonoTime{}+std::chrono::seconds(5));ASSERT_TRUE(d);EXPECT_FALSE(d->action.intent.type==ActionType::Move&&d->action.intent.direction==Dir::Right);
}
TEST(TerrainRouting,RealWallStillCutsTheCorridorRoute){
 RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);ASSERT_TRUE(bot.process(corridor_frame(0,{4,2}),MonoTime{}));auto o=corridor_frame(10,{1,2});Entity e;e.type=EntityType::Obstacle;e.position={5,2};o.entities.push_back(e);
 auto d=bot.process(o,MonoTime{}+std::chrono::seconds(5));ASSERT_TRUE(d);EXPECT_NE(d->reason,"P4_reach_mission");
}

TEST(TerrainRouting,RememberedResourceRouteSurvivesRemoteOccupantVisibility){
 RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);auto seed=corridor_frame(0,{4,2});Entity r;r.type=EntityType::Resource;r.id="public_high";r.value=20;r.position={7,2};seed.entities.push_back(r);ASSERT_TRUE(bot.process(seed,MonoTime{}));
 for(Tick tick=10;tick<=11;++tick){auto o=corridor_frame(tick,{1,2});if(tick==10){Entity e;e.type=EntityType::Robot;e.robot_id="B";e.position={5,2};e.hp=100;e.protected_now=false;o.entities.push_back(e);}auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(tick*500));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"P5_resource_or_collection_mission");EXPECT_EQ(d->action.intent.type,ActionType::Move);EXPECT_EQ(d->action.intent.direction,Dir::Right);}
}
