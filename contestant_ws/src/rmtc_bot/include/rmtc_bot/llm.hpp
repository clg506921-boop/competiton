#pragma once
#include "rmtc_bot/types.hpp"
#include "rmtc_bot/events.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace rmtc {
struct LlmConfig {
  std::string url,model,token;
  int max_tokens{};
  bool official_default_model_authorized{};
  std::uint64_t call_quota{100}; // Rule default; 0 only for announced unlimited. Immutable per token.
};
struct LlmJob {
  std::string session_id,source_id;
  std::uint64_t generation{};
  bool qualification{};
  nlohmann::json messages;
  int attempt{};
  std::optional<EventContext> event_context{};
  bool portable_fallback{};
};
// Official deepseek-flash always disables thinking and requests JSON output,
// including retries. Legacy research models retain portable compatibility.
// No announcement text is inspected here.
bool uses_fast_model_profile(const LlmConfig&,const LlmJob&) noexcept;
nlohmann::json make_chat_request(const LlmConfig&,const LlmJob&);
enum class CompletionFinish { Unknown, Stop, Length, ContentFilter, Other };
const char* completion_finish_name(CompletionFinish) noexcept;
struct HttpReply {
  long status{};
  std::string error_code,content;
  bool forwarded_success{},uncertain_inflight{},cancelled{};
  CompletionFinish finish{CompletionFinish::Unknown};
  std::size_t response_bytes{};
  double request_ms{};
};
// Decode only the model's HTTP response, never an announcement. No network access.
HttpReply decode_chat_completion(long status,std::string_view body) noexcept;
using CancelCheck=std::function<bool()>;
class ModelTransport {
 public:
  virtual ~ModelTransport()=default;
  virtual HttpReply send(const LlmConfig&,const LlmJob&,const CancelCheck&)=0;
};
class CurlTransport final:public ModelTransport {
 public:
  CurlTransport();~CurlTransport() override;
  HttpReply send(const LlmConfig&,const LlmJob&,const CancelCheck&) override;
 private:
  void* handle_{};
};
enum class SubmitResult { Accepted, Disabled, Full, Duplicate, SessionMismatch, Stopped };
enum class ModelOutputState { NotRequested, TransportError, EmptyContent, Truncated, Filtered,
  ParseRejected, EnvelopeRejected, ItemsRejected, PartiallyAccepted, Unresolved, NoEvents,
  Validated, ValidationFailed };
const char* model_output_state_name(ModelOutputState) noexcept;
struct ModelDiagnostics {
  ModelOutputState state{ModelOutputState::NotRequested};
  std::string parse_detail,first_rejection_field;
  std::uint32_t ignored_fields{},accepted{},unresolved{};
  std::array<std::uint32_t,static_cast<std::size_t>(EventIssue::Count)> rejected{};
};
struct LlmResult {
  LlmJob job; HttpReply reply;
  std::shared_ptr<const InterpretedEvents> interpreted;
  std::string output_error;
  ModelDiagnostics diagnostics;
};
// Worker-only normalization and validation. Exposes bounded diagnostics without response text.
void inspect_model_reply(LlmResult&) noexcept;
struct LlmUsage {std::uint64_t attempts{},successful{},uncertain_reserved{},superseded_pending{},skipped_retries{};};
// One worker / one in-flight request is valid for both practice (max 1) and formal (max 2).
// Network may block this worker, never the ROS control thread.
class LlmScheduler {
 public:
  LlmScheduler(LlmConfig,std::unique_ptr<ModelTransport>);
  ~LlmScheduler();
  void begin_session(std::string session,std::uint64_t generation);
  SubmitResult submit(LlmJob);
  std::vector<LlmResult> take_results(std::size_t maximum=16);
  LlmUsage usage();
  bool event_work_pending();
  void shutdown();
  bool enabled() const { return enabled_; }
  std::uint64_t dropped_results() const {return dropped_results_.load();}
 private:
  void run() noexcept;
  void loop();
  LlmConfig config_;std::unique_ptr<ModelTransport> transport_;
  const bool enabled_;
  std::mutex mutex_;std::condition_variable cv_;
  std::string session_;
  std::atomic<std::uint64_t> generation_{0},dropped_results_{0};
  std::atomic<bool> stopping_{false};
  bool requests_stopped_{};
  bool optional_extensions_rejected_{};
  bool event_active_{};
  LlmUsage usage_;
  std::deque<LlmJob> queue_;std::deque<LlmResult> results_;std::set<std::string> submitted_;
  std::thread worker_;
};
bool valid_llm_config(const LlmConfig&);
} // namespace rmtc
