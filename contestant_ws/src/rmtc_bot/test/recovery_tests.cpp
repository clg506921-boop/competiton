#include "rmtc_bot/core.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
namespace {
Observation frame(Tick tick=0){Observation o;o.tick=tick;o.session_id="recovery-new";o.seat="B";
  o.map={20,20};o.position={4,4};o.alive=true;o.hp=100;o.state_complete=true;
  o.entities_complete=true;o.vision_radius=4;o.protected_now=false;return o;}
MonoTime at(int ms){return MonoTime{}+std::chrono::milliseconds(ms);}
PlanOverride select_right(const Observation&,ActionIntent,const PlanContext&){return {{ActionType::Move,Dir::Right},"test_move",{}};}
PlanOverride select_wait(const Observation&,ActionIntent,const PlanContext&){return {{},"test_wait",{}};}
}
TEST(RecoveryReceipts,SchemaErrorOnSameActionIsRejectedWithoutObstacleEvidence){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=1;o.receipt=Receipt{0,ActionType::Move,false,{},std::string("E_SCHEMA")};
  auto d=core.process(o,at(100),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Rejected);EXPECT_TRUE(d->diagnostics.receipt_matched);
  EXPECT_EQ(d->diagnostics.expected_receipt_action,ActionType::Move);
  EXPECT_EQ(d->diagnostics.receipt_error,"E_SCHEMA");EXPECT_FALSE(core.world().blocked({5,4},1));
  EXPECT_EQ(d->action.intent.type,ActionType::Move);
}
TEST(RecoveryReceipts,CombatDeathCancelsMovementWithoutInventingWall){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=1;o.alive=false;o.hp=0;o.receipt=Receipt{0,ActionType::Move,false,{},std::string("E_STATE")};
  auto d=core.process(o,at(100),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::StateCancelled);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
  EXPECT_FALSE(core.world().blocked({5,4},1));EXPECT_FALSE(d->diagnostics.receipt_direction_applied);
}
TEST(RecoveryReceipts,ImmediateRespawnUsesOfficialPositionAndDoesNotBlockOriginalDestination){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=1;o.position={12,12};o.position_reason="respawn";
  o.receipt=Receipt{0,ActionType::Move,false,{},std::string("E_STATE")};
  auto d=core.process(o,at(100),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::StateCancelled);EXPECT_TRUE(d->diagnostics.position_discontinuous);
  EXPECT_FALSE(core.world().blocked({5,4},1));EXPECT_EQ(core.world().terrain({12,12}),Terrain::Free);
  EXPECT_EQ(d->action.tick,1);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(RecoveryReceipts,ContradictoryUnknownErrorCannotChangeDirectionOrLeakText){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=1;o.position={3,4};o.receipt=Receipt{0,ActionType::Move,true,Dir::Left,std::string("Bearer SECRET-test-value")};
  auto d=core.process(o,at(100),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Rejected);EXPECT_EQ(d->diagnostics.receipt_error,"UNKNOWN");
  EXPECT_FALSE(d->diagnostics.receipt_direction_applied);EXPECT_EQ(d->action.intent.direction,Dir::Right);
}
TEST(RecoveryReceipts,WorldSplitImmediateProtectedRespawnCanCancelAsWaitWithoutError){
  CoreConfig config;config.stage=StageMode::Battle;BotCore core(config);auto o=frame(135);o.position={9,8};ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=136;o.position={16,1};o.position_reason="respawn";o.protected_now=true;
  o.receipt=Receipt{135,ActionType::Wait,false,{},{}};
  auto d=core.process(o,at(500),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::StateCancelled);EXPECT_TRUE(d->diagnostics.position_discontinuous);
  EXPECT_FALSE(core.world().blocked({10,8},136));EXPECT_EQ(d->action.tick,136);
}
TEST(RecoveryReceipts,WaitMismatchIsNotExcusedWithoutImmediateProtectedRespawnEvidence){
  for(int variant=0;variant<4;++variant){CoreConfig config;config.stage=StageMode::Battle;BotCore core(config);auto o=frame(135);ASSERT_TRUE(core.process(o,at(0),select_right));
    o.tick=136;o.position={16,1};o.position_reason="respawn";o.protected_now=true;
    o.receipt=Receipt{135,ActionType::Wait,false,{},{}};
    if(variant==0)o.position_reason.reset();
    if(variant==1)o.protected_now=false;
    if(variant==2)o.tick=137;
    if(variant==3)o.receipt->error="E_TICK";
    auto d=core.process(o,at(500),select_right);ASSERT_TRUE(d);
    EXPECT_NE(d->previous_receipt,ReceiptKind::StateCancelled);
    EXPECT_EQ(d->previous_receipt,variant==3?ReceiptKind::Rejected:ReceiptKind::Mismatched);
  }
}
TEST(RecoveryReceipts,DelayedSuccessAfterGapCannotSetCurrentIllusionDirection){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_right));
  o.tick=4;o.position={8,4};o.receipt=Receipt{0,ActionType::Move,true,Dir::Left,{}};
  auto d=core.process(o,at(400),select_right);ASSERT_TRUE(d);
  EXPECT_EQ(d->diagnostics.skipped_ticks,3);EXPECT_TRUE(d->diagnostics.receipt_matched);
  EXPECT_FALSE(d->diagnostics.receipt_immediate);EXPECT_FALSE(d->diagnostics.receipt_direction_applied);
  EXPECT_EQ(d->action.intent.direction,Dir::Right);EXPECT_EQ(core.world().terrain({8,4}),Terrain::Free);
}
TEST(RecoveryContinuity,GapBreaksAlternatingPositionEvidence){
  BotCore core;auto o=frame();std::optional<Decision> d;
  for(Tick i=0;i<7;++i){o.tick=i<4?i:i+2;o.position=i%2?Cell{5,4}:Cell{4,4};
    d=core.process(o,at(static_cast<int>(o.tick)*100),select_wait);ASSERT_TRUE(d);
    if(i==4){EXPECT_EQ(d->diagnostics.skipped_ticks,2);}}
  EXPECT_FALSE(core.world().blocked({5,4},8));EXPECT_EQ(d->action.tick,8);
}
TEST(RecoveryContinuity,LongPauseKeepsTickAndDoesNotAdvanceWorldForDuplicate){
  BotCore core;auto o=frame();ASSERT_TRUE(core.process(o,at(0),select_wait));
  auto duplicate=core.process(o,at(4000),select_wait);ASSERT_TRUE(duplicate);EXPECT_TRUE(duplicate->duplicate);
  o.tick=1;auto d=core.process(o,at(6000),select_wait);ASSERT_TRUE(d);
  EXPECT_TRUE(d->diagnostics.resumed_after_silence);EXPECT_EQ(d->diagnostics.skipped_ticks,0);
  EXPECT_DOUBLE_EQ(*d->diagnostics.arrival_interval_ms,6000);EXPECT_EQ(core.world().visits({4,4}),2);
  EXPECT_TRUE(d->diagnostics.receipt_missing);EXPECT_EQ(d->action.tick,1);
}
TEST(RecoveryRestart,LateJoinDoesNotAssociatePreviousProcessReceiptOrGuessBirthplace){
  BotCore core;auto o=frame(73);o.position={16,15};o.receipt=Receipt{72,ActionType::Move,true,Dir::Left,{}};
  auto d=core.process(o,at(0),select_right);ASSERT_TRUE(d);EXPECT_TRUE(d->session_changed);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Unconfirmed);EXPECT_FALSE(d->diagnostics.receipt_matched);
  EXPECT_FALSE(d->diagnostics.receipt_direction_applied);EXPECT_EQ(d->action.tick,73);
  EXPECT_EQ(core.world().terrain({16,15}),Terrain::Free);
  o.tick=74;o.position={17,15};o.receipt=Receipt{73,ActionType::Move,true,Dir::Right,{}};
  d=core.process(o,at(100),select_right);ASSERT_TRUE(d);EXPECT_EQ(d->previous_receipt,ReceiptKind::Confirmed);
  EXPECT_TRUE(d->diagnostics.receipt_direction_applied);EXPECT_FALSE(d->diagnostics.receipt_missing);
}
TEST(RecoveryEvidence,ShorterAvoidanceCannotEraseLongerReservation){
  World world;world.reset({20,20});world.block_temporarily({5,4},12);world.block_temporarily({5,4},4);
  EXPECT_TRUE(world.blocked({5,4},11));EXPECT_FALSE(world.blocked({5,4},12));
  EXPECT_EQ(world.terrain({5,4}),Terrain::Unknown);
}
TEST(FinalActionGuard,DynamicWallChecksPhysicalDirectionIncludingReversal){
  for(bool reversed:{false,true}){
    BotCore core;auto o=frame();int checks=0;
    auto select=[&](const Observation&,ActionIntent,const PlanContext&){
      return PlanOverride{{ActionType::Move,reversed?Dir::Left:Dir::Right},"selected_move",reversed,
        [&](Cell p,Tick t){++checks;EXPECT_EQ(p,(Cell{5,4}));EXPECT_EQ(t,0);return true;}};
    };
    auto d=core.process(o,at(0),select);ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
    EXPECT_EQ(d->diagnostics.final_guard_reason,"event_barrier");EXPECT_EQ(checks,1);
    d=core.process(o,at(20),select);ASSERT_TRUE(d);EXPECT_TRUE(d->duplicate);
    EXPECT_EQ(d->action.intent.type,ActionType::Wait);EXPECT_EQ(checks,1);
  }
}
TEST(FinalActionGuard,DynamicWallAllowsOpenCellAndExceptionProducesWait){
  for(bool throws:{false,true}){
    BotCore core;
    auto select=[&](const Observation&,ActionIntent,const PlanContext&){
      return PlanOverride{{ActionType::Move,Dir::Right},"selected_move",false,
        [&](Cell,Tick)->bool{if(throws)throw std::runtime_error("private detail");return false;}};
    };
    auto d=core.process(frame(),at(0),select);ASSERT_TRUE(d);
    EXPECT_EQ(d->action.intent.type,throws?ActionType::Wait:ActionType::Move);
    EXPECT_EQ(d->diagnostics.final_guard_reason,throws?"event_check_failed":"");
  }
}

