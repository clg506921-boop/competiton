#include "rmtc_bot/event_memory.hpp"
#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rmtc {
namespace {
using Json=nlohmann::json;
bool contains(const Json& area,Cell p){
  if(area.is_null())return false;
  const auto kind=area["kind"].get<std::string>();
  if(kind=="global")return true;
  if(kind=="points"){for(const auto& c:area["at"])if(c[0]==p.x&&c[1]==p.y)return true;return false;}
  if(kind=="cross")return area["at"][0]==p.x||area["at"][1]==p.y;
  if(kind=="square"){
    int r=(area["size"].get<int>()-1)/2;
    return std::abs(p.x-area["center"][0].get<int>())<=r&&std::abs(p.y-area["center"][1].get<int>())<=r;
  }
  return kind=="rect"&&p.x>=area["min"][0].get<int>()&&p.y>=area["min"][1].get<int>()&&
    p.x<=area["max"][0].get<int>()&&p.y<=area["max"][1].get<int>();
}
std::optional<Tick> when(const Json& j,const char* key){return j.contains(key)&&!j[key].is_null()?std::optional<Tick>(j[key].get<Tick>()):std::nullopt;}
}
void EventMemory::reset(std::string session){
  session_=std::move(session);seat_.clear();records_.clear();effects_.clear();source_to_event_.clear();history_.clear();sequence_=0;
  own_spawn_.reset();
}
bool EventMemory::split_contains(const Json& area,Cell p)const{
  // Only our explicit initial public position is known. Never infer other seats'
  // spawns from map symmetry, a late join, or a later respawn position.
  return (!own_spawn_||p!=*own_spawn_)&&contains(area,p);
}
bool EventMemory::grant(std::string type,std::string event,std::optional<Tick> at,Tick from,Tick until){
  return grant_for(seat_,std::move(type),std::move(event),at,from,until);
}
bool EventMemory::grant_for(const std::string& seat,std::string type,std::string event,std::optional<Tick> at,Tick from,Tick until){
  if(until<=from)return false;
  if(seat!="A"&&seat!="B"&&seat!="C"&&seat!="D")return false;
  const auto key=seat+":"+type;
  auto old=effects_.find(key);
  if(old!=effects_.end()){
    if(old->second.granted_tick&&(!at||*at<*old->second.granted_tick))return false;
    if(!at&&old->second.until_exclusive!=until)return false;
  }
  effects_[key]={type,std::move(event),at,from,until};return true;
}
bool EventMemory::public_active(const std::string& id,Tick t)const{
  auto it=records_.find(id);if(it==records_.end())return false;
  const auto& r=it->second;auto start=when(r.data,"start_tick");
  if(!start||t<*start||(r.closed_observed&&t>=*r.closed_observed))return false;
  auto end=when(r.data,"end_tick");
  auto type=r.data["type"].get<std::string>();
  if(type=="unknown")return false;
  if(type=="explosion"||type=="illusion")return t==*start;
  return end&&t<*end;
}
MergeOutcome EventMemory::merge_validated(Json event,Tick observed){
  if(!event.is_object()||!event.contains("type")||!event.contains("phase"))return {false,{},"invalid_event"};
  const auto type=event["type"].get<std::string>(),phase=event["phase"].get<std::string>();
  if(phase=="report"||type=="unknown")return {false,{},"informational_only"};
  std::string id;
  if(!event["related_event_id"].is_null()){
    id=event["related_event_id"].get<std::string>();
    if(!records_.count(id)||records_.at(id).data["type"]!=type)return {false,id,"unresolved_link"};
  }
  if(phase=="forecast"&&!id.empty()){
    // A validated link names an existing event, not a second hazard instance.
    const auto& known=records_.at(id).data;
    for(const auto* key:{"start_tick","end_tick","trigger_end_tick","effect_end_tick","area","params","trigger"})
      if(!event[key].is_null()&&event[key]!=known[key])return {false,id,"contradictory_forecast"};
    return {false,id,"duplicate_linked_forecast"};
  }
  if(phase=="forecast"||(phase=="activated"&&id.empty())){
    if(event["start_tick"].is_null()||event["area"].is_null()||!event["params"].is_object())
      return {false,{},"incomplete_independent_event"};
    auto source=event["source_ids"][0].get<std::string>()+":"+type+":"+
        Json::array({event["start_tick"],event["area"],event["params"]}).dump();
    auto existing=source_to_event_.find(source);if(existing!=source_to_event_.end())return {false,existing->second,"duplicate_source"};
    if(records_.size()>=128)return {false,{},"memory_full"};
    id=type+"-"+std::to_string(++sequence_);source_to_event_[source]=id;records_[id]=Record{event,std::nullopt,-1,-1,0};
    auto& r=records_.at(id);
    for(std::size_t i=0;i<history_.size();++i)derive(id,r,history_[i],i?&history_[i-1]:nullptr);
    return {true,id,{}};
  }
  if(id.empty())return {false,{},"unresolved_link"};
  auto& r=records_.at(id);
  auto known_start=when(r.data,"start_tick");
  if(known_start&&observed<*known_start)return {false,id,"notice_before_activation"};
  if(phase=="activated"){
    for(const auto* key:{"start_tick","end_tick","trigger_end_tick","effect_end_tick","area","params","trigger"})
      if(!event[key].is_null()&&event[key]!=r.data[key])return {false,id,"contradictory_activation"};
    r.data["phase"]="activated";return {true,id,{}};
  }
  auto at=when(event,"at_tick");
  auto start=when(r.data,"start_tick");
  if((at&&(*at>observed||(start&&*at<*start)))||
      ((phase=="activated"||phase=="ended")&&start&&observed<*start))
    return {false,id,"notice_time_contradiction"};
  if(phase=="ended"){
    if(event["subject_ids"].is_null()){
      r.closed_observed=r.closed_observed?std::min(*r.closed_observed,observed):observed;
      // Knowledge time, not a fabricated occurrence tick. Closure stays monotone.
      if(type=="unrivaled"){
        for(auto e=effects_.begin();e!=effects_.end();){
          if(e->second.type==type&&e->second.event_id==id)e=effects_.erase(e);else ++e;
        }
      }
      return {true,id,{}};
    }
    bool removed=false;
    for(const auto& subject:event["subject_ids"]){
      auto e=effects_.find(subject.get<std::string>()+":"+type);
      if(e!=effects_.end()&&e->second.event_id==id&&at&&e->second.granted_tick&&*at>=*e->second.granted_tick){effects_.erase(e);removed=true;}
    }
    return {removed,id,removed?"":"undated_or_older_expiry_not_applied"};
  }
  if(phase=="granted"){
    auto end=when(event,"effect_end_tick");if(!end)end=when(r.data,"effect_end_tick");
    if(!end)return {false,id,"unknown_personal_expiry"};
    Tick from=at.value_or(observed);
    if(at&&(type=="unrivaled"||type=="vulnerability")){
      if(from==std::numeric_limits<Tick>::max())return {false,id,"time_overflow"};
      ++from;
    }
    bool ok=false;
    for(const auto& subject:event["subject_ids"])
      ok=grant_for(subject.get<std::string>(),type,id,at,from,*end)||ok;
    return {ok,id,ok?"":"ambiguous_or_stale_grant"};
  }
  return {false,id,"unsupported_phase"};
}
void EventMemory::derive(const std::string& id,Record& r,const Observation& o,const Observation* previous){
  if(o.tick<=r.derived_through)return;
  r.derived_through=o.tick;
  const auto& e=r.data;auto start=when(e,"start_tick");if(!start)return;
  auto type=e["type"].get<std::string>();
  if(type=="illusion"&&o.tick==*start&&o.alive.value_or(false)&&contains(e["area"],o.position)){
    auto end=when(e,"effect_end_tick");if(end)grant(type,id,*start,*start,*end);
  }
  if(!o.receipt||o.receipt->tick+1!=o.tick||!previous||previous->tick!=o.receipt->tick)return;
  Tick action=o.receipt->tick;
  if(!public_active(id,action)){r.dwell=0;return;}
  bool moved=o.receipt->action==ActionType::Move&&o.receipt->moved;
  if(type=="angel"&&moved&&contains(e["area"],o.position)&&o.position_reason!=std::optional<std::string>("respawn")){
    auto trigger_end=when(e,"trigger_end_tick");
    if(trigger_end&&action<*trigger_end){auto interval=healing_interval(action,e["params"]["heal_ticks"].get<Tick>());
      if(interval)grant(type,id,action,interval->from,interval->until_exclusive);}
  }else if(type=="vulnerability"&&moved){
    auto end=when(e,"effect_end_tick"),trigger_end=when(e,"trigger_end_tick");
    if(end&&trigger_end){auto interval=vulnerability_interval(action,*start,*trigger_end,*end);
      if(interval)grant(type,id,action,interval->from,interval->until_exclusive);}
  }else if(type=="unrivaled"){
    if(!o.alive.value_or(false)||!contains(e["area"],o.position)){r.dwell=0;r.last_dwell_tick=-1;return;}
    if(o.position_reason==std::optional<std::string>("respawn")){r.dwell=0;r.last_dwell_tick=-1;}
    r.dwell=r.last_dwell_tick==action-1?r.dwell+1:1;r.last_dwell_tick=action;
    if(r.dwell==e["params"]["required_dwell_ticks"].get<int>()){
      auto end=when(e,"end_tick");if(end)grant(type,id,action,o.tick,*end);
    }
  }
}
void EventMemory::observe(const Observation& o){
  if(o.session_id!=session_)return;
  if(!history_.empty()&&o.tick<=history_.back().tick)return;
  seat_=o.seat;
  if(o.tick==0&&o.state_complete&&o.alive.value_or(false)&&o.map.contains(o.position)&&
      o.position_reason==std::optional<std::string>("initial"))own_spawn_=o.position;
  const Observation* previous=history_.empty()?nullptr:&history_.back();
  for(auto& entry:records_)derive(entry.first,entry.second,o,previous);
  Observation snapshot;snapshot.tick=o.tick;snapshot.session_id=o.session_id;snapshot.seat=o.seat;
  snapshot.position=o.position;snapshot.alive=o.alive;snapshot.receipt=o.receipt;snapshot.position_reason=o.position_reason;
  history_.push_back(std::move(snapshot));if(history_.size()>128)history_.pop_front();
}
std::optional<PersonalEffect> EventMemory::self_effect(const std::string& type,Tick now)const{
  return known_effect(seat_,type,now);
}
std::optional<PersonalEffect> EventMemory::known_effect(const std::string& seat,const std::string& type,Tick now)const{
  auto it=effects_.find(seat+":"+type);if(it==effects_.end()||now<it->second.known_from||now>=it->second.until_exclusive)return {};
  return it->second;
}
std::optional<Json> EventMemory::effect_parameters(const PersonalEffect& effect)const{
  auto it=records_.find(effect.event_id);if(it==records_.end()||!it->second.data["params"].is_object())return {};
  return it->second.data["params"];
}
std::vector<PublicOpportunity> EventMemory::opportunities(MapSize map,Cell current,Tick now)const{
  std::vector<PublicOpportunity> out;
  if(map.width<1||map.height<1||map.width>256||map.height>256||!map.contains(current))return out;
  for(const auto& item:records_){
    const auto& r=item.second;const auto& e=r.data;
    if(e["type"]!="angel"&&e["type"]!="unrivaled")continue;
    const auto announced_start=when(e,"start_tick");
    const bool upcoming=e["phase"]=="forecast"&&announced_start&&now<*announced_start&&!r.closed_observed;
    if(!public_active(item.first,now)&&!upcoming)continue;
    // Forecasts may guide travel; they do not grant an effect before activation.
    if(e["type"]=="unrivaled"&&self_effect("unrivaled",now))continue;
    PublicOpportunity candidate;candidate.event_id=item.first;
    candidate.start=*when(e,"start_tick");candidate.until_exclusive=*when(e,"end_tick");
    candidate.entry_until_exclusive=when(e,"trigger_end_tick").value_or(candidate.until_exclusive);
    if(e["type"]=="angel"){
      candidate.kind=OpportunityKind::Healing;
      candidate.heal_ticks=e["params"]["heal_ticks"].get<Tick>();candidate.heal_per_tick=e["params"]["heal_per_tick"].get<double>();
      for(const auto& point:e["area"]["at"]){
        Cell p{point[0].get<int>(),point[1].get<int>()};
        if(map.contains(p)&&std::find(candidate.targets.begin(),candidate.targets.end(),p)==candidate.targets.end())candidate.targets.push_back(p);
        if(candidate.targets.size()>=64)break;
      }
    }else{
      candidate.kind=OpportunityKind::Fortress;
      Tick required=e["params"]["required_dwell_ticks"].get<Tick>();
      if(required<1||required>32)continue;
      candidate.required_dwell=static_cast<int>(required);
      candidate.confirmed_dwell=r.last_dwell_tick==now-1?r.dwell:0;
      // Keep only nearby cells with bounded storage, including the current cell first.
      for(int i=0;i<map.width*map.height;++i){auto p=map.cell(i);if(!contains(e["area"],p))continue;
        candidate.targets.push_back(p);
        auto closer=[&](Cell a,Cell b){int da=std::abs(a.x-current.x)+std::abs(a.y-current.y),db=std::abs(b.x-current.x)+std::abs(b.y-current.y);
          return da!=db?da<db:map.index(a)<map.index(b);};
        std::sort(candidate.targets.begin(),candidate.targets.end(),closer);
        if(candidate.targets.size()>64)candidate.targets.pop_back();
      }
    }
    if(!candidate.targets.empty())out.push_back(std::move(candidate));
    if(out.size()>=16)break;
  }
  return out;
}
Json EventMemory::known_events(Tick now)const{
  struct Context {int priority;Tick recent;std::string id;};
  std::vector<Context> ids;
  for(const auto& entry:records_){
    bool personal=false;for(const auto& effect:effects_)if(effect.second.event_id==entry.first&&effect.second.until_exclusive>now)personal=true;
    auto start=when(entry.second.data,"start_tick");bool future=start&&now<*start;
    const auto& data=entry.second.data;
    auto effect_end=when(data,"effect_end_tick");
    bool possible_effect=effect_end&&now<*effect_end;
    if(data["type"]=="angel"){
      auto end=when(data,"end_tick");
      if(end&&data["params"].is_object()){
        Tick h=data["params"]["heal_ticks"].get<Tick>();
        possible_effect=*end<=std::numeric_limits<Tick>::max()-h&&now<*end+h;
      }
    }
    // Keep a bounded recent history for delayed ended/granted notices. This
    // affects prompt context only and never reactivates an expired hazard.
    Tick latest=start.value_or(0);
    for(const auto* key:{"end_tick","trigger_end_tick","effect_end_tick"})
      if(auto t=when(data,key))latest=std::max(latest,*t);
    if(entry.second.closed_observed)latest=*entry.second.closed_observed;
    const bool active=public_active(entry.first,now)||future||possible_effect;
    const bool recent=now>=latest&&now-latest<=128;
    if(active||personal||recent)ids.push_back({personal?3:active?2:1,latest,entry.first});
  }
  std::sort(ids.begin(),ids.end(),[](const auto& a,const auto& b){
    if(a.priority!=b.priority)return a.priority>b.priority;
    return a.recent!=b.recent?a.recent>b.recent:a.id<b.id;
  });
  Json out=Json::array();for(const auto& item:ids){if(out.size()>=16)break;
    const auto& record=records_.at(item.id);const auto& e=record.data;
    out.push_back({{"event_id",item.id},{"type",e["type"]},{"phase",e["phase"]},
      {"start_tick",e["start_tick"]},{"end_tick",e["end_tick"]},{"area",e["area"]},
      {"trigger_end_tick",e["trigger_end_tick"]},{"effect_end_tick",e["effect_end_tick"]},
      {"trigger",e["trigger"]},{"params",e["params"]},
      {"closed_observed_tick",record.closed_observed?Json(*record.closed_observed):Json(nullptr)}});
  }return out;
}
bool EventMemory::dangerous(Cell p,Tick t,bool protected_now)const{
  for(const auto& entry:records_){
    if(!public_active(entry.first,t))continue;
    const auto& e=entry.second.data;auto type=e["type"].get<std::string>();
    if(type=="world_split"&&split_contains(e["area"],p))return true;
    if(type=="explosion"&&contains(e["area"],p)&&(!protected_now||!e["params"]["respects_protection"].get<bool>()))return true;
  }return false;
}
std::optional<Json> EventMemory::active_parameters(const std::string& type,Tick t)const{
  for(const auto& entry:records_)if(entry.second.data["type"]==type&&public_active(entry.first,t))
    return entry.second.data["params"];
  return {};
}
bool EventMemory::split_wall(Cell p,Tick t)const{
  for(const auto& entry:records_)if(entry.second.data["type"]=="world_split"&&
      public_active(entry.first,t)&&split_contains(entry.second.data["area"],p))return true;
  return false;
}
PreMoveHazard EventMemory::before_move(Cell p,Tick tick,bool protection)const{
  PreMoveHazard out;
  for(const auto& entry:records_){const auto& e=entry.second.data;
    if(!public_active(entry.first,tick)||!contains(e["area"],p))continue;
    if(e["type"]=="explosion"&&(!protection||!e["params"]["respects_protection"].get<bool>()))out.damage+=e["params"]["damage"].get<double>();
    if(e["type"]=="world_split"&&split_contains(e["area"],p)&&when(e,"start_tick")==std::optional<Tick>(tick))out.forced_respawn=true;
  }
  return out;
}
bool EventMemory::wall_may_expire(Cell p,Tick first,Tick last,Tick at)const{
  for(const auto& entry:records_){const auto& e=entry.second.data;if(e["type"]!="world_split"||!split_contains(e["area"],p))continue;
    auto start=when(e,"start_tick"),end=when(e,"end_tick");if(!start||!end)continue;
    Tick until=entry.second.closed_observed?std::min(*end,*entry.second.closed_observed):*end;
    if(at>=until&&first>=*start&&last<until)return true;
  }return false;
}
void EventMemory::reconcile_walls(World& world,Tick now)const{
  auto map=world.size();
  for(const auto& entry:records_){const auto& e=entry.second.data;if(e["type"]!="world_split")continue;
    auto start=when(e,"start_tick"),end=when(e,"end_tick");if(!start||!end)continue;
    Tick until=entry.second.closed_observed?std::min(*end,*entry.second.closed_observed):*end;
    if(now<until)continue;
    for(int i=0;i<map.width*map.height;++i){auto p=map.cell(i);if(split_contains(e["area"],p))world.forget_expired_wall(p,*start,until,now);}
  }
}
double EventMemory::possible_chili_damage(Cell destination,Tick tick,bool protection,const std::vector<Entity>& visible)const{
  double damage=0;
  for(const auto& entry:records_){
    const auto& e=entry.second.data;if(e["type"]!="chili"||!public_active(entry.first,tick))continue;
    if(protection&&e["params"]["respects_protection"].get<bool>())continue;
    // Each point can be triggered at most once this settlement. Enemy movement is a
    // possibility, not a confirmed intent. Multiple different crosses may stack.
    for(const auto& point:e["area"]["at"]){Cell center{point[0].get<int>(),point[1].get<int>()};
      if(center==destination||(center.x!=destination.x&&center.y!=destination.y))continue;
      bool can_enter=false;
      for(const auto& enemy:visible)if(enemy.type==EntityType::Robot&&
          std::abs(enemy.position.x-center.x)+std::abs(enemy.position.y-center.y)==1)can_enter=true;
      if(can_enter)damage+=e["params"]["damage"].get<double>();
    }
  }
  return damage;
}
} // namespace rmtc
