#pragma once
#include "rmtc_bot/types.hpp"
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace rmtc {
enum class GateResult { NewTick, NewSession, Duplicate, Drop };
class SessionClock {
 public:
  explicit SessionClock(double initial_ms=100) : initial_ms_(initial_ms) {}
  GateResult accept(const Observation& o,MonoTime now);
  double period_ms() const;
  std::uint64_t generation() const { return generation_; }
 private:
  std::string session_;
  Tick tick_{-1};
  double initial_ms_;
  std::uint64_t generation_{};
  MonoTime advanced_{};
  std::deque<double> intervals_;
  std::unordered_set<std::string> retired_;
};
enum class Terrain { Unknown, Free, Wall };
class World {
 public:
  void reset(MapSize map);
  void update(const Observation& o);
  MapSize size() const { return map_; }
  Terrain terrain(Cell c) const;
  bool blocked(Cell c,Tick tick,bool ignore_remembered_wall=false) const;
  Tick first_wall_seen(Cell c)const;
  Tick last_wall_seen(Cell c)const;
  Tick last_observed(Cell c)const;
  void forget_expired_wall(Cell c,Tick start,Tick until,Tick now);
  int visits(Cell c) const;
  const std::unordered_map<int,Entity>& resources() const { return resources_; }
  void block_temporarily(Cell c,Tick until);
 private:
  MapSize map_;
  std::vector<Terrain> terrain_;
  std::vector<Tick> blocked_until_;
  std::vector<Tick> wall_first_,wall_last_;
  std::vector<Tick> observed_;
  std::vector<int> visits_;
  std::deque<int> recent_;
  std::unordered_map<int,Entity> resources_;
};
std::vector<int> unknown_footprint_counts(const World&,int radius);
struct PlanContext {World& world;bool new_session{},battle{},receipt_reversed{};};
using MovementBlocked=std::function<bool(Cell,Tick)>;
struct PlanOverride {ActionIntent intent;std::string reason;std::optional<bool> movement_reversed;
  MovementBlocked movement_blocked;};
using PlanHook=std::function<PlanOverride(const Observation&,ActionIntent,const PlanContext&)>;
class BotCore {
 public:
  explicit BotCore(CoreConfig config={});
  std::optional<Decision> process(const Observation& observation,MonoTime now,const PlanHook& hook={});
  void confirm_battle(){if(config_.stage==StageMode::Auto||config_.stage==StageMode::Battle)battle_seen_=true;}
  StageMode active_stage()const{return config_.stage==StageMode::Diagnostic?StageMode::Diagnostic:battle_seen_?StageMode::Battle:StageMode::Collect;}
  void note_published(Tick tick);
  double action_budget_ms()const{return config_.tick_ms.value_or(battle_seen_?500.0:100.0);}
  const World& world() const { return world_; }
  double period_ms() const { return clock_.period_ms(); }
 private:
  ActionIntent plan(const Observation& o,std::string& reason);
  ActionIntent guard(ActionIntent intent,const Observation& o,const MovementBlocked&,std::string& reason) const;
  CoreConfig config_;
  SessionClock clock_;
  World world_;
  std::optional<Action> cached_;
  std::optional<Cell> goal_;
  struct SentAction {Tick tick;ActionIntent intent;Cell origin;bool alive;
    std::optional<Dir> attempted_direction;bool published;};
  std::deque<SentAction> sent_;
  std::optional<Tick> previous_tick_;
  MonoTime previous_received_{};
  std::deque<Cell> positions_;
  Tick last_receipt_tick_{-1};
  bool reversed_{},battle_seen_{};
  int submissions_{},stalled_{};
  std::vector<double> distance_buffer_;
  std::vector<int> step_buffer_,first_buffer_,candidate_buffer_;
  std::vector<unsigned char> occupied_buffer_;
  std::vector<std::pair<double,int>> heap_buffer_;
};
} // namespace rmtc
