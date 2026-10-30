#include "rmtc_bot/core.hpp"
#include "rmtc_bot/protocol.hpp"
#include "rmtc_bot/respawn.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace rmtc;
using Json=nlohmann::json;
namespace {
std::string rel(int dx=0,int dy=0){
  std::string s;
  for(int y=0;y<9;++y){if(y)s+='\n';for(int x=0;x<9;++x)
    s+=(x==4&&y==4)?'1':(x==4+dx&&y==4+dy)?'2':'0';}
  return s;
}
Json frame(){
  return {{"tick",0},{"session_id","s1"},{"vision_radius",4},
    {"self",{{"team","A"},{"robot_id","A"},{"position",{1,1}},{"hp",100},
      {"alive",true},{"protected",false},{"last_action",nullptr}}},
    {"map",{{"width",20},{"height",20}}},{"visible_entities",Json::array()}};
}
Observation obs(){return std::get<Observation>(parse_observation(frame().dump(),"A"));}
MonoTime time_ms(int milliseconds){return MonoTime{}+std::chrono::milliseconds(milliseconds);}
Entity resource(Cell p,double value=10){Entity e;e.type=EntityType::Resource;e.position=p;e.value=value;e.id="r";return e;}
}
TEST(Relmap,ProtocolExampleAndSameCell){
  EXPECT_EQ(std::get<Cell>(parse_relative_position(rel(1,-2))),(Cell{1,-2}));
  EXPECT_EQ(std::get<Cell>(parse_relative_position(rel())),(Cell{0,0}));
}
TEST(Relmap,RejectMalformed){
  auto s=rel();s+='\n';EXPECT_TRUE(std::holds_alternative<Error>(parse_relative_position(s)));
  s=rel();s[44]='0';EXPECT_TRUE(std::holds_alternative<Error>(parse_relative_position(s)));
  s=rel(1,0);s[0]='2';EXPECT_TRUE(std::holds_alternative<Error>(parse_relative_position(s)));
  s=rel();s[9]=' ';EXPECT_TRUE(std::holds_alternative<Error>(parse_relative_position(s)));
}
TEST(Parser,AcceptsMinimalAndOptionalFields){
  auto p=parse_observation(frame().dump(),"A");ASSERT_TRUE(std::holds_alternative<Observation>(p));
  auto o=std::get<Observation>(p);EXPECT_TRUE(o.state_complete);EXPECT_TRUE(o.entities_complete);
  EXPECT_FALSE(o.resource_value);EXPECT_FALSE(o.receipt);EXPECT_FALSE(o.position_reason);
}
TEST(Parser,RejectsBadTickTypes){
  for(auto value:{Json(true),Json(1.0),Json(-1),Json("1"),Json(18446744073709551615ULL)}){
    auto f=frame();f["tick"]=value;EXPECT_TRUE(std::holds_alternative<Error>(parse_observation(f.dump(),"A")));
  }
}
TEST(Parser,RejectsBadSessionPositionAndSeat){
  auto f=frame();f["session_id"]=nullptr;EXPECT_TRUE(std::holds_alternative<Error>(parse_observation(f.dump(),"A")));
  f=frame();f["self"]["position"]={20,1};EXPECT_TRUE(std::holds_alternative<Error>(parse_observation(f.dump(),"A")));
  EXPECT_TRUE(std::holds_alternative<Error>(parse_observation(frame().dump(),"B")));
  EXPECT_TRUE(std::holds_alternative<Error>(parse_observation("{","A")));
}
TEST(Parser,RejectsDuplicateKeysAndHugeInput){
  auto f=frame().dump();f.insert(1,"\"tick\":7,");
  auto p=parse_observation(f,"A");ASSERT_TRUE(std::holds_alternative<Error>(p));EXPECT_EQ(std::get<Error>(p).code,ErrorCode::DuplicateKey);
  EXPECT_TRUE(std::holds_alternative<Error>(parse_observation(std::string(2097153,' '),"A")));
}
TEST(Parser,MalformedEntityDoesNotClearMemory){
  auto f=frame();f["visible_entities"]=Json::array({{{"type","obstacle"},{"position","bad"}}});
  auto p=parse_observation(f.dump(),"A");ASSERT_TRUE(std::holds_alternative<Observation>(p));
  EXPECT_FALSE(std::get<Observation>(p).entities_complete);
}
TEST(Parser,RelativeWorldAndRadius){
  auto f=frame();f["visible_entities"]=Json::array({{{"type","resource_high"},{"id","r1"},{"value",20},{"position",rel(3,2)}}});
  auto o=std::get<Observation>(parse_observation(f.dump(),"A"));ASSERT_EQ(o.entities.size(),1U);EXPECT_EQ(o.entities[0].position,(Cell{4,3}));
  f["vision_radius"]=1;o=std::get<Observation>(parse_observation(f.dump(),"A"));EXPECT_TRUE(o.entities.empty());EXPECT_FALSE(o.entities_complete);
}
TEST(Parser,MissingOrInvalidRadiusCannotIntroduceEntitiesOrEraseMemory){
  auto original=obs();World world;world.reset(original.map);
  original.entities={resource({2,1})};world.update(original);
  for(const auto& radius:{Json(nullptr),Json(true),Json(-1),Json(5),Json("4")}){
    auto f=frame();f["vision_radius"]=radius;
    f["visible_entities"]=Json::array({{{"type","resource_high"},{"id","phantom"},{"value",20},{"position",rel(3,2)}}});
    auto o=std::get<Observation>(parse_observation(f.dump(),"A"));
    EXPECT_TRUE(o.state_complete);EXPECT_FALSE(o.entities_complete);EXPECT_TRUE(o.entities.empty());
    world.update(o);ASSERT_EQ(world.resources().size(),1U);EXPECT_EQ(world.resources().begin()->second.id,"r");
  }
  auto f=frame();f.erase("vision_radius");
  f["visible_entities"]=Json::array({{{"type","robot"},{"robot_id","B"},{"position",rel(1,0)}}});
  auto o=std::get<Observation>(parse_observation(f.dump(),"A"));EXPECT_TRUE(o.entities.empty());EXPECT_FALSE(o.entities_complete);
}
TEST(Parser,MissingAliveAllowsTrustedWaitOnly){
  auto f=frame();f["self"].erase("alive");auto o=std::get<Observation>(parse_observation(f.dump(),"A"));
  EXPECT_FALSE(o.state_complete);BotCore core;auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
}
TEST(Codec,WaitOmitsDirection){
  Action a{9,"session",{ActionType::Wait,Dir::Up}};auto j=Json::parse(encode_action(a));
  EXPECT_FALSE(j.contains("direction"));EXPECT_EQ(j["tick"],9);EXPECT_EQ(j["session_id"],"session");
  a.intent={ActionType::Move,Dir::Left};EXPECT_EQ(Json::parse(encode_action(a))["direction"],"left");
}
TEST(Session,OldTicksDuplicatesAndMedian){
  SessionClock gate;auto o=obs();EXPECT_EQ(gate.accept(o,time_ms(0)),GateResult::NewSession);
  for(int i=1;i<=5;++i){o.tick=i;EXPECT_EQ(gate.accept(o,time_ms(i*500)),GateResult::NewTick);}
  EXPECT_DOUBLE_EQ(gate.period_ms(),500);
  EXPECT_EQ(gate.accept(o,time_ms(2600)),GateResult::Duplicate);
  o.tick=1;EXPECT_EQ(gate.accept(o,time_ms(5000)),GateResult::Drop);
  o.tick=2;EXPECT_EQ(gate.accept(o,time_ms(5500)),GateResult::Drop);
  EXPECT_EQ(gate.generation(),1U);
}
TEST(Session,NewSessionAndRetiredSession){
  SessionClock gate;auto o=obs();gate.accept(o,time_ms(0));
  o.session_id="s2";EXPECT_EQ(gate.accept(o,time_ms(50)),GateResult::NewSession);
  o.tick=1;EXPECT_EQ(gate.accept(o,time_ms(250)),GateResult::NewTick);
  EXPECT_EQ(gate.generation(),2U);o.session_id="s1";o.tick=50;
  EXPECT_EQ(gate.accept(o,time_ms(2000)),GateResult::Drop);
}
TEST(Session,JumpAndStallDoNotChangePeriod){
  SessionClock gate;auto o=obs();gate.accept(o,time_ms(0));o.tick=4;gate.accept(o,time_ms(400));
  EXPECT_DOUBLE_EQ(gate.period_ms(),100);o.tick=5;gate.accept(o,time_ms(10000));EXPECT_DOUBLE_EQ(gate.period_ms(),100);
}
TEST(World,InvalidAndDeadFramesDoNotEraseKnownResource){
  World w;auto o=obs();w.reset(o.map);o.entities.push_back(resource({2,1}));w.update(o);
  EXPECT_EQ(w.resources().size(),1U);o.entities.clear();o.entities_complete=false;++o.tick;w.update(o);EXPECT_EQ(w.resources().size(),1U);
  o.entities_complete=true;o.alive=false;++o.tick;w.update(o);EXPECT_EQ(w.resources().size(),1U);
  o.alive=true;++o.tick;w.update(o);EXPECT_TRUE(w.resources().empty());
}
TEST(Planner,ChoosesValuePerDistance){
  BotCore core;auto o=obs();o.entities={resource({2,1},10),resource({1,3},20)};
  auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->reason,"resource");EXPECT_EQ(d->action.intent.direction,Dir::Down);
}
TEST(Planner,Uses100TickHorizonAndStopsAtTerminal){
  BotCore core;auto o=obs();o.tick=99;o.entities={resource({1,3},20),resource({2,1},10)};
  auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Right);
  o.tick=101;d=core.process(o,time_ms(200));ASSERT_TRUE(d);EXPECT_TRUE(d->diagnostics.terminal_state);
}
TEST(Planner,ExploresAndWaitsOnFullyKnownEmptyMap){
  BotCore core;auto o=obs();auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Move);
  BotCore small;o.map={3,3};d=small.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
}
TEST(Planner,DeadWaitsAndDuplicateDoesNotAdvance){
  BotCore core;auto o=obs();o.alive=false;auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
  int visits=core.world().visits(o.position);d=core.process(o,time_ms(50));ASSERT_TRUE(d);EXPECT_TRUE(d->duplicate);EXPECT_EQ(core.world().visits(o.position),visits);
  for(int i=0;i<6;++i){core.process(o,time_ms(60+i));}
  EXPECT_FALSE(core.process(o,time_ms(80)));
}
TEST(Planner,DoesNotMoveIntoVisibleRobotOrObstacle){
  BotCore core;auto o=obs();o.map={3,1};o.position={0,0};o.entities={resource({2,0},20)};
  Entity obstacle;obstacle.position={1,0};o.entities.push_back(obstacle);
  auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.type,ActionType::Wait);
}
TEST(Planner,DirectionCompensationFromSuccessfulReceipt){
  BotCore core;auto o=obs();o.entities={resource({2,1},10)};auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Right);
  o.tick=1;o.position={0,1};o.entities={resource({2,1},10)};o.receipt=Receipt{0,ActionType::Move,true,Dir::Left,std::nullopt};
  d=core.process(o,time_ms(100));ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Left);
  EXPECT_EQ(d->previous_receipt,ReceiptKind::Confirmed);
}
TEST(Planner,InitialResourceUnderfootIsCollectedByWaiting){
  BotCore core;auto o=obs();o.entities={resource(o.position,20)};
  auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);
  EXPECT_EQ(d->action.intent.type,ActionType::Wait);EXPECT_EQ(d->reason,"resource_underfoot");
}
TEST(Planner,PolicyVariantsKeepCurrentTickAndLegalMoves){
  for(auto policy:{CollectorPolicy::Greedy,CollectorPolicy::Lookahead,CollectorPolicy::Frontier}){
    CoreConfig config;config.policy=policy;BotCore core(config);auto o=obs();
    o.entities={resource({2,1},10),resource({1,3},20),resource({4,3},10)};
    auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);EXPECT_EQ(d->action.tick,o.tick);
    EXPECT_EQ(d->action.session_id,o.session_id);EXPECT_EQ(d->action.intent.type,ActionType::Move);
    ASSERT_TRUE(d->action.intent.direction);EXPECT_TRUE(o.map.contains(step(o.position,*d->action.intent.direction)));
  }
}
TEST(Planner,SwitchingSessionClearsResourceMemory){
  BotCore core;auto o=obs();o.entities={resource({2,1},10)};core.process(o,time_ms(0));
  ASSERT_EQ(core.world().resources().size(),1U);o.session_id="next";o.entities.clear();o.entities_complete=false;
  auto d=core.process(o,time_ms(250));ASSERT_TRUE(d);EXPECT_TRUE(d->session_changed);EXPECT_TRUE(core.world().resources().empty());
}
TEST(Planner,OccupationFailureIsTemporaryNotStaticWall){
  BotCore core;auto o=obs();o.entities={resource({2,1},20)};auto d=core.process(o,time_ms(0));ASSERT_TRUE(d);
  o.tick=1;o.receipt=Receipt{0,ActionType::Move,false,std::nullopt,std::string("E_BLOCKED")};
  core.process(o,time_ms(100));EXPECT_NE(core.world().terrain({2,1}),Terrain::Wall);
  EXPECT_TRUE(core.world().blocked({2,1},1));EXPECT_FALSE(core.world().blocked({2,1},3));
}
TEST(Planner,InSessionMapMutationCannotResendWrongTick){
  BotCore core;auto o=obs();ASSERT_TRUE(core.process(o,time_ms(0)));
  o.tick=1;o.map.width=24;EXPECT_FALSE(core.process(o,time_ms(100)));
  EXPECT_FALSE(core.process(o,time_ms(110)));
  o.map.width=20;auto d=core.process(o,time_ms(120));ASSERT_TRUE(d);EXPECT_EQ(d->action.tick,1);
}
TEST(Planner,RepeatedOldReceiptDoesNotRepeatedlyChangeMapping){
  BotCore core;auto o=obs();o.entities={resource({2,1},10)};core.process(o,time_ms(0));
  o.tick=1;o.receipt=Receipt{0,ActionType::Move,false,std::nullopt,std::string("E_BLOCKED")};
  auto d=core.process(o,time_ms(100));ASSERT_TRUE(d);EXPECT_TRUE(d->previous_receipt);
  o.tick=2;d=core.process(o,time_ms(200));ASSERT_TRUE(d);EXPECT_FALSE(d->previous_receipt);
}
TEST(Respawn,InitialSpawnAndUpRightDownLeft){
  MapSize m{5,5};auto clear=[](Cell){return false;};
  EXPECT_EQ(choose_respawn({2,2},m,clear,{}),(Cell{2,2}));
  EXPECT_EQ(choose_respawn({2,2},m,clear,{{2,2}}),(Cell{2,1}));
  EXPECT_EQ(choose_respawn({2,2},m,clear,{{2,2},{2,1}}),(Cell{3,2}));
  EXPECT_EQ(choose_respawn({2,2},m,clear,{{2,2},{2,1},{3,2}}),(Cell{2,3}));
}
TEST(Respawn,ObstaclesDoNotBlockSearch){
  MapSize m{5,5};auto blocked=[](Cell c){return std::abs(c.x-2)+std::abs(c.y-2)<=1;};
  EXPECT_EQ(choose_respawn({2,2},m,blocked,{}),(Cell{2,0}));
}
TEST(Respawn,SameDistanceUsesBreadthFirstExpansionOrder){
  auto blocked=[](Cell c){return std::abs(c.x-2)+std::abs(c.y-2)<=1||c==Cell{2,0};};
  EXPECT_EQ(choose_respawn({2,2},{5,5},blocked,{}),(Cell{3,1}));
}
TEST(Respawn,SimultaneousSeatsReserveLandings){
  auto land=choose_simultaneous_respawns({{'D',{1,1}},{'B',{1,1}},{'A',{1,1}},{'C',{1,1}}},
       {3,3},[](Cell){return false;},{});
  EXPECT_EQ(land.at('A'),(Cell{1,1}));EXPECT_EQ(land.at('B'),(Cell{1,0}));
  EXPECT_EQ(land.at('C'),(Cell{2,1}));EXPECT_EQ(land.at('D'),(Cell{1,2}));
}
TEST(Respawn,ResourcesDoNotExcludeLandingAndAllBlockedIsEmpty){
  EXPECT_EQ(choose_respawn({0,0},{1,1},[](Cell){return false;},{}),(Cell{0,0}));
  EXPECT_FALSE(choose_respawn({0,0},{1,1},[](Cell){return true;},{}));
}

