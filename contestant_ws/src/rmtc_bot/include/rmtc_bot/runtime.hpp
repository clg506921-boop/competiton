#pragma once
#include "rmtc_bot/config.hpp"
#include "rmtc_bot/core.hpp"
#include "rmtc_bot/event_memory.hpp"
#include "rmtc_bot/escape.hpp"
#include "rmtc_bot/objective.hpp"
#include "rmtc_bot/recon.hpp"
#include "rmtc_bot/pursuit.hpp"
#include <deque>
#include <set>
#include <utility>
namespace rmtc {
struct RuntimeStatus {
  StageMode stage{StageMode::Collect};
  DecisionType decision_type{DecisionType::Balanced};
  bool ai_enabled{},local_success_observed{},merge_budget_exhausted{};
  std::uint64_t generation{},announcements_dropped{},model_rejected{},unlinked_dropped{};
  LlmUsage usage;
  long last_http_status{};
  std::string last_ai_error,last_ai_source;
  double last_ai_request_ms{};
  std::size_t last_ai_response_bytes{};
  CompletionFinish last_ai_finish{CompletionFinish::Unknown};
  ModelDiagnostics last_model_diagnostics;
  Tick last_event_observed_tick{-1},last_event_consumed_tick{-1};
  std::uint64_t events_validated{},events_applied{},events_ignored{},sources_unresolved{},truncated_responses{};
  int confirmed_normal_min{},confirmed_high_min{};
  std::uint64_t collection_gaps{};
};
// ROS-free coordinator. Call process() and then, AFTER publishing, after_publish().
// All state here belongs to the control thread; scheduler owns HTTP and output validation.
class BotRuntime {
 public:
  explicit BotRuntime(RuntimeConfig,std::unique_ptr<ModelTransport> transport=nullptr);
  ~BotRuntime();
  void shutdown();
  std::optional<Decision> process(const Observation&,MonoTime);
  void after_publish(const Observation&);
  RuntimeStatus status();
  double period_ms()const{return core_.period_ms();}
  double action_budget_ms()const{return core_.action_budget_ms();}
  void note_published(Tick tick){core_.note_published(tick);}
 private:
  struct Notice {std::string id,body;Tick observed{};};
  struct Pending {nlohmann::json event;Tick observed{};};
  PlanOverride choose(const Observation&,ActionIntent,const PlanContext&);
  void reset(const Observation&);
  void merge_results(const Observation&);
  void collect_notices(const Observation&);
  ActionIntent tactical(const Observation&,ActionIntent,const PlanContext&,bool reversed,std::string&);
  RuntimeConfig config_;BotCore core_;EventMemory events_;
  std::unique_ptr<LlmScheduler> scheduler_;
  RuntimeStatus status_;
  std::string session_;
  std::set<std::string> seen_bodies_;
  std::deque<Notice> notices_;
  std::deque<LlmResult> results_;
  std::deque<Pending> unlinked_;
  std::size_t notice_bytes_{},notice_sequence_{};
  bool qualification_queued_{},post_publish_ready_{};
  Tick latest_tick_{-1};std::optional<Tick> illusion_until_;
  std::vector<int> distance_,first_,parent_,queue_;
  std::vector<int> objective_distance_,objective_queue_;
  EscapePlanner escape_;
  Tick progress_tick_{-1};std::optional<double> previous_resource_value_;
  std::unordered_map<int,Entity> previous_resources_;
  std::set<std::string> collected_ids_;
  int confirmed_normal_{},confirmed_high_{};
  struct TacticalSample {Tick tick;Cell position;double damage,resource;std::size_t missions;bool alive;ActionType action;
    std::vector<std::pair<std::string,Cell>> diagonal_enemies;};
  std::deque<TacticalSample> tactical_history_;
  struct LosingSite {Cell position;Tick tick;};
  std::deque<LosingSite> losing_sites_;
  std::optional<Cell> recon_goal_;
  Tick recon_until_{};
  PublicPursuit pursuit_;
};
} // namespace rmtc