TEST(RecoveryPublication,DroppedLocalActionCannotConfirmAnImplicitWait){
  CoreConfig cfg;cfg.require_publication_confirmation=true;BotCore core(cfg);auto o=frame();
  ASSERT_TRUE(core.process(o,at(0),select_wait));
  o.tick=1;o.receipt=Receipt{0,ActionType::Wait,false,{},{}};
  auto d=core.process(o,at(100),select_wait);ASSERT_TRUE(d);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Unconfirmed);EXPECT_FALSE(d->diagnostics.receipt_matched);
  core.note_published(1);o.tick=2;o.receipt=Receipt{1,ActionType::Wait,false,{},{}};
  d=core.process(o,at(200),select_wait);ASSERT_TRUE(d);EXPECT_EQ(d->previous_receipt,ReceiptKind::Confirmed);
}
TEST(RecoveryPublication,CachedResumeCanConfirmSuccessfulResend){
  CoreConfig cfg;cfg.require_publication_confirmation=true;BotCore core(cfg);auto o=frame();
  ASSERT_TRUE(core.process(o,at(0),select_right));
  auto duplicate=core.process(o,at(5000),select_right);ASSERT_TRUE(duplicate);EXPECT_TRUE(duplicate->duplicate);
  core.note_published(0);o.tick=1;o.position={5,4};o.receipt=Receipt{0,ActionType::Move,true,Dir::Right,{}};
  auto d=core.process(o,at(5100),select_right);ASSERT_TRUE(d);EXPECT_EQ(d->previous_receipt,ReceiptKind::Confirmed);
}
TEST(RecoveryTerminal,FinalReceiptSurvivesActionSuppression){
  CoreConfig cfg;cfg.horizon=100;BotCore core(cfg);auto o=frame(99);
  ASSERT_TRUE(core.process(o,at(0),select_wait));o.tick=100;o.receipt=Receipt{99,ActionType::Wait,false,{},{}};
  auto d=core.process(o,at(100),select_right);ASSERT_TRUE(d);EXPECT_TRUE(d->diagnostics.terminal_state);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Confirmed);EXPECT_EQ(d->reason,"terminal_receipt_only");
  EXPECT_FALSE(core.process(o,at(200),select_right));
  o.session_id="immediate-reopen";o.tick=0;o.receipt.reset();
  d=core.process(o,at(201),select_wait);ASSERT_TRUE(d);EXPECT_TRUE(d->session_changed);
  EXPECT_FALSE(d->diagnostics.terminal_state);EXPECT_EQ(d->action.tick,0);
}
