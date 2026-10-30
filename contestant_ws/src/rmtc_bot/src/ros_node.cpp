#include "rmtc_bot/runtime.hpp"
#include "rmtc_bot/protocol.hpp"
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
using Json=nlohmann::json;
class AsyncLog {
 public:
  explicit AsyncLog(std::string path):path_(std::move(path)),worker_([this]{run();}){}
  ~AsyncLog(){
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
    cv_.notify_all();if(worker_.joinable())worker_.join();
  }
  void push(std::string record){
    {std::lock_guard<std::mutex> lock(mutex_);
      if(queue_.size()>=1024){++dropped_;return;}queue_.push_back(std::move(record));}
    cv_.notify_one();
  }
  std::uint64_t dropped()const{return dropped_.load();}
 private:
  void run() noexcept {
    try{
      std::ofstream file(path_,std::ios::app);if(!file){std::cerr<<"Cannot open bot log\n";return;}
      while(true){
        std::string row;
        {std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[this]{return stopping_||!queue_.empty();});
          if(queue_.empty()&&stopping_)break;
          row=std::move(queue_.front());queue_.pop_front();}
        file<<row<<'\n';file.flush(); // Worker only; retain completed rows before a later process crash.
      }
    }catch(...){std::cerr<<"Bot log writer stopped after exception\n";}
  }
  std::string path_;std::mutex mutex_;std::condition_variable cv_;
  std::deque<std::string> queue_;bool stopping_{};std::atomic<std::uint64_t> dropped_{};std::thread worker_;
};
std::string env(const char* key,const char* fallback){auto value=std::getenv(key);return value?value:fallback;}
Json startup_file(const char* variable,Json fallback){
  auto path=env(variable,"");if(path.empty())return fallback;
  std::ifstream file(path,std::ios::binary);if(!file)throw std::invalid_argument("Cannot open startup configuration file");
  std::string bytes;char ch;while(file.get(ch)){if(bytes.size()>=65536)throw std::invalid_argument("Startup configuration too large");bytes+=ch;}
  bool duplicate=false;std::vector<std::set<std::string>> keys;
  auto callback=[&](int depth,Json::parse_event_t e,Json& value){
    if(depth>16)return false;
    if(e==Json::parse_event_t::object_start)keys.emplace_back();
    else if(e==Json::parse_event_t::key&&!keys.empty()&&!keys.back().insert(value.get<std::string>()).second)duplicate=true;
    else if(e==Json::parse_event_t::object_end&&!keys.empty())keys.pop_back();
    return true;
  };
  auto j=Json::parse(bytes,callback,false);if(duplicate||j.is_discarded()||!j.is_object())throw std::invalid_argument("Invalid startup configuration JSON");return j;
}
rmtc::RuntimeConfig config(bool wait,const std::string& policy){
  std::map<std::string,std::string> values;
  for(auto key:{"RMTC_STAGE","RMTC_MAX_TICKS","RMTC_TICK_MS","RMTC_AI_URL","RMTC_TEAM_TOKEN","RMTC_AI_TOKEN","RMTC_AI_MODEL",
      "RMTC_AI_MAX_TOKENS","RMTC_AI_QUOTA","RMTC_AI_ALLOW_DEFAULT_MODEL"})
    if(auto value=std::getenv(key))values[key]=value;
  auto out=rmtc::parse_runtime_config(startup_file("RMTC_CONNECTION_CONFIG",Json::object()),values,
    startup_file("RMTC_DECISION_CONFIG",Json{{"decision_type","balanced"}}),policy,wait);
  out.core.require_publication_confirmation=true;return out;
}
class BotNode:public rclcpp::Node {
 public:
  BotNode():Node("turing_bot_node"),seat_(declare_parameter<std::string>("seat","A")),
    team_(declare_parameter<std::string>("team_name","RMTC-M0")),
    mode_(declare_parameter<std::string>("mode","collect")),
    policy_(declare_parameter<std::string>("policy","greedy")),
    dispatch_(declare_parameter<std::string>("dispatch","immediate")),
    runtime_(config(mode_=="wait",policy_)),log_(env("RMTC_LOG_FILE","rmtc-bot.jsonl")){
    if(!rmtc::valid_seat(seat_)||!rmtc::valid_team_name(team_)||(mode_!="wait"&&mode_!="collect")||
       (dispatch_!="immediate"&&dispatch_!="mailbox"))
      throw std::invalid_argument("Invalid seat, team_name or mode");
    const std::string prefix="/team_"+seat_;
    registration_=create_publisher<std_msgs::msg::String>(prefix+"/registration",
      rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local());
    connect_data_endpoints();
    if(dispatch_=="mailbox")dispatch_timer_=create_wall_timer(std::chrono::milliseconds(1),[this](){
      try {auto msg=std::move(latest_);if(msg)receive(*msg,latest_received_);}
      catch(...){RCLCPP_ERROR(get_logger(),"Mailbox timer failed");}
    });
    health_timer_=create_wall_timer(std::chrono::seconds(1),[this](){
      const auto now=std::chrono::steady_clock::now();
      if(terminal_observed_||now-last_health_log_<std::chrono::seconds(5))return;
      auto ms=[&](auto at){return std::chrono::duration<double,std::milli>(now-at).count();};
      const double threshold=std::max(3000.0,3*runtime_.period_ms());
      if(ms(last_message_at_)<threshold&&ms(last_advanced_at_)<threshold)return;
      last_health_log_=now;
      log_.push(Json{{"kind","control_health"},{"run_id",run_id_},{"seat",seat_},
        {"session_id",health_session_},{"last_tick",health_tick_},
        {"state",ms(last_message_at_)>=threshold?"perception_silence":"tick_not_advancing"},
        {"message_silence_ms",ms(last_message_at_)},{"tick_silence_ms",ms(last_advanced_at_)},
        {"diagnostic_only",true},{"pause_or_disconnect_possible",true},{"actions_generated",0}}.dump());
      if(ms(last_message_at_)>=threshold&&now-last_endpoint_rebuild_at_>=std::chrono::seconds(5)){
        last_endpoint_rebuild_at_=now;
        try{
          connect_data_endpoints();latest_.reset();++endpoint_rebuilds_;
          log_.push(Json{{"kind","transport_recovery"},{"run_id",run_id_},{"seat",seat_},
            {"last_tick",health_tick_},{"attempt",endpoint_rebuilds_},{"endpoints_recreated",true},
            {"actions_generated",0},{"session_reset",false},{"pause_or_disconnect_possible",true}}.dump());
        }catch(...){
          log_.push(Json{{"kind","transport_recovery"},{"run_id",run_id_},{"seat",seat_},
            {"last_tick",health_tick_},{"endpoints_recreated",false},{"actions_generated",0}}.dump());
        }
      }
    });
    std_msgs::msg::String registration;registration.data=Json{{"team",seat_},{"name",team_}}.dump();
    registration_->publish(registration);
    const auto status=runtime_.status();
    log_.push(Json{{"kind","startup"},{"log_schema_version",1},{"run_id",run_id_},{"seat",seat_},
      {"stage",rmtc::stage_name(status.stage)},{"decision_type",rmtc::decision_type_name(status.decision_type)},
      {"ai_enabled",status.ai_enabled}}.dump());
    RCLCPP_INFO(get_logger(),"Bot ready: seat=%s run_mode=%s stage=%s decision_type=%s AI=%s",seat_.c_str(),mode_.c_str(),rmtc::stage_name(status.stage),rmtc::decision_type_name(status.decision_type),status.ai_enabled?"configured":"disabled");
  }
  ~BotNode()override{runtime_.shutdown();} // HTTP joins before AsyncLog is destroyed.
 private:
  void connect_data_endpoints(){
    const auto prefix="/team_"+seat_;
    const auto qos=rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
    auto publisher=create_publisher<std_msgs::msg::String>(prefix+"/action",qos);
    auto subscription=create_subscription<std_msgs::msg::String>(prefix+"/perception",qos,
      [this](std_msgs::msg::String::ConstSharedPtr msg){
        try{
          const auto received=std::chrono::steady_clock::now();last_message_at_=received;
          if(dispatch_=="immediate")receive(*msg,received);
          else {if(latest_)++overwritten_;latest_=msg;latest_received_=received;}
        }catch(...){RCLCPP_ERROR(get_logger(),"Dispatch callback failed");}
      });
    // SingleThreadedExecutor: no perception callback is running during replacement.
    // Install both only after construction succeeds. Never replay an old action here.
    publisher_=std::move(publisher);subscription_=std::move(subscription);
  }
  void receive(const std_msgs::msg::String& msg,rmtc::MonoTime received) noexcept {
    try{
      const auto begin=std::chrono::steady_clock::now();
      auto parsed=rmtc::parse_observation(msg.data,seat_);
      if(auto error=std::get_if<rmtc::Error>(&parsed)){
        log_.push(Json{{"kind","parse_error"},{"log_schema_version",1},{"run_id",run_id_},
          {"seat",seat_},{"field",error->field}}.dump());return;
      }
      const auto parsed_at=std::chrono::steady_clock::now();
      const auto& o=std::get<rmtc::Observation>(parsed);
      auto d=runtime_.process(o,begin);if(!d)return;
      if(health_session_!=o.session_id||o.tick>health_tick_){last_advanced_at_=received;health_tick_=o.tick;health_session_=o.session_id;}
      terminal_observed_=d->diagnostics.terminal_state;
      const auto planned_at=std::chrono::steady_clock::now();
      std_msgs::msg::String output;output.data=rmtc::encode_action(d->action);
      const auto encoded_at=std::chrono::steady_clock::now();
      const double budget_ms=runtime_.action_budget_ms();
      const bool expired=std::chrono::duration<double,std::milli>(encoded_at-received).count()>=budget_ms;
      bool sent=false,publication_failed=false;
      if(!d->diagnostics.terminal_state&&!expired){
        try{publisher_->publish(output);runtime_.note_published(o.tick);sent=true;}
        catch(...){publication_failed=true;}
      }
      const auto published=std::chrono::steady_clock::now();
      if(sent&&!d->duplicate){try{runtime_.after_publish(o);}catch(...){RCLCPP_ERROR(get_logger(),"Post-publish AI preparation failed");}}
      const auto state=runtime_.status();
      const auto post_publish_at=std::chrono::steady_clock::now();
      auto micros=[](auto duration){return std::chrono::duration<double,std::micro>(duration).count();};
      const auto& diagnostic=d->diagnostics;
      Json row={{"kind","decision"},{"log_schema_version",1},{"run_id",run_id_},{"seat",seat_},{"stage",rmtc::stage_name(state.stage)},
        {"decision_type",rmtc::decision_type_name(state.decision_type)},{"session_id",o.session_id},{"tick",o.tick},
        {"action",rmtc::action_name(d->action.intent.type)},{"reason",d->reason},
        {"policy",policy_},{"dispatch",dispatch_},{"mailbox_overwritten",overwritten_},
        {"position",{o.position.x,o.position.y}},{"duplicate",d->duplicate},
        {"session_changed",d->session_changed},{"application_us",micros(published-received)},
        {"published",sent},{"publication_failed",publication_failed},{"terminal_state",d->diagnostics.terminal_state},
        {"expired_before_publish",expired},{"action_budget_ms",budget_ms},
        {"publish_returned_after_budget",sent&&micros(published-received)>=budget_ms*1000},
        {"dispatch_wait_us",micros(begin-received)},
        {"plan_us",micros(planned_at-parsed_at)},{"encode_us",micros(encoded_at-planned_at)},
        {"publish_us",micros(published-encoded_at)},{"post_publish_us",micros(post_publish_at-published)},
        {"parse_us",micros(parsed_at-begin)},{"decision_publish_us",micros(published-parsed_at)},
        {"tick_period_ms",runtime_.period_ms()},{"log_dropped",log_.dropped()},
        {"ai_enabled",state.ai_enabled},{"ai_local_success",state.local_success_observed},
        {"ai_attempts",state.usage.attempts},{"ai_successful",state.usage.successful},
        {"ai_uncertain_reserved",state.usage.uncertain_reserved},{"model_rejected",state.model_rejected},
        {"ai_superseded_pending",state.usage.superseded_pending},{"ai_skipped_retries",state.usage.skipped_retries},
        {"ai_http_status",state.last_http_status},{"ai_error",state.last_ai_error},{"ai_source_id",state.last_ai_source},
        {"ai_request_ms",state.last_ai_request_ms},{"ai_response_bytes",state.last_ai_response_bytes},
        {"ai_finish_reason",rmtc::completion_finish_name(state.last_ai_finish)},
        {"model_output_state",rmtc::model_output_state_name(state.last_model_diagnostics.state)},
        {"model_parse_detail",state.last_model_diagnostics.parse_detail},
        {"model_rejection_field",state.last_model_diagnostics.first_rejection_field},
        {"model_ignored_fields",state.last_model_diagnostics.ignored_fields},
        {"model_event_accepted",state.last_model_diagnostics.accepted},{"model_unresolved",state.last_model_diagnostics.unresolved},
        {"model_rejected_envelope",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Envelope)]},
        {"model_rejected_schema",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Schema)]},
        {"model_rejected_source",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Source)]},
        {"model_rejected_link",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Link)]},
        {"model_rejected_geometry",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Geometry)]},
        {"model_rejected_time",state.last_model_diagnostics.rejected[static_cast<std::size_t>(rmtc::EventIssue::Time)]},
        {"model_events_validated_total",state.events_validated},{"model_events_merged_total",state.events_applied},
        {"model_events_ignored_total",state.events_ignored},{"model_unresolved_total",state.sources_unresolved},
        {"confirmed_normal_min",state.confirmed_normal_min},{"confirmed_high_min",state.confirmed_high_min},
        {"collection_gaps",state.collection_gaps},
        {"model_truncated_total",state.truncated_responses},
        {"model_observed_tick",state.last_event_observed_tick},{"model_consumed_tick",state.last_event_consumed_tick},
        {"merge_budget_exhausted",state.merge_budget_exhausted},{"announcements_dropped",state.announcements_dropped},
        {"skipped_ticks",diagnostic.skipped_ticks},{"resumed_after_silence",diagnostic.resumed_after_silence},
        {"position_discontinuous",diagnostic.position_discontinuous},
        {"receipt_expected",diagnostic.receipt_expected},{"receipt_missing",diagnostic.receipt_missing},
        {"receipt_fresh",diagnostic.receipt_fresh},{"receipt_matched",diagnostic.receipt_matched},
        {"receipt_immediate",diagnostic.receipt_immediate},{"receipt_direction_applied",diagnostic.receipt_direction_applied}};
      if(diagnostic.arrival_interval_ms)row["arrival_interval_ms"]=*diagnostic.arrival_interval_ms;
      if(diagnostic.expected_receipt_action)row["expected_receipt_action"]=rmtc::action_name(*diagnostic.expected_receipt_action);
      if(!diagnostic.receipt_error.empty())row["receipt_error"]=diagnostic.receipt_error;
      if(!diagnostic.final_guard_reason.empty())row["final_guard_reason"]=diagnostic.final_guard_reason;
      if(o.hp)row["hp"]=*o.hp;
      if(o.position_reason)row["position_reason"]=*o.position_reason;
      if(o.alive)row["alive"]=*o.alive;
      if(o.protected_now)row["protected_now"]=*o.protected_now;
      if(o.resource_value)row["resource_value"]=*o.resource_value;
      if(o.effective_damage)row["effective_damage"]=*o.effective_damage;
      if(o.respawn_in)row["respawn_in"]=*o.respawn_in;
      if(o.defending)row["defending_observed"]=*o.defending;
      if(o.mission_completed)row["mission_completed"]=*o.mission_completed;
      if(o.invalid_actions)row["invalid_actions"]=*o.invalid_actions;
      if(d->action.intent.direction)row["direction"]=rmtc::direction_name(*d->action.intent.direction);
      if(d->previous_receipt){row["receipt_kind"]=static_cast<int>(*d->previous_receipt);
        row["receipt_status"]=rmtc::receipt_kind_name(*d->previous_receipt);}
      if(o.receipt){row["receipt_tick"]=o.receipt->tick;row["moved"]=o.receipt->moved;
        row["receipt_action"]=rmtc::action_name(o.receipt->action);}
      Json opponents=Json::array();std::size_t visible_opponents=0;
      for(const auto& entity:o.entities)if(entity.type==rmtc::EntityType::Robot){
        ++visible_opponents;if(opponents.size()>=3)continue;
        Json seen={{"id",entity.robot_id},{"position",{entity.position.x,entity.position.y}}};
        if(entity.hp)seen["hp"]=*entity.hp;
        if(entity.protected_now)seen["protected"]=*entity.protected_now;
        if(entity.defending)seen["defending_observed"]=*entity.defending;
        opponents.push_back(std::move(seen));
      }
      row["visible_opponents"]=std::move(opponents);row["visible_opponent_count"]=visible_opponents;
      row["callback_before_log_us"]=micros(std::chrono::steady_clock::now()-received);
      log_.push(row.dump());
    }catch(const std::exception&){RCLCPP_ERROR(get_logger(),"Perception callback failed; frame dropped");}
    catch(...){RCLCPP_ERROR(get_logger(),"Unknown callback failure; frame dropped");}
  }
  std::string seat_,team_,mode_,policy_,dispatch_;rmtc::BotRuntime runtime_;AsyncLog log_;
  const std::string run_id_=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std_msgs::msg::String::ConstSharedPtr latest_;rmtc::MonoTime latest_received_{};
  std::uint64_t overwritten_{};rclcpp::TimerBase::SharedPtr dispatch_timer_;
  rclcpp::TimerBase::SharedPtr health_timer_;
  rmtc::MonoTime last_message_at_{std::chrono::steady_clock::now()},last_advanced_at_{last_message_at_},last_health_log_{last_message_at_};
  rmtc::Tick health_tick_{-1};std::string health_session_;bool terminal_observed_{};
  rmtc::MonoTime last_endpoint_rebuild_at_{last_message_at_};std::uint64_t endpoint_rebuilds_{};
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_,registration_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};
}
int main(int argc,char** argv){
  // This branch must precede ROS initialization and construction of any worker.
  // It shares the production parser but never starts DDS, HTTP or a log writer.
  if(argc==2&&std::string(argv[1])=="--check-config"){
    try{
      const auto mode=env("RMTC_RUN_MODE","collect");
      const auto dispatch=env("RMTC_RESEARCH_DISPATCH","immediate");
      if((mode!="collect"&&mode!="wait")||(dispatch!="immediate"&&dispatch!="mailbox"))
        throw std::invalid_argument("Invalid startup mode");
      const auto c=config(mode=="wait",env("RMTC_RESEARCH_POLICY","greedy"));
      const bool ai=rmtc::valid_llm_config(c.llm);
      const bool ai_required=c.core.stage==rmtc::StageMode::Auto||c.core.stage==rmtc::StageMode::Battle;
      const bool ready=!ai_required||ai;
      std::cout<<Json{{"configuration_valid",true},{"stage",rmtc::stage_name(c.core.stage)},
        {"decision_type",rmtc::decision_type_name(c.decision_type)},{"ai_configured",ai},
        {"stage2_ready",ai},{"ready_for_selected_stage",ready},{"ai_required",ai_required},
        {"ros_initialized",false},{"network_requests",0}}.dump()<<'\n';
      return ready?0:3;
    }catch(...){
      std::cout<<Json{{"configuration_valid",false},{"error","invalid_startup_configuration"},
        {"ros_initialized",false},{"network_requests",0}}.dump()<<'\n';
      return 2;
    }
  }
  try{
    // Fail before ROS startup instead of silently playing stage two without
    // qualification. Explicit stage one remains usable without any AI account.
    const auto startup=config(env("RMTC_RUN_MODE","collect")=="wait",env("RMTC_RESEARCH_POLICY","greedy"));
    if((startup.core.stage==rmtc::StageMode::Auto||startup.core.stage==rmtc::StageMode::Battle)&&!rmtc::valid_llm_config(startup.llm)){
      std::cerr<<"AI connection is required for stage auto/2; run --check-config with announced settings\n";
      return 3;
    }
    rclcpp::init(argc,argv);
    auto node=std::make_shared<BotNode>();
    rclcpp::executors::SingleThreadedExecutor executor;executor.add_node(node);executor.spin();
    executor.remove_node(node);node.reset();rclcpp::shutdown();return 0;
  }catch(const std::exception& e){std::cerr<<"Bot startup failed: "<<e.what()<<'\n';}
  catch(...){std::cerr<<"Bot startup failed\n";}
  if(rclcpp::ok())rclcpp::shutdown();
  return 1;
}
