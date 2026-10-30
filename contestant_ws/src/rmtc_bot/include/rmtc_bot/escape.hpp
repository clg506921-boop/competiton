#pragma once
#include "rmtc_bot/types.hpp"
#include <functional>
namespace rmtc {
enum class EscapeKind { NoThreat, Route, NoRoute, BudgetExceeded };
struct EscapeResult {EscapeKind kind{EscapeKind::NoThreat};ActionIntent first_action;int steps{};};
// Coordinates are physical; the caller converts only the next MOVE to an illusion command.
class EscapePlanner {
 public:
  EscapeResult plan(MapSize,Cell,Tick,const std::function<bool(Cell,Tick)>& blocked,
      const std::function<bool(Cell,Tick)>& danger,int lookahead=32);
 private:
  std::vector<unsigned char> dangerous_,safe_hold_;
  std::vector<int> first_,queue_;
};
} // namespace rmtc
