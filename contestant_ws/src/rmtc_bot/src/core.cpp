#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace rmtc {
std::vector<int> unknown_footprint_counts(const World& world,int radius){
  const auto map=world.size();const int w=map.width,h=map.height,stride=w+1;
  std::vector<int> counts(w*h,0);if(radius<0||w<1||h<1)return counts;
  radius=std::min(radius,std::max(w,h));std::vector<int> sum((w+1)*(h+1),0);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    sum[(y+1)*stride+x+1]=(world.terrain({x,y})==Terrain::Unknown)+sum[y*stride+x+1]+sum[(y+1)*stride+x]-sum[y*stride+x];
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){
    int left=std::max(0,x-radius),right=std::min(w,x+radius+1),top=std::max(0,y-radius),bottom=std::min(h,y+radius+1);
    counts[y*w+x]=sum[bottom*stride+right]-sum[top*stride+right]-sum[bottom*stride+left]+sum[top*stride+left];
  }return counts;
}
double SessionClock::period_ms() const {
  if(intervals_.empty())return initial_ms_;
  std::array<double,5> values{};std::copy(intervals_.begin(),intervals_.end(),values.begin());
  std::sort(values.begin(),values.begin()+intervals_.size());
  auto middle=intervals_.size()/2;
  return intervals_.size()%2?values[middle]:(values[middle-1]+values[middle])/2;
}
GateResult SessionClock::accept(const Observation& o,MonoTime now) {
  if(o.session_id!=session_) {
    if(retired_.count(o.session_id))return GateResult::Drop;
    if(!session_.empty()){
      // v1.1 requires a fresh ID on reopen. Accept its first frame immediately;
      // delayed frames from an already retired session remain inadmissible.
      retired_.insert(session_);
    }
    session_=o.session_id;tick_=o.tick;advanced_=now;intervals_.clear();
    ++generation_;return GateResult::NewSession;
  }
  if(o.tick<tick_)return GateResult::Drop;
  if(o.tick==tick_)return GateResult::Duplicate;
  double elapsed=std::chrono::duration<double,std::milli>(now-advanced_).count();
  if(o.tick==tick_+1&&elapsed>0&&elapsed<=5000){
    intervals_.push_back(elapsed);if(intervals_.size()>5)intervals_.pop_front();
  }
  tick_=o.tick;advanced_=now;return GateResult::NewTick;
}
void World::reset(MapSize map){
  map_=map;auto n=static_cast<std::size_t>(map.width*map.height);
  terrain_.assign(n,Terrain::Unknown);blocked_until_.assign(n,0);visits_.assign(n,0);
  wall_first_.assign(n,-1);wall_last_.assign(n,-1);
  observed_.assign(n,-1);
  recent_.clear();resources_.clear();
}
Terrain World::terrain(Cell c) const { return map_.contains(c)?terrain_[map_.index(c)]:Terrain::Wall; }
bool World::blocked(Cell c,Tick tick,bool ignore_wall) const {
  return !map_.contains(c)||(!ignore_wall&&terrain(c)==Terrain::Wall)||blocked_until_[map_.index(c)]>tick;
}
Tick World::first_wall_seen(Cell c)const{return map_.contains(c)?wall_first_[map_.index(c)]:-1;}
Tick World::last_wall_seen(Cell c)const{return map_.contains(c)?wall_last_[map_.index(c)]:-1;}
Tick World::last_observed(Cell c)const{return map_.contains(c)?observed_[map_.index(c)]:-1;}
void World::forget_expired_wall(Cell c,Tick start,Tick until,Tick now){
  if(!map_.contains(c)||now<until)return;
  auto i=map_.index(c);
  // Pre-existing walls and obstacles seen after expiration retain their evidence.
  if(terrain_[i]==Terrain::Wall&&wall_first_[i]>=start&&wall_last_[i]<until){
    terrain_[i]=Terrain::Unknown;wall_first_[i]=wall_last_[i]=-1;
  }
}
int World::visits(Cell c) const {return map_.contains(c)?visits_[map_.index(c)]:0;}
void World::block_temporarily(Cell c,Tick until){
  if(map_.contains(c))blocked_until_[map_.index(c)]=std::max(blocked_until_[map_.index(c)],until);
}
void World::update(const Observation& o){
  if(!o.alive.value_or(false))return; // Dead frames contain no surrounding entities.
  int self=map_.index(o.position);terrain_[self]=Terrain::Free;wall_first_[self]=wall_last_[self]=-1;
  observed_[self]=o.tick;
  recent_.push_back(self);++visits_[self];
  if(recent_.size()>30){--visits_[recent_.front()];recent_.pop_front();}
  if(o.entities_complete&&o.vision_radius){
    int r=*o.vision_radius;
    for(int y=std::max(0,o.position.y-r);y<=std::min(map_.height-1,o.position.y+r);++y)
      for(int x=std::max(0,o.position.x-r);x<=std::min(map_.width-1,o.position.x+r);++x){
        int i=map_.index({x,y});terrain_[i]=Terrain::Free;resources_.erase(i);observed_[i]=o.tick;
      }
  }
  for(const auto& e:o.entities){
    int i=map_.index(e.position);
    observed_[i]=o.tick;
    if(e.type==EntityType::Obstacle){terrain_[i]=Terrain::Wall;if(wall_first_[i]<0)wall_first_[i]=o.tick;wall_last_[i]=o.tick;}
    else if(e.type==EntityType::Resource){resources_[i]=e;terrain_[i]=Terrain::Free;wall_first_[i]=wall_last_[i]=-1;}
  }
  if(o.entities_complete&&o.vision_radius){int r=*o.vision_radius;
    for(int y=std::max(0,o.position.y-r);y<=std::min(map_.height-1,o.position.y+r);++y)
      for(int x=std::max(0,o.position.x-r);x<=std::min(map_.width-1,o.position.x+r);++x){
        int i=map_.index({x,y});if(terrain_[i]!=Terrain::Wall)wall_first_[i]=wall_last_[i]=-1;
      }
  }
  // A resource may still be under the car at the initial state. Keep an explicitly
  // reported one so the planner can WAIT and collect it rather than moving away.
}
BotCore::BotCore(CoreConfig config):config_(config),clock_(config.tick_ms.value_or(config.stage==StageMode::Battle?500:100)){}
ActionIntent BotCore::guard(ActionIntent a,const Observation& o,const MovementBlocked& blocked,std::string& reason) const {
  auto reject=[&](const char* code){reason=code;return ActionIntent{};};
  if(!o.state_complete||!o.alive.value_or(false))return reject("dead_or_incomplete");
  if(a.type==ActionType::Attack||a.type==ActionType::Defend){
    if(config_.stage==StageMode::Collect||config_.stage==StageMode::Diagnostic||!battle_seen_||o.protected_now.value_or(true))return reject("combat_unavailable");
    if(a.type==ActionType::Attack&&!a.direction)return reject("missing_direction");
  }
  if(a.type==ActionType::Move){
    if(!a.direction)return reject("missing_direction");
    Dir actual=reversed_?opposite(*a.direction):*a.direction;
    Cell destination=step(o.position,actual);
    if(world_.blocked(destination,o.tick))return reject("terrain_blocked");
    for(const auto& e:o.entities)if(e.type==EntityType::Robot&&e.position==destination)return reject("occupied");
    try{if(blocked&&blocked(destination,o.tick))return reject("event_barrier");}
    catch(...){return reject("event_check_failed");}
  }
  return a;
}
std::optional<Decision> BotCore::process(const Observation& o,MonoTime now,const PlanHook& hook){
  if(cached_&&cached_->session_id==o.session_id&&
      (world_.size().width!=o.map.width||world_.size().height!=o.map.height))return {};
  auto accepted=clock_.accept(o,now);
  if(accepted==GateResult::Drop)return {};
  if(accepted==GateResult::Duplicate){
    if(!cached_||submissions_>=8)return {};
    ++submissions_;
    return Decision{*cached_,"duplicate_cached",true,false,std::nullopt,{}};
  }
  bool fresh=accepted==GateResult::NewSession;
  Decision::Diagnostics diagnostics;
  if(!fresh&&previous_tick_){
    diagnostics.skipped_ticks=o.tick-*previous_tick_-1;
    diagnostics.arrival_interval_ms=std::chrono::duration<double,std::milli>(now-previous_received_).count();
    diagnostics.resumed_after_silence=*diagnostics.arrival_interval_ms>std::max(1000.0,5*clock_.period_ms());
    diagnostics.receipt_expected=true;
    diagnostics.receipt_missing=!o.receipt||o.receipt->tick!=*previous_tick_;
    if(!sent_.empty()){
      const auto p=sent_.back().origin;
      diagnostics.position_discontinuous=std::abs(o.position.x-p.x)+std::abs(o.position.y-p.y)>1;
    }
  }
  // Never let a partially failed new-frame update resend the previous Tick's cached action.
  cached_.reset();
  if(fresh){
    world_.reset(o.map);cached_.reset();goal_.reset();previous_tick_.reset();
    sent_.clear();positions_.clear();last_receipt_tick_=-1;
    reversed_=false;battle_seen_=config_.stage==StageMode::Battle;stalled_=0;
  }else if(world_.size().width!=o.map.width||world_.size().height!=o.map.height)return {};
  const bool respawn=o.position_reason==std::optional<std::string>("respawn");
  if(diagnostics.skipped_ticks||diagnostics.resumed_after_silence||diagnostics.position_discontinuous||respawn){
    // These samples cannot prove consecutive movement failures or oscillation.
    positions_.clear();stalled_=0;goal_.reset();
  }
  std::optional<ReceiptKind> receipt;
  if(o.receipt&&o.receipt->tick>last_receipt_tick_){
    const auto& r=*o.receipt;
    last_receipt_tick_=r.tick;
    diagnostics.receipt_fresh=true;
    diagnostics.receipt_immediate=o.tick>0&&r.tick==o.tick-1;
    diagnostics.receipt_error=safe_receipt_error(r.error);
    auto it=std::find_if(sent_.begin(),sent_.end(),[&](const auto& sent){return sent.tick==r.tick;});
    if(it!=sent_.end()&&it->published){
      diagnostics.receipt_matched=true;diagnostics.expected_receipt_action=it->intent.type;
      const bool moving=r.action==ActionType::Move&&it->intent.type==ActionType::Move;
      const bool error=!diagnostics.receipt_error.empty();
      const bool execution_failure=moving&&!r.moved&&(!error||diagnostics.receipt_error=="E_BLOCKED"||diagnostics.receipt_error=="E_OOB");
      const bool explicit_respawn_cancellation=respawn&&diagnostics.receipt_immediate&&
        o.alive==std::optional<bool>(true)&&o.protected_now==std::optional<bool>(true)&&
        r.action==ActionType::Wait&&!r.moved&&!error&&it->intent.type!=ActionType::Wait;
      if(explicit_respawn_cancellation)receipt=ReceiptKind::StateCancelled;
      else if(diagnostics.receipt_error=="E_STATE"&&it->intent.type==ActionType::Move&&!r.moved&&
          (o.alive==std::optional<bool>(false)||respawn))receipt=ReceiptKind::StateCancelled;
      else if(execution_failure)receipt=ReceiptKind::MoveFailed;
      else if(error)receipt=ReceiptKind::Rejected;
      else receipt=r.action==it->intent.type?ReceiptKind::Confirmed:ReceiptKind::Mismatched;
      const bool continuous=diagnostics.receipt_immediate&&it->alive&&o.state_complete&&o.alive.value_or(false)&&!respawn;
      if(moving&&it->intent.direction&&continuous){
        if(*receipt==ReceiptKind::Confirmed&&r.moved&&r.effective_direction){
          if(*r.effective_direction==*it->intent.direction){reversed_=false;diagnostics.receipt_direction_applied=true;}
          else if(*r.effective_direction==opposite(*it->intent.direction)){reversed_=true;diagnostics.receipt_direction_applied=true;}
          else receipt=ReceiptKind::Mismatched;
          stalled_=0;
        }else if(*receipt==ReceiptKind::MoveFailed&&o.position==it->origin){
          ++stalled_;
          // Only an immediate execution failure at the original position is evidence.
          // State cancellation/death/respawn and delayed receipts do not imply obstacles.
          if(it->attempted_direction)world_.block_temporarily(step(it->origin,*it->attempted_direction),tick_after(o.tick,2));
        }
      }
    }else receipt=ReceiptKind::Unconfirmed;
  }
  if(config_.stage==StageMode::Auto||config_.stage==StageMode::Battle)
    for(const auto& e:o.entities)if(e.type==EntityType::Robot)battle_seen_=true;
  world_.update(o);
  if(stalled_>=4){goal_.reset();stalled_=0;}
  if(o.alive.value_or(false)){
    if(respawn)positions_.clear();
    positions_.push_back(o.position);if(positions_.size()>7)positions_.pop_front();
    if(positions_.size()==7&&positions_[0]!=positions_[1]){
      bool alternating=true;
      for(std::size_t i=2;i<positions_.size();++i)if(positions_[i]!=positions_[i%2])alternating=false;
      if(alternating){goal_.reset();world_.block_temporarily(positions_[1],tick_after(o.tick,2));positions_.clear();}
    }
  }else positions_.clear();
  const Tick horizon=config_.horizon.value_or(battle_seen_?500:100);
  if(o.tick>=horizon){
    // Consume the last execution receipt before suppressing terminal actions.
    diagnostics.terminal_state=true;previous_tick_=o.tick;previous_received_=now;
    return Decision{Action{o.tick,o.session_id,{}} ,"terminal_receipt_only",false,fresh,receipt,std::move(diagnostics)};
  }
  std::string reason=config_.wait_only?"wait_validation":"collect";
  ActionIntent intent=config_.wait_only?ActionIntent{}:plan(o,reason);
  MovementBlocked movement_blocked;
  if(hook){try{auto selected=hook(o,intent,PlanContext{world_,fresh,battle_seen_,reversed_});
      if(!config_.wait_only){intent=selected.intent;reason=std::move(selected.reason);}
      if(selected.movement_reversed)reversed_=*selected.movement_reversed;
      movement_blocked=std::move(selected.movement_blocked);
    }catch(...){intent={};reason="planner_exception_wait";}
  }
  intent=guard(intent,o,movement_blocked,diagnostics.final_guard_reason);
  Action a{o.tick,o.session_id,intent};cached_=a;submissions_=1;
  std::optional<Dir> attempted;
  if(intent.type==ActionType::Move&&intent.direction)attempted=reversed_?opposite(*intent.direction):*intent.direction;
  sent_.push_back({o.tick,intent,o.position,o.state_complete&&o.alive.value_or(false),attempted,!config_.require_publication_confirmation});
  if(sent_.size()>128)sent_.pop_front();
  previous_tick_=o.tick;previous_received_=now;
  return Decision{a,reason,false,fresh,receipt,std::move(diagnostics)};
}
void BotCore::note_published(Tick tick){
  auto it=std::find_if(sent_.rbegin(),sent_.rend(),[&](const auto& sent){return sent.tick==tick;});
  if(it!=sent_.rend())it->published=true;
}
ActionIntent BotCore::plan(const Observation& o,std::string& reason){
  if(!o.state_complete||!o.alive.value_or(false)){reason="dead_or_incomplete";return {};}
  auto map=o.map;int count=map.width*map.height,start=map.index(o.position);
  if(world_.resources().count(start)){reason="resource_underfoot";return {};}
  const double inf=std::numeric_limits<double>::infinity();
  auto& dist=distance_buffer_;auto& steps=step_buffer_;auto& first=first_buffer_;auto& occupied=occupied_buffer_;
  dist.assign(count,inf);steps.assign(count,0);first.assign(count,-1);occupied.assign(count,0);
  for(const auto& e:o.entities)if(e.type==EntityType::Robot)occupied[map.index(e.position)]=true;
  using Item=std::pair<double,int>;
  auto& heap=heap_buffer_;heap.clear();heap.reserve(static_cast<std::size_t>(count)*2);
  auto push=[&](Item item){heap.push_back(item);std::push_heap(heap.begin(),heap.end(),std::greater<Item>());};
  dist[start]=0;push({0,start});
  while(!heap.empty()){
    std::pop_heap(heap.begin(),heap.end(),std::greater<Item>());auto [cost,i]=heap.back();heap.pop_back();if(cost>dist[i]+1e-9)continue;
    for(auto d:kSearchOrder){
      Cell p=step(map.cell(i),d);if(!map.contains(p)||world_.blocked(p,o.tick))continue;
      int j=map.index(p);if(occupied[j])continue;
      double next=cost+(world_.terrain(p)==Terrain::Unknown?1.2:1.0);
      if(next+1e-9<dist[j]){
        dist[j]=next;steps[j]=steps[i]+1;first[j]=i==start?static_cast<int>(d):first[i];push({next,j});
      }
    }
  }
  Tick horizon=config_.horizon.value_or(battle_seen_?500:100);
  auto reachable=[&](int i){return i!=start&&std::isfinite(dist[i])&&(o.tick>=horizon||steps[i]<=horizon-o.tick);};
  std::optional<std::vector<int>> unknown_counts;
  auto gain_at=[&](Cell c){
    if(!unknown_counts)unknown_counts=unknown_footprint_counts(world_,o.vision_radius.value_or(0));
    return (*unknown_counts)[map.index(c)];
  };
  auto& candidates=candidate_buffer_;candidates.clear();
  for(const auto& entry:world_.resources())if(reachable(entry.first))candidates.push_back(entry.first);
  std::sort(candidates.begin(),candidates.end(),[&](int a,int b){
    double va=world_.resources().at(a).value/(dist[a]+1),vb=world_.resources().at(b).value/(dist[b]+1);
    return va!=vb?va>vb:a<b;
  });
  if(candidates.size()>16)candidates.resize(16); // Bounded work even on malformed/extended practice maps.
  int target=-1;double best=-inf;
  for(int i:candidates){
    double value=world_.resources().at(i).value/(dist[i]+1.0);
    if(config_.policy==CollectorPolicy::Frontier)
      value+=0.5*gain_at(map.cell(i))/(dist[i]+1.0);
    else if(config_.policy==CollectorPolicy::Lookahead&&candidates.size()>1){
      std::vector<double> onward(count,inf);std::vector<int> lengths(count,0);
      std::priority_queue<Item,std::vector<Item>,std::greater<Item>> second;
      onward[i]=0;second.push({0,i});
      while(!second.empty()){
        auto [cost,index]=second.top();second.pop();if(cost>onward[index]+1e-9)continue;
        for(auto direction:kSearchOrder){
          Cell p=step(map.cell(index),direction);if(!map.contains(p)||world_.blocked(p,o.tick))continue;
          int next=map.index(p);if(occupied[next])continue;
          double next_cost=cost+(world_.terrain(p)==Terrain::Unknown?1.2:1.0);
          if(next_cost+1e-9<onward[next]){
            onward[next]=next_cost;lengths[next]=lengths[index]+1;second.push({next_cost,next});
          }
        }
      }
      double followup=0;
      for(int j:candidates)if(j!=i&&std::isfinite(onward[j])&&
          (o.tick>=horizon||steps[i]+lengths[j]<=horizon-o.tick))
        followup=std::max(followup,world_.resources().at(j).value/(dist[i]+onward[j]+1));
      value+=0.35*followup;
    }
    if(value>best+1e-9||(std::abs(value-best)<1e-9&&(target<0||i<target))){target=i;best=value;}
  }
  if(target>=0){reason="resource";goal_=map.cell(target);}
  else {
    reason="explore";int radius=o.vision_radius.value_or(0);
    auto exploration=[&](int i){return gain_at(map.cell(i))/(dist[i]+1.0)-0.25*world_.visits(map.cell(i));};
    for(int i=0;i<count;++i)if(reachable(i)){
      double value=exploration(i);
      if(value>0&&value>best+1e-9){best=value;target=i;}
    }
    if(goal_&&map.contains(*goal_)){
      int i=map.index(*goal_);
      if(reachable(i)){
        double old=exploration(i);
        if(old>0&&best<=old+0.2*std::max(std::abs(old),0.1)){target=i;best=old;}
      }
    }
    if(target>=0)goal_=map.cell(target);else goal_.reset();
  }
  if(target<0||first[target]<0){reason="no_reachable_target";return {};}
  Dir actual=static_cast<Dir>(first[target]);
  return {ActionType::Move,reversed_?opposite(actual):actual};
}
} // namespace rmtc
