#include "rmtc_bot/opportunity.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rmtc {
std::optional<OpportunityEstimate> estimate_healing_opportunity(const PublicOpportunity& e,Tick now,
    int moves,double hp,std::optional<HealingCoverage> existing,Tick horizon){
  if(e.kind!=OpportunityKind::Healing||moves<1||moves>12||now<0||!std::isfinite(hp)||hp<=0||hp>=100||
      e.heal_ticks<=0||!std::isfinite(e.heal_per_tick)||e.heal_per_tick<=0)return {};
  Tick entry=tick_after(now,moves-1),limit=now<horizon?horizon-1:std::numeric_limits<Tick>::max();
  if(entry<e.start||entry>=e.until_exclusive||entry>=e.entry_until_exclusive||entry>=limit||
      entry>std::numeric_limits<Tick>::max()-e.heal_ticks)return {};
  Tick end=std::min(entry+e.heal_ticks,limit);
  double before=0,after=0;
  if(existing&&std::isfinite(existing->per_tick)&&existing->per_tick>0){
    before=static_cast<double>(std::max<Tick>(0,std::min({entry,existing->until_exclusive,limit})-now))*existing->per_tick;
    after=static_cast<double>(std::max<Tick>(0,std::min(existing->until_exclusive,limit)-entry))*existing->per_tick;
  }
  const double missing=100-hp;
  const double without=std::min(missing,before+after);
  const double with=std::min(missing,before+static_cast<double>(end-entry)*e.heal_per_tick);
  const double incremental=std::max(0.0,with-without);
  if(incremental<=0)return {};
  // Up to four control-Tick equivalents; no healing is subtracted from pre-move combat.
  return OpportunityEstimate{entry,end,moves,incremental,4*incremental/100.0};
}
std::optional<OpportunityEstimate> estimate_fortress_opportunity(const PublicOpportunity& e,Tick now,
    int moves,double potential,Tick horizon){
  if(e.kind!=OpportunityKind::Fortress||now<0||moves<0||moves>12||e.required_dwell<1||
      e.required_dwell>32||!std::isfinite(potential)||potential<0)return {};
  const int confirmed=moves==0&&now>=e.start?std::clamp(e.confirmed_dwell,0,e.required_dwell):0;
  const int remaining=e.required_dwell-confirmed;
  if(remaining<=0||e.start<0)return {};
  const Tick arrival=tick_after(now,std::max(1,moves)-1);
  // Forecast travel and waiting consume controls, but dwell starts only when active.
  const Tick first_attack=tick_after(std::max(arrival,e.start),remaining);
  const Tick limit=now<horizon?horizon:std::numeric_limits<Tick>::max();
  if(first_attack<=now||first_attack-now>32||first_attack>=e.until_exclusive||first_attack>=limit)return {};
  const int controls=static_cast<int>(first_attack-now);
  const double useful=static_cast<double>(std::min<Tick>(10,std::min(e.until_exclusive,limit)-first_attack))/10.0;
  const double value=std::clamp(std::max(.5,potential),.5,2.0)*useful;
  return OpportunityEstimate{first_attack,std::min(e.until_exclusive,limit),controls,0,value};
}
} // namespace rmtc
