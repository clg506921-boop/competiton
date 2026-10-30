#include "rmtc_bot/pursuit.hpp"
#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
namespace {
Observation frame(Tick tick){
  Observation o;o.session_id="pursuit";o.seat="A";o.map={40,40};o.tick=tick;o.position={10,10};
  o.alive=true;o.hp=100;o.protected_now=false;o.state_complete=o.entities_complete=true;
  o.vision_radius=4;o.effective_damage=200;o.resource_value=120;return o;
}
Entity robot(const std::string& seat,Cell p){Entity e;e.type=EntityType::Robot;e.robot_id=seat;e.position=p;e.hp=100;e.protected_now=false;return e;}
void moving_frames(PublicPursuit& memory){for(int t=0;t<3;++t){auto o=frame(t);o.entities={robot("B",{9+t,11}),robot("C",{7+t,11})};memory.observe(o);}}
const std::function<bool(Cell,Tick)> safe=[](Cell,Tick){return true;};
}
TEST(Pursuit,RequiresThreeConsecutiveVisibleFramesBeforeExtrapolating){
  PublicPursuit p;
  for(int t=0;t<3;++t){auto o=frame(t);o.entities={robot("B",{9+t,11})};p.observe(o);EXPECT_EQ(p.moving("B"),t==2);}
  EXPECT_EQ(p.forecast("B",2,safe),std::optional<Cell>(Cell{13,11}));
  EXPECT_FALSE(p.forecast("B",3,safe));
}
TEST(Pursuit,GapSessionDeathTeleportProtectionAndIncompleteFramesInvalidateMotion){
  for(int change=0;change<8;++change){PublicPursuit p;moving_frames(p);auto o=frame(3);o.entities={robot("B",{12,11}),robot("C",{10,11})};
    if(change==0)o.tick=4;if(change==1)o.session_id="another";if(change==2)o.alive=false;
    if(change==3)o.position={15,15};if(change==4)o.entities[0].protected_now=true;
    if(change==5)o.entities_complete=false;if(change==6)o.entities.clear();if(change==7)o.vision_radius=0;
    p.observe(o);EXPECT_FALSE(p.moving("B"))<<change;
  }
}
TEST(Pursuit,UnknownBlockedOrHazardousIntermediateCellCancelsForecast){
  PublicPursuit p;moving_frames(p);
  EXPECT_FALSE(p.forecast("B",2,[](Cell c,Tick){return c!=Cell{12,11};}));
  EXPECT_FALSE(p.forecast("B",2,[](Cell,Tick t){return t<4;}));
  auto o=frame(3);o.entities={robot("B",{12,11}),robot("D",{13,11})};p.observe(o);
  EXPECT_FALSE(p.forecast("B",1,safe));
}
TEST(Pursuit,ThirdPartyCoMotionSupportsPerpendicularInterceptionOnly){
  PublicPursuit p;moving_frames(p);auto predicted=p.forecast("B",1,safe);ASSERT_TRUE(predicted);
  EXPECT_TRUE(p.crossfire("B",{12,10},*predicted));
  EXPECT_FALSE(p.crossfire("B",{14,11},*predicted));
  EXPECT_FALSE(p.crossfire("B",{12,7},*predicted));
}
TEST(Pursuit,StandingOrTurningThirdPartyIsNotAssumedToBePursuing){
  PublicPursuit p;
  for(int t=0;t<3;++t){auto o=frame(t);o.entities={robot("B",{9+t,11}),robot("C",{8,11})};p.observe(o);}
  EXPECT_FALSE(p.crossfire("B",{12,10},{12,11}));
  auto o=frame(3);o.entities={robot("B",{11,12}),robot("C",{9,11})};p.observe(o);EXPECT_FALSE(p.moving("B"));
}
TEST(Pursuit,MissingHpOrProtectionAndHpIncreaseCannotBecomeVelocityEvidence){
  for(int change=0;change<3;++change){PublicPursuit p;moving_frames(p);auto o=frame(3);auto b=robot("B",{12,11});
    if(change==0)b.hp.reset();if(change==1)b.protected_now.reset();if(change==2)b.hp=110;
    o.entities={b};p.observe(o);EXPECT_FALSE(p.moving("B"));}
}
TEST(Pursuit,EqualSpeedChaseDoesNotInventAnApproachFromOppositeMotion){
  PublicPursuit p;
  for(int t=0;t<3;++t){auto o=frame(t);o.entities={robot("B",{9+t,11}),robot("C",{12-t,12})};p.observe(o);}
  EXPECT_FALSE(p.crossfire("B",{12,10},{12,11}));
}

TEST(PursuitRuntime,PublicThirdPartyChaseCanSelectAPerpendicularCutoff){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(int t=0;t<3;++t){auto o=frame(t);o.position={10+t,9};o.entities={robot("B",{9+t,11}),robot("C",{6+t,11})};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(t*500));ASSERT_TRUE(d);
    if(t==2){EXPECT_EQ(d->reason,"P7_crossfire_intercept");EXPECT_EQ(d->action.intent.direction,Dir::Down);}
  }
}
TEST(PursuitRuntime,LowHpCannotUseThirdPartyChaseToIgnorePredictedLethalFire){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime bot(config);
  for(int t=0;t<3;++t){auto o=frame(t);o.position={10+t,9};o.hp=20;o.entities={robot("B",{9+t,11}),robot("C",{6+t,11})};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(t*500));ASSERT_TRUE(d);
    if(t==2)EXPECT_NE(d->reason,"P7_crossfire_intercept");
  }
}

TEST(PursuitRuntime,EqualSpeedTailDoesNotClaimStaticFiringProgress){
  RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);
  for(int t=0;t<3;++t){auto o=frame(100+t);o.position={10+t,10};o.entities={robot("B",{14+t,10})};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
    if(t==2){EXPECT_NE(d->reason,"P7_nearby_firing_position");EXPECT_NE(d->reason,"P7_tiebreak_firing_position");}
  }
}
TEST(PursuitRuntime,StandingTargetRetainsItsExistingFiringOffer){
  RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);
  for(int t=0;t<3;++t){auto o=frame(100+t);o.position={10+t,10};o.entities={robot("B",{16,10})};
    auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);
    if(t==2)EXPECT_EQ(d->reason,"P7_nearby_firing_position");
  }
}
TEST(PursuitRuntime,CurrentInRangeShotIsNotSuppressedByEarlierFollowing){
  RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime bot(c);
  for(int t=0;t<3;++t){auto o=frame(100+t);o.position={10+t,10};o.entities={robot("B",{14+t,10})};bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));}
  auto o=frame(103);o.position={13,10};o.entities={robot("B",{15,10})};
  auto d=bot.process(o,MonoTime{}+std::chrono::milliseconds(o.tick*500));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Attack);
}
