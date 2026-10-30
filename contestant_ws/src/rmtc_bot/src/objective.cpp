#include "rmtc_bot/objective.hpp"
#include "rmtc_bot/battle.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rmtc {
std::optional<NavigationObjective> select_navigation_objective(const Observation& o,
    const std::unordered_map<int,Entity>& resources,int normal,int high,Tick horizon,
    const PathSteps& distance,const BetweenSteps& between){
  struct Candidate {Cell cell;int steps;double value;std::string resource_id;bool reach;};
  std::vector<Candidate> candidates;
  auto unfinished=[&](const Mission& m){return std::find(o.completed_missions.begin(),o.completed_missions.end(),m.id)==o.completed_missions.end();};
  auto add=[&](Cell cell,const Entity* resource,bool reach){
    if(!o.map.contains(cell))return;
    auto steps=distance(cell);if(!steps||*steps<0||(o.tick<horizon&&*steps>horizon-o.tick))return;
    auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.cell==cell;});
    if(found!=candidates.end()){found->reach=found->reach||reach;return;}
    if(candidates.size()>=32)return;
    candidates.push_back({cell,*steps,resource?resource->value:0,resource?resource->id:"",reach});
  };
  // Resources first so a co-located reach task is merged without losing the pickup.
  std::vector<int> indices;for(const auto& item:resources)indices.push_back(item.first);
  std::sort(indices.begin(),indices.end());
  for(int index:indices){const auto& e=resources.at(index);
    if(std::isfinite(e.value)&&e.value>0)add(e.position,&e,false);
    if(candidates.size()>=32)break;
  }
  for(const auto& m:o.missions)if(unfinished(m)&&m.type=="reach"&&m.position){add(*m.position,nullptr,true);if(candidates.size()>=32)break;}
  struct Reward {double total{},missions{},completed{};};
  auto reward=[&](const Candidate& first,const Candidate* second){
    double value=first.value;int gained_normal=first.value>0&&first.value<20,gained_high=first.value>=20;
    if(second&&second->value>0&&second->resource_id!=first.resource_id){value+=second->value;
      gained_normal+=second->value<20;gained_high+=second->value>=20;}
    Reward out;const double initial=o.resource_value.value_or(0);
    out.total=resource_points(initial+value)-resource_points(initial);
    for(const auto& m:o.missions){if(!unfinished(m))continue;double progress=0;
      if(m.type=="reach"&&m.position&&(first.cell==*m.position||(second&&second->cell==*m.position)))progress=1;
      else if(m.type=="normal_resources"||m.type=="high_resources"){
        const bool is_high=m.type=="high_resources";const int gained=is_high?gained_high:gained_normal;
        if((is_high?high:normal)>=m.target.value_or(1))continue;
        const double left=std::max(1.0,m.target.value_or(1)-(is_high?high:normal));
        progress=std::min(1.0,gained/left);
      }else if(m.type=="resource_value"&&m.target){
        if(initial>=*m.target)continue;
        const double left=std::max(1.0,*m.target-initial);progress=std::min(1.0,value/left);
      }
      out.missions+=5*progress;
      if(progress>=1)++out.completed;
    }
    out.total+=out.missions;return out;
  };
  std::vector<NavigationObjective> goals;
  for(const auto& c:candidates){auto r=reward(c,nullptr);if(r.total>0||c.value>0)
    goals.push_back({c.cell,c.steps,r.total,r.missions,r.total/(c.steps+1.0),c.value>0,c.reach,r.completed,c.value});}
  const bool task_available=std::any_of(goals.begin(),goals.end(),[](const auto& g){return g.mission_points>0;});
  if(task_available){
    const bool collection_pending=std::any_of(o.missions.begin(),o.missions.end(),[&](const auto& m){
      if(!unfinished(m))return false;
      if(m.type=="normal_resources")return normal<m.target.value_or(1);
      if(m.type=="high_resources")return high<m.target.value_or(1);
      return m.type=="resource_value"&&m.target&&o.resource_value.value_or(0)<*m.target;
    });
    goals.erase(std::remove_if(goals.begin(),goals.end(),[&](const auto& goal){
      if(goal.mission_points>0)return false;
      // Once collection missions are satisfied, a distant reach task need not
      // surrender nearby contested pickups. Admit a detour only with a proven
      // route to the sole unfinished reach target and 20 control Ticks of reserve.
      // This is a feasibility check, never pre-credit for completing the mission.
      if(collection_pending||goal.immediate_points<=0||o.tick>=horizon)return true;
      const Tick first_controls=std::max(1,goal.steps),remaining=horizon-o.tick;
      if(first_controls>remaining||remaining-first_controls<20)return true;
      int reach_count=0;
      for(const auto& mission:o.missions)if(unfinished(mission)&&mission.type=="reach"&&mission.position){
        ++reach_count;auto onward=between(goal.position,*mission.position);
        if(!onward||*onward<0||*onward>remaining-first_controls-20)return true;
      }
      return reach_count!=1; // Multiple visits need a joint route proof; do not guess one.
    }),goals.end());
  }
  std::sort(goals.begin(),goals.end(),[&](const auto& a,const auto& b){
    if(a.plan_rate!=b.plan_rate)return a.plan_rate>b.plan_rate;
    if(a.completed_missions!=b.completed_missions)return a.completed_missions>b.completed_missions;
    const double ar=a.resource_value/(a.steps+1.0),br=b.resource_value/(b.steps+1.0);
    return ar!=br?ar>br:o.map.index(a.position)<o.map.index(b.position);});
  if(goals.size()>8)goals.resize(8);
  std::optional<NavigationObjective> best;
  for(auto goal:goals){
    const auto& first=*std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.cell==goal.position;});
    double followup=0;
    for(const auto& next:candidates){if(next.cell==first.cell)continue;
      auto onward=between(first.cell,next.cell);if(!onward||*onward<0)continue;
      const Tick controls=std::max(1,first.steps)+static_cast<Tick>(*onward);
      if(o.tick<horizon&&controls>horizon-o.tick)continue;
      // Score the whole two-stop route together; a mission can contribute at most five points.
      const double combined=reward(first,&next).total;
      if(combined<=goal.immediate_points+1e-9)continue;
      followup=std::max(followup,combined/(controls+1.0));
    }
    goal.plan_rate+=.25*followup;
    auto secondary_better=[&]{
      if(goal.completed_missions!=best->completed_missions)return goal.completed_missions>best->completed_missions;
      const double a=goal.resource_value/(goal.steps+1.0),b=best->resource_value/(best->steps+1.0);
      return a!=b?a>b:o.map.index(goal.position)<o.map.index(best->position);
    };
    if(!best||goal.plan_rate>best->plan_rate||(goal.plan_rate==best->plan_rate&&secondary_better()))best=goal;
  }
  return best;
}
} // namespace rmtc
