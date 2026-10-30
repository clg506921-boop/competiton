#include "rmtc_bot/battle.hpp"
#include "rmtc_bot/rules.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rmtc {
AttackEstimate estimate_attack(const Combatant& self,Dir d,const std::vector<Combatant>& enemies,
    const CombatParameters& p,bool unrivaled,bool reflection){
  AttackEstimate out;if(!std::isfinite(self.hp)||self.hp<=0||self.protected_now)return out;
  std::vector<const Combatant*> targets;
  if(unrivaled){
    if(p.unrivaled_size<1||p.unrivaled_size%2==0)return out;
    const int radius=(p.unrivaled_size-1)/2;
    for(const auto& enemy:enemies)if(enemy.hp>0&&std::abs(enemy.position.x-self.position.x)<=radius&&
        std::abs(enemy.position.y-self.position.y)<=radius)targets.push_back(&enemy);
  }else{
    auto v=delta(d);const Combatant* nearest=nullptr;int nearest_distance=3;
    for(const auto& enemy:enemies){
      if(enemy.hp<=0)continue;
      int dx=enemy.position.x-self.position.x,dy=enemy.position.y-self.position.y;
      int distance=dx*v.x+dy*v.y;
      if(distance<1||distance>2||dx!=distance*v.x||dy!=distance*v.y)continue;
      if(distance<nearest_distance){nearest=&enemy;nearest_distance=distance;}
    }
    if(nearest)targets.push_back(nearest);
  }
  for(const auto* enemy:targets){
    if(enemy->protected_now){out.targets.push_back({enemy->seat,0});continue;}
    if(reflection&&enemy->defending_this_tick){
      out.reflected_damage+=p.reflection_damage;out.targets.push_back({enemy->seat,0});continue;
    }
    double damage=unrivaled?(enemy->defending_this_tick?p.unrivaled_defended_damage:p.unrivaled_damage):
      (enemy->defending_this_tick?p.normal_defended_damage:p.normal_damage);
    if(enemy->vulnerable)damage*=enemy->vulnerability_multiplier.value_or(p.vulnerability_multiplier);
    out.targets.push_back({enemy->seat,std::max(0.0,damage)});
  }
  return out;
}
double apportioned_effective_damage(double hp,double own,double total){
  if(!std::isfinite(hp)||!std::isfinite(own)||!std::isfinite(total)||hp<=0||own<=0||total<=0||own>total)return 0;
  return std::min(hp,total)*(own/total);
}
double resource_points(double r){return std::isfinite(r)?rules::resource_points_cap*std::clamp(r/rules::resource_value_cap,0.0,1.0):0;}
double combat_points(double d){return std::isfinite(d)?rules::combat_points_cap*std::clamp(d/rules::combat_damage_cap,0.0,1.0):0;}
double candidate_utility(const TacticalCandidate& c,int h){
  if(h<1||h>5||!std::isfinite(c.score_points)||!std::isfinite(c.current_death_loss_points)||
      !std::isfinite(c.next_position_risk_points)||c.current_death_loss_points<0||c.next_position_risk_points<0||
      !std::isfinite(c.completed_mission_gain)||c.completed_mission_gain<0||
      !std::isfinite(c.resource_value_gain)||c.resource_value_gain<0||
      !std::isfinite(c.effective_damage_gain)||c.effective_damage_gain<0||!std::isfinite(c.net_effective_damage_gain)||!std::isfinite(c.navigation_progress)||!std::isfinite(c.surviving_hp)||c.surviving_hp<0)
    return -std::numeric_limits<double>::infinity();
  return (c.score_points-c.current_death_loss_points-c.next_position_risk_points)/h;
}
std::optional<std::size_t> choose_tactical_candidate(const std::vector<TacticalCandidate>& choices,DecisionType mode,int h){
  std::optional<std::size_t> nonattack,attack;
  double best_nonattack=-std::numeric_limits<double>::infinity(),best_attack=best_nonattack;
  auto secondary_better=[&](std::size_t a,std::size_t b){
    const auto& x=choices[a];const auto& y=choices[b];
    if(x.completed_mission_gain!=y.completed_mission_gain)return x.completed_mission_gain>y.completed_mission_gain;
    if(x.resource_value_gain!=y.resource_value_gain)return x.resource_value_gain>y.resource_value_gain;
    if(x.net_effective_damage_gain!=y.net_effective_damage_gain)return x.net_effective_damage_gain>y.net_effective_damage_gain;
    if(x.surviving_hp!=y.surviving_hp)return x.surviving_hp>y.surviving_hp;
    if(x.effective_damage_gain!=y.effective_damage_gain)return x.effective_damage_gain>y.effective_damage_gain;
    return x.navigation_progress>y.navigation_progress;
  };
  for(std::size_t i=0;i<choices.size();++i){
    const auto& c=choices[i];double u=candidate_utility(c,h);if(!std::isfinite(u))continue;
    if(c.intent.type==ActionType::Attack){
      if(mode==DecisionType::Collect&&(!(c.reliable_kill||c.no_counter_under_confirmed_model)||u<=0))continue;
      if(u>best_attack||(attack&&u==best_attack&&secondary_better(i,*attack))){best_attack=u;attack=i;}
    }else if(u>best_nonattack||(nonattack&&u==best_nonattack&&secondary_better(i,*nonattack))){best_nonattack=u;nonattack=i;}
  }
  if(attack&&(best_attack>best_nonattack||(best_attack==best_nonattack&&
      (!nonattack||!secondary_better(*nonattack,*attack)))))return attack;
  return nonattack;
}
std::optional<Mission> nearest_unfinished_reach(const Observation& o,
    const std::function<std::optional<int>(Cell)>& path,Tick horizon){
  std::optional<Mission> best;int best_distance=std::numeric_limits<int>::max();
  for(const auto& mission:o.missions){
    if(mission.type!="reach"||!mission.position||
        std::find(o.completed_missions.begin(),o.completed_missions.end(),mission.id)!=o.completed_missions.end())continue;
    auto distance=path(*mission.position);
    if(!distance||*distance<0||(o.tick<horizon&&*distance>horizon-o.tick))continue;
    if(*distance<best_distance){best=mission;best_distance=*distance;}
  }
  return best;
}
} // namespace rmtc
