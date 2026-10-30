#include "rmtc_bot/pursuit.hpp"
#include <algorithm>
#include <cmath>
namespace rmtc {
void PublicPursuit::reset(){session_.clear();map_={};self_={};tick_=-1;tracks_.clear();}
void PublicPursuit::observe(const Observation& o){
  if(!o.state_complete||!o.entities_complete||!o.alive.value_or(false)||!o.vision_radius||
      *o.vision_radius<1||*o.vision_radius>4||!o.map.contains(o.position)||o.tick<0){reset();return;}
  const bool consecutive=session_==o.session_id&&map_.width==o.map.width&&map_.height==o.map.height&&
      tick_>=0&&o.tick==tick_after(tick_,1)&&
      std::abs(self_.x-o.position.x)+std::abs(self_.y-o.position.y)<=1;
  std::vector<Track> next;
  for(const auto& e:o.entities){
    if(e.type!=EntityType::Robot||e.robot_id.empty()||e.robot_id==o.seat||!o.map.contains(e.position)||
        std::abs(e.position.x-o.position.x)>*o.vision_radius||std::abs(e.position.y-o.position.y)>*o.vision_radius)continue;
    if(next.size()==3||std::any_of(next.begin(),next.end(),[&](const auto& t){return t.seat==e.robot_id;})){reset();return;}
    Track current{e.robot_id,e.position,{},e.hp.value_or(0),0,
      e.hp&&std::isfinite(*e.hp)&&*e.hp>0&&e.protected_now==false};
    const auto previous=std::find_if(tracks_.begin(),tracks_.end(),[&](const auto& t){return t.seat==current.seat;});
    if(consecutive&&current.eligible&&previous!=tracks_.end()&&previous->eligible&&current.hp<=previous->hp){
      const Cell velocity{e.position.x-previous->position.x,e.position.y-previous->position.y};
      if(std::abs(velocity.x)+std::abs(velocity.y)==1){
        current.velocity=velocity;current.streak=previous->velocity==velocity?std::min(2,previous->streak+1):1;
      }
    }
    next.push_back(std::move(current));
  }
  session_=o.session_id;map_=o.map;self_=o.position;tick_=o.tick;tracks_=std::move(next);
}
bool PublicPursuit::moving(const std::string& seat)const{
  return std::any_of(tracks_.begin(),tracks_.end(),[&](const auto& t){return t.seat==seat&&t.eligible&&t.streak>=2;});
}
bool PublicPursuit::same_speed_tail(const std::string& seat,Cell velocity)const{
  if(std::abs(velocity.x)+std::abs(velocity.y)!=1)return false;
  const auto t=std::find_if(tracks_.begin(),tracks_.end(),[&](const auto& car){return car.seat==seat;});
  if(t==tracks_.end()||!moving(seat)||t->velocity!=velocity)return false;
  const Cell relative{t->position.x-self_.x,t->position.y-self_.y};
  return std::abs(relative.x)+std::abs(relative.y)>2&&relative.x*velocity.x+relative.y*velocity.y>0;
}
std::optional<Cell> PublicPursuit::forecast(const std::string& seat,int steps,
    const std::function<bool(Cell,Tick)>& known_safe)const{
  const auto it=std::find_if(tracks_.begin(),tracks_.end(),[&](const auto& t){return t.seat==seat;});
  if(it==tracks_.end()||!moving(seat)||steps<1||steps>2)return {};
  Cell p=it->position;
  for(int k=1;k<=steps;++k){
    p={p.x+it->velocity.x,p.y+it->velocity.y};const Tick at=tick_after(tick_,k);
    // Stop the entire prediction at unknown terrain, event walls, danger or a
    // presently occupied cell. No guessed turns or extrapolation through robots.
    if(!map_.contains(p)||p==self_||at<=tick_||!known_safe(p,at)||
        std::any_of(tracks_.begin(),tracks_.end(),[&](const auto& other){return other.seat!=seat&&other.position==p;}))return {};
  }
  return p;
}
bool PublicPursuit::crossfire(const std::string& seat,Cell firing,Cell predicted)const{
  const auto target=std::find_if(tracks_.begin(),tracks_.end(),[&](const auto& t){return t.seat==seat;});
  if(target==tracks_.end()||!moving(seat)||firing==predicted)return false;
  const bool own_horizontal=firing.y==predicted.y;
  if(!own_horizontal&&firing.x!=predicted.x)return false;
  const int own_distance=std::abs(firing.x-predicted.x)+std::abs(firing.y-predicted.y);
  if(own_distance>2)return false;
  for(const auto& chaser:tracks_){
    if(chaser.seat==seat||!moving(chaser.seat)||chaser.velocity!=target->velocity)continue;
    const Cell separation{target->position.x-chaser.position.x,target->position.y-chaser.position.y};
    const int distance=std::abs(separation.x)+std::abs(separation.y);
    // Co-motion behind the target is only possible pursuit evidence. The other
    // robot remains hostile in the independent worst-case incoming-damage model.
    if(distance<1||distance>4||separation.x*chaser.velocity.x+separation.y*chaser.velocity.y<=0)continue;
    const bool horizontal=separation.y==0;
    if((horizontal||separation.x==0)&&own_horizontal!=horizontal)return true;
  }
  return false;
}
} // namespace rmtc
