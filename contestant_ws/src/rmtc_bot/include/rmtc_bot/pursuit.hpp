#pragma once
#include "rmtc_bot/types.hpp"
#include <functional>
namespace rmtc {
// At most three currently visible robots. Hints expire on every missing frame;
// they are hypotheses for positioning, never authoritative combat/score inputs.
class PublicPursuit {
 public:
  void reset();
  void observe(const Observation&);
  bool moving(const std::string&) const;
  bool same_speed_tail(const std::string&,Cell own_velocity) const;
  std::optional<Cell> forecast(const std::string&,int steps,
      const std::function<bool(Cell,Tick)>& known_safe) const;
  bool crossfire(const std::string& target,Cell firing_cell,Cell predicted_target) const;
 private:
  struct Track {std::string seat;Cell position,velocity;double hp{};int streak{};bool eligible{};};
  std::string session_;
  MapSize map_;
  Cell self_;
  Tick tick_{-1};
  std::vector<Track> tracks_;
};
} // namespace rmtc