TEST(VisibilityCounts,MatchesNaiveCountsAtEveryRadiusAndMapEdge){
  for(auto map:std::vector<MapSize>{{1,1},{3,7},{24,24},{64,64}}){
    World world;world.reset(map);
    for(int k=0;k<20;++k){Observation o;o.map=map;o.position={k*7%map.width,k*11%map.height};o.tick=k;o.alive=true;o.entities_complete=true;o.vision_radius=1;
      for(int y=std::max(0,o.position.y-1);y<=std::min(map.height-1,o.position.y+1);++y)
        for(int x=std::max(0,o.position.x-1);x<=std::min(map.width-1,o.position.x+1);++x)if((x+3*y)%5==0&&Cell{x,y}!=o.position){Entity e;e.type=EntityType::Obstacle;e.position={x,y};o.entities.push_back(e);}
      world.update(o);
    }
    for(int radius=0;radius<=4;++radius){auto counts=unknown_footprint_counts(world,radius);ASSERT_EQ(counts.size(),static_cast<std::size_t>(map.width*map.height));
      for(int y=0;y<map.height;++y)for(int x=0;x<map.width;++x){int naive=0;
        for(int yy=std::max(0,y-radius);yy<=std::min(map.height-1,y+radius);++yy)for(int xx=std::max(0,x-radius);xx<=std::min(map.width-1,x+radius);++xx)naive+=world.terrain({xx,yy})==Terrain::Unknown;
        EXPECT_EQ(counts[map.index({x,y})],naive);
      }
    }
  }
}
