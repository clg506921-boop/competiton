#include "rmtc_bot/protocol.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace rmtc {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kPerceptionLimit = 2*1024*1024;
const Json* member(const Json& j,const char* key) {
  if (!j.is_object()) return nullptr;
  auto it=j.find(key); return it==j.end()?nullptr:&*it;
}
bool integer(const Json& j,Tick& n) {
  if (j.is_number_unsigned()) {
    auto u=j.get<std::uint64_t>();
    if(u>static_cast<std::uint64_t>(std::numeric_limits<Tick>::max())) return false;
    n=static_cast<Tick>(u); return true;
  }
  if (!j.is_number_integer()) return false;
  n=j.get<Tick>(); return true;
}
std::optional<double> number(const Json* j) {
  if (!j || !j->is_number()) return {};
  double d=j->get<double>(); return std::isfinite(d)&&d>=0?std::optional<double>(d):std::nullopt;
}
std::optional<bool> boolean(const Json* j) {
  return j&&j->is_boolean()?std::optional<bool>(j->get<bool>()):std::nullopt;
}
std::optional<std::string> string(const Json* j) {
  return j&&j->is_string()?std::optional<std::string>(j->get<std::string>()):std::nullopt;
}
std::optional<Cell> coordinate(const Json* j, MapSize map) {
  if (!j || !j->is_array() || j->size()!=2) return {};
  Tick x{},y{};
  if(!integer((*j)[0],x)||!integer((*j)[1],y)||x<0||y<0||x>=map.width||y>=map.height)return {};
  return Cell{static_cast<int>(x),static_cast<int>(y)};
}
std::optional<Dir> direction(const Json* j) {
  auto s=string(j); if(!s)return {};
  for(auto d:kSearchOrder) if(*s==direction_name(d))return d;
  return {};
}
std::optional<ActionType> action_type(const Json* j) {
  auto s=string(j); if(!s)return {};
  for(auto a:{ActionType::Move,ActionType::Attack,ActionType::Defend,ActionType::Wait})
    if(*s==action_name(a))return a;
  return {};
}
} // namespace

