#include "rmtc_bot/recon.hpp"
#include "rmtc_bot/battle.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace rmtc {
std::optional<ReconGoal> select_combat_recon(const Observation& o,const World& world,Tick horizon,
    const ReconDistance& distance,const ReconFeasible& feasible){
  const auto map=world.size();
  if(!o.alive.value_or(false)||!o.state_complete||!o.entities_complete||!o.vision_radius||
      *o.vision_radius<1||*o.vision_radius>4||o.tick<0||map.width!=o.map.width||map.height!=o.map.height||!map.contains(o.position)||
      !o.effective_damage||!std::isfinite(*o.effective_damage)||*o.effective_damage<0)return {};
  double remaining=std::max(0.0,15-combat_points(*o.effective_damage));
  for(const auto& m:o.missions)if(m.type=="effective_damage"&&m.target&&std::isfinite(*m.target)&&
      *o.effective_damage<*m.target&&std::find(o.completed_missions.begin(),o.completed_missions.end(),m.id)==o.completed_missions.end())remaining+=5;
  // Fixed-size shortlist bounds route checks. Staleness is from our own valid observations.
  constexpr int age_limit=20,max_steps=12;
  std::array<ReconGoal,8> best{};std::size_t used=0;const int radius=*o.vision_radius;
  const double value_cap=remaining>0?std::min(.25,.05*remaining):.25;
  auto better=[&](const ReconGoal& a,const ReconGoal& b){
    return a.estimated_rate!=b.estimated_rate?a.estimated_rate>b.estimated_rate:map.index(a.position)<map.index(b.position);};
  for(int index=0;index<map.width*map.height;++index){
    const auto p=map.cell(index);if(p==o.position||world.terrain(p)!=Terrain::Free)continue;
    auto steps=distance(p);if(!steps||*steps<1||*steps>max_steps||(o.tick<horizon&&*steps>=horizon-o.tick))continue;
    double stale=0;int cells=0,footprint=0;
    for(int y=std::max(0,p.y-radius);y<=std::min(map.height-1,p.y+radius);++y)
      for(int x=std::max(0,p.x-radius);x<=std::min(map.width-1,p.x+radius);++x){
        ++footprint;Cell cell{x,y};
        if(std::abs(x-o.position.x)<=radius&&std::abs(y-o.position.y)<=radius)continue;
        if(world.terrain(cell)!=Terrain::Free)continue;
        const auto observed=world.last_observed(cell);
        // Unknown terrain remains the ordinary explorer's responsibility.
        if(observed<0||observed>o.tick)continue;
        const Tick age=std::min<Tick>(age_limit,o.tick-observed);if(age<2)continue;
        ++cells;stale+=static_cast<double>(age)/age_limit;
      }
    if(cells==0||footprint==0)continue;
    ReconGoal goal{p,*steps,cells,value_cap*stale/footprint/(*steps+1.0)};
    std::size_t place=0;while(place<used&&!better(goal,best[place]))++place;
    if(place==best.size())continue;
    if(used<best.size())++used;
    for(std::size_t i=used-1;i>place;--i){best[i]=best[i-1];}
    best[place]=goal;
  }
  for(std::size_t i=0;i<used;++i)if(feasible(best[i].position,best[i].steps))return best[i];
  return {};
}
} // namespace rmtc
