#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rmtc {
using Tick = std::int64_t;
inline Tick tick_after(Tick tick,Tick count){
  return count>0&&tick>std::numeric_limits<Tick>::max()-count?std::numeric_limits<Tick>::max():tick+count;
}
using MonoTime = std::chrono::steady_clock::time_point;
struct Cell { int x{}, y{}; };
inline bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Cell a, Cell b) { return !(a == b); }
struct MapSize {
  int width{}, height{};
  bool contains(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width && c.y < height; }
  int index(Cell c) const { return c.y * width + c.x; }
  Cell cell(int i) const { return {i % width, i / width}; }
};
enum class Dir { Up, Right, Down, Left };
constexpr std::array<Dir, 4> kSearchOrder{Dir::Up, Dir::Right, Dir::Down, Dir::Left};
inline Cell delta(Dir d) {
  switch (d) { case Dir::Up: return {0,-1}; case Dir::Right: return {1,0};
    case Dir::Down: return {0,1}; case Dir::Left: return {-1,0}; }
  return {};
}
inline Cell step(Cell p, Dir d) { auto v=delta(d); return {p.x+v.x,p.y+v.y}; }
inline Dir opposite(Dir d) { return static_cast<Dir>((static_cast<int>(d)+2)%4); }
enum class ActionType { Move, Attack, Defend, Wait };
enum class EntityType { Robot, Resource, Obstacle };
enum class StageMode { Auto, Collect, Battle, Diagnostic };
enum class CollectorPolicy { Greedy, Lookahead, Frontier };
enum class ReceiptKind { Confirmed, Rejected, Mismatched, Unconfirmed, TerminalCandidate,
  MoveFailed, StateCancelled };
inline const char* receipt_kind_name(ReceiptKind kind){
  switch(kind){
    case ReceiptKind::Confirmed:return "confirmed";
    case ReceiptKind::Rejected:return "rejected";
    case ReceiptKind::Mismatched:return "mismatched";
    case ReceiptKind::Unconfirmed:return "unconfirmed";
    case ReceiptKind::TerminalCandidate:return "terminal_candidate";
    case ReceiptKind::MoveFailed:return "move_failed";
    case ReceiptKind::StateCancelled:return "state_cancelled";
  }
  return "unconfirmed";
}
inline const char* safe_receipt_error(const std::optional<std::string>& error){
  if(!error||error->empty())return "";
  for(const char* code:{"E_PARSE","E_SCHEMA","E_TICK","E_SESSION","E_STATE","E_RATE","E_OOB","E_BLOCKED"})
    if(*error==code)return code;
  return "UNKNOWN";
}
enum class ErrorCode { Parse, Type, Range, MissingField, WrongSeat, BadRelmap, TooLarge, DuplicateKey };
struct Error { ErrorCode code; std::string field; };
template<class T> using Result = std::variant<T,Error>;
struct Receipt {
  Tick tick{};
  ActionType action{ActionType::Wait};
  bool moved{};
  std::optional<Dir> effective_direction;
  std::optional<std::string> error;
};
struct Entity {
  EntityType type{EntityType::Obstacle};
  Cell position;
  std::string id;
  std::string robot_id;
  double value{};
  std::optional<double> hp;
  std::optional<bool> protected_now, defending;
};
struct Mission {
  std::string id, type;
  std::optional<double> target;
  std::optional<Cell> position;
};
struct Observation {
  Tick tick{};
  std::string session_id;
  std::string seat;
  MapSize map;
  Cell position;
  std::optional<int> vision_radius;
  bool state_complete{}, entities_complete{};
  std::optional<bool> alive, protected_now, defending;
  std::optional<Tick> respawn_in, mission_completed, invalid_actions;
  std::optional<double> hp;
  std::optional<std::string> position_reason;
  std::optional<Receipt> receipt;
  std::optional<double> resource_value, effective_damage;
  std::vector<std::string> completed_missions;
  std::vector<Entity> entities;
  std::vector<Mission> missions;
  std::string event_text, mission_text;
};
struct ActionIntent { ActionType type{ActionType::Wait}; std::optional<Dir> direction; };
struct Action { Tick tick{}; std::string session_id; ActionIntent intent; };
struct CoreConfig {
  StageMode stage{StageMode::Auto};
  CollectorPolicy policy{CollectorPolicy::Greedy};
  std::optional<Tick> horizon;
  bool wait_only{};
  bool require_publication_confirmation{};
  std::optional<double> tick_ms;
};
struct Decision {
  Action action;
  std::string reason;
  bool duplicate{}, session_changed{};
  std::optional<ReceiptKind> previous_receipt;
  struct Diagnostics {
    Tick skipped_ticks{};
    std::optional<double> arrival_interval_ms;
    bool resumed_after_silence{},position_discontinuous{};
    bool receipt_expected{},receipt_missing{},receipt_fresh{},receipt_matched{},receipt_immediate{};
    bool receipt_direction_applied{},terminal_state{};
    std::optional<ActionType> expected_receipt_action;
    std::string receipt_error,final_guard_reason;
  } diagnostics;
};
} // namespace rmtc