bool valid_seat(std::string_view s) { return s.size()==1 && s[0]>='A' && s[0]<='D'; }
bool valid_team_name(std::string_view s) {
  return !s.empty()&&s.size()<=96&&s.find_first_of("\r\n") == std::string_view::npos;
}
const char* direction_name(Dir d) {
  switch(d){case Dir::Up:return "up";case Dir::Right:return "right";case Dir::Down:return "down";case Dir::Left:return "left";}
  return "";
}
const char* action_name(ActionType a) {
  switch(a){case ActionType::Move:return "move";case ActionType::Attack:return "attack";case ActionType::Defend:return "defend";case ActionType::Wait:return "wait";}
  return "";
}
Result<Cell> parse_relative_position(std::string_view s) {
  if(s.size()!=89)return Error{ErrorCode::BadRelmap,"position.length"};
  int twos=0;Cell found{};
  for(int row=0;row<9;++row) {
    for(int col=0;col<9;++col) {
      auto c=s[static_cast<std::size_t>(row*10+col)];
      if(row==4&&col==4){if(c!='1')return Error{ErrorCode::BadRelmap,"position.center"};}
      else if(c=='2'){++twos;found={col-4,row-4};}
      else if(c!='0')return Error{ErrorCode::BadRelmap,"position.character"};
    }
    if(row<8&&s[static_cast<std::size_t>(row*10+9)]!='\n')return Error{ErrorCode::BadRelmap,"position.newline"};
  }
  if(twos>1)return Error{ErrorCode::BadRelmap,"position.multiple_entities"};
  return found;
}
Result<Observation> parse_observation(std::string_view text,std::string_view seat) noexcept {
  try {
    if(text.size()>kPerceptionLimit)return Error{ErrorCode::TooLarge,"perception"};
    bool duplicate=false,deep=false;
    std::vector<std::unordered_set<std::string>> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json& value) {
      if(depth>64){deep=true;return false;}
      if(event==Json::parse_event_t::object_start)keys.emplace_back();
      else if(event==Json::parse_event_t::key&&!keys.empty()){
        if(!keys.back().insert(value.get<std::string>()).second)duplicate=true;
      }else if(event==Json::parse_event_t::object_end&&!keys.empty())keys.pop_back();
      return true;
    };
    Json j=Json::parse(text.begin(),text.end(),callback,false);
    if(duplicate)return Error{ErrorCode::DuplicateKey,"perception"};
    if(deep||j.is_discarded()||!j.is_object())return Error{ErrorCode::Parse,"perception"};
    Observation o;
    const auto* tick=member(j,"tick");
    if(!tick||!integer(*tick,o.tick)||o.tick<0)return Error{ErrorCode::Type,"tick"};
    auto session=string(member(j,"session_id"));
    if(!session||session->empty()||session->size()>512)return Error{ErrorCode::Type,"session_id"};
    o.session_id=*session;
    const auto* self=member(j,"self");
    if(!self||!self->is_object())return Error{ErrorCode::MissingField,"self"};
    auto team=string(member(*self,"team"));
    if(!valid_seat(seat)||!team||*team!=seat)return Error{ErrorCode::WrongSeat,"self.team"};
    o.seat=*team;
    auto robot_id=string(member(*self,"robot_id"));
    if(robot_id&&*robot_id!=seat)return Error{ErrorCode::WrongSeat,"self.robot_id"};
    const auto* map=member(j,"map");
    const auto* w=map?member(*map,"width"):nullptr;
    const auto* h=map?member(*map,"height"):nullptr;
    Tick width{},height{};
    if(!w||!h||!integer(*w,width)||!integer(*h,height)||width<1||height<1||width>256||height>256)
      return Error{ErrorCode::Range,"map"};
    o.map={static_cast<int>(width),static_cast<int>(height)};
    auto pos=coordinate(member(*self,"position"),o.map);
    if(!pos)return Error{ErrorCode::Range,"self.position"};
    o.position=*pos;
    o.alive=boolean(member(*self,"alive"));o.hp=number(member(*self,"hp"));
    o.protected_now=boolean(member(*self,"protected"));
    o.defending=boolean(member(*self,"defending"));
    if(auto v=member(*self,"respawn_in")){Tick n{};if(integer(*v,n)&&n>=0)o.respawn_in=n;}
    o.position_reason=string(member(*self,"position_reason"));
    o.state_complete=o.alive.has_value()&&o.hp.has_value();
    if(const auto* v=member(j,"vision_radius")) {
      Tick radius{};if(integer(*v,radius)&&radius>=0&&radius<=4)o.vision_radius=static_cast<int>(radius);
    }
    const auto* rec=member(*self,"last_action");
    if(rec&&rec->is_object()) {
      Receipt r;auto t=member(*rec,"tick");auto a=action_type(member(*rec,"action"));auto m=boolean(member(*rec,"moved"));
      if(t&&integer(*t,r.tick)&&r.tick>=0&&r.tick<o.tick&&a&&m){
        r.action=*a;r.moved=*m;r.effective_direction=direction(member(*rec,"effective_direction"));r.error=string(member(*rec,"error"));
        if(!r.moved||(r.action==ActionType::Move&&r.effective_direction))o.receipt=r;
      }
    }
    const auto* entities=member(j,"visible_entities");
    o.entities_complete=entities&&entities->is_array()&&entities->size()<=4096&&o.vision_radius.has_value();
    // Missing/invalid radius gives no trustworthy envelope for any entity.
    // Preserve self/score and existing memory, but never invent radius four.
    if(o.vision_radius&&entities&&entities->is_array()&&entities->size()<=4096)for(const auto& e:*entities){
      auto type=string(member(e,"type"));auto rp=string(member(e,"position"));
      if(!type||!rp){o.entities_complete=false;continue;}
      auto rel=parse_relative_position(*rp);
      if(!std::holds_alternative<Cell>(rel)){o.entities_complete=false;continue;}
      auto d=std::get<Cell>(rel);Entity out;out.position={o.position.x+d.x,o.position.y+d.y};
      int radius=*o.vision_radius;
      if(!o.map.contains(out.position)||std::abs(d.x)>radius||std::abs(d.y)>radius){o.entities_complete=false;continue;}
      if(*type=="obstacle")out.type=EntityType::Obstacle;
      else if(*type=="resource_normal"||*type=="resource_high"){
        auto val=number(member(e,"value"));auto id=string(member(e,"id"));
        if(!val||!id||id->empty()){o.entities_complete=false;continue;}
        out.type=EntityType::Resource;out.value=*val;out.id=*id;
      }else if(*type=="robot"){
        auto id=string(member(e,"robot_id"));
        if(!id||!valid_seat(*id)||*id==seat){o.entities_complete=false;continue;}
        out.type=EntityType::Robot;out.robot_id=*id;out.hp=number(member(e,"hp"));
        out.protected_now=boolean(member(e,"protected"));out.defending=boolean(member(e,"defending"));
      }else {o.entities_complete=false;continue;}
      o.entities.push_back(std::move(out));
    }
    if(auto score=member(j,"score")){
      o.resource_value=number(member(*score,"resource_value"));o.effective_damage=number(member(*score,"effective_damage"));
      if(auto v=member(*score,"mission_completed")){Tick n{};if(integer(*v,n)&&n>=0)o.mission_completed=n;}
      if(auto v=member(*score,"invalid_actions")){Tick n{};if(integer(*v,n)&&n>=0)o.invalid_actions=n;}
      if(auto ids=member(*score,"mission_completed_ids");ids&&ids->is_array())
        for(const auto& id:*ids)if(id.is_string())o.completed_missions.push_back(id.get<std::string>());
    }
    if(auto missions=member(j,"missions");missions&&missions->is_array()&&missions->size()<=64)
      for(const auto& m:*missions){
        auto id=string(member(m,"id"));auto type=string(member(m,"type"));if(!id||!type)continue;
        o.missions.push_back({*id,*type,number(member(m,"target")),coordinate(member(m,"position"),o.map)});
      }
    o.event_text=string(member(j,"event_text")).value_or("");
    o.mission_text=string(member(j,"mission_text")).value_or("");
    return o;
  } catch(...) { return Error{ErrorCode::Parse,"perception.exception"}; }
}
std::string encode_action(const Action& a) {
  Json j={{"tick",a.tick},{"session_id",a.session_id},{"action",action_name(a.intent.type)}};
  if(a.intent.direction&&(a.intent.type==ActionType::Move||a.intent.type==ActionType::Attack))j["direction"]=direction_name(*a.intent.direction);
  return j.dump();
}
} // namespace rmtc
