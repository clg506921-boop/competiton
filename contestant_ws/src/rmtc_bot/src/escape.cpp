#include "rmtc_bot/escape.hpp"
#include <algorithm>
#include <limits>
namespace rmtc {
EscapeResult EscapePlanner::plan(MapSize map,Cell start,Tick now,
    const std::function<bool(Cell,Tick)>& blocked,const std::function<bool(Cell,Tick)>& danger,int horizon){
  if(!map.contains(start)||map.width<1||map.height<1||map.width>256||map.height>256||now<0||horizon<1||horizon>32)return {EscapeKind::BudgetExceeded,{},0};
  horizon=static_cast<int>(std::min<Tick>(horizon,std::numeric_limits<Tick>::max()-now));
  if(!horizon)return {};
  bool threat=false;for(int dt=1;dt<=horizon;++dt)if(danger(start,now+dt)){threat=true;break;}
  if(!threat)return {};
  const auto cells=static_cast<std::uint64_t>(map.width)*map.height;
  const auto total=cells*(horizon+1);
  if(total>65536)return {EscapeKind::BudgetExceeded,{},0};
  const int n=static_cast<int>(cells),count=static_cast<int>(total),origin=map.index(start);
  dangerous_.assign(count,0);safe_hold_.assign(count,0);first_.assign(count,-1);
  queue_.clear();queue_.reserve(count);
  for(int dt=horizon;dt>=0;--dt)for(int i=0;i<n;++i){int state=dt*n+i;
    dangerous_[state]=danger(map.cell(i),now+dt);
    safe_hold_[state]=!dangerous_[state]&&(dt==horizon||safe_hold_[state+n]);
  }
  // The caller handles unavoidable activation at the current Tick separately.
  if(safe_hold_[n+origin])return {};
  first_[origin]=-2;queue_.push_back(origin);
  for(std::size_t head=0;head<queue_.size();++head){int state=queue_[head],dt=state/n,i=state%n;
    if(dt>=horizon)continue;
    for(int d=0;d<5;++d){Cell p=d==4?map.cell(i):step(map.cell(i),kSearchOrder[d]);
      if(!map.contains(p)||(d!=4&&blocked(p,now+dt)))continue;
      int next=(dt+1)*n+map.index(p);
      if(dangerous_[next]||first_[next]!=-1)continue;
      first_[next]=dt==0?d:first_[state];queue_.push_back(next);
      if(safe_hold_[next]){int chosen=first_[next];return {EscapeKind::Route,
        chosen==4?ActionIntent{}:ActionIntent{ActionType::Move,kSearchOrder[chosen]},dt+1};}
    }
  }
  return {EscapeKind::NoRoute,{},0};
}
} // namespace rmtc
