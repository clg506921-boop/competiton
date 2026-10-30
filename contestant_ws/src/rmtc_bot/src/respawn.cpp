#include "rmtc_bot/respawn.hpp"
#include <algorithm>
#include <queue>
namespace rmtc {
std::optional<Cell> choose_respawn(Cell spawn,MapSize map,const std::function<bool(Cell)>& blocked,
                                  const std::vector<Cell>& occupied){
  if(!map.contains(spawn))return {};
  std::vector<bool> seen(static_cast<std::size_t>(map.width*map.height),false);
  std::queue<Cell> pending;pending.push(spawn);seen[map.index(spawn)]=true;
  while(!pending.empty()){
    Cell c=pending.front();pending.pop();
    if(!blocked(c)&&std::find(occupied.begin(),occupied.end(),c)==occupied.end())return c;
    for(auto d:kSearchOrder){
      Cell n=step(c,d);if(!map.contains(n)||seen[map.index(n)])continue;
      seen[map.index(n)]=true;pending.push(n); // Even blocked/occupied cells expand the search.
    }
  }
  return {};
}
std::map<char,Cell> choose_simultaneous_respawns(const std::map<char,Cell>& spawns,MapSize map,
    const std::function<bool(Cell)>& blocked,std::vector<Cell> occupied){
  std::map<char,Cell> result;
  for(char seat='A';seat<='D';++seat){
    auto it=spawns.find(seat);if(it==spawns.end())continue;
    auto landing=choose_respawn(it->second,map,blocked,occupied);
    if(landing){result.emplace(seat,*landing);occupied.push_back(*landing);}
  }
  return result;
}
} // namespace rmtc
