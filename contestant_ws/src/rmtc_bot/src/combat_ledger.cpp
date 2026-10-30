#include "rmtc_bot/combat_ledger.hpp"
#include "rmtc_bot/rules.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace rmtc {
CombatLedger project_combat_ledger(const std::vector<Combatant>& cars,const std::vector<CombatHit>& hits){
  const auto n=cars.size();CombatLedger out;
  out.damage_taken.assign(n,0);out.damage_credit.assign(n,0);out.kill_credit.assign(n,0);out.dead.assign(n,false);
  std::vector<std::set<std::size_t>> contributors(n);
  for(const auto& hit:hits){
    if(hit.source>=n||hit.target>=n||hit.source==hit.target||!std::isfinite(hit.nominal)||hit.nominal<=0||
       cars[hit.source].hp<=0||cars[hit.target].hp<=0||cars[hit.target].protected_now)continue;
    out.damage_taken[hit.target]+=hit.nominal;contributors[hit.target].insert(hit.source);
  }
  for(std::size_t i=0;i<n;++i){
    out.dead[i]=cars[i].hp>0&&out.damage_taken[i]>=cars[i].hp;
    if(out.dead[i]&&!contributors[i].empty())
      for(auto source:contributors[i])out.kill_credit[source]+=rules::kill_damage_pool/contributors[i].size();
  }
  for(const auto& hit:hits){
    if(!hit.ordinary||hit.source>=n||hit.target>=n||hit.source==hit.target||!std::isfinite(hit.nominal)||hit.nominal<=0||
       cars[hit.source].hp<=0||cars[hit.target].hp<=0||cars[hit.target].protected_now)continue;
    out.damage_credit[hit.source]+=apportioned_effective_damage(cars[hit.target].hp,hit.nominal,out.damage_taken[hit.target]);
  }
  return out;
}
double projected_effective_damage(double previous,double ordinary,double kill,bool dead){
  if(!std::isfinite(previous)||!std::isfinite(ordinary)||!std::isfinite(kill)||previous<0||ordinary<0||kill<0)return 0;
  return std::max(0.0,previous+ordinary+kill-(dead?rules::death_damage_penalty:0));
}
} // namespace rmtc
