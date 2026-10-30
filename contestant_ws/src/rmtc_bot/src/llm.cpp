#include "rmtc_bot/llm.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rmtc {
namespace {
using Json=nlohmann::json;
constexpr std::size_t kBodyLimit=2097152;
bool safe_header(const std::string& s){return s.find_first_of("\r\n")==std::string::npos&&s.find('\0')==std::string::npos;}
bool valid_gateway_url(const std::string& url){
  // Parse locally with the same URL library used for transport. No DNS or HTTP.
  if(!safe_header(url)||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c==127;}))return false;
  if(url.rfind("http://",0)!=0&&url.rfind("https://",0)!=0)return false;
  if(url.find('?')!=std::string::npos||url.find('#')!=std::string::npos)return false;
  const auto authority_begin=url.find("://")+3;
  const auto authority_end=url.find('/',authority_begin);
  const auto authority=url.substr(authority_begin,authority_end-authority_begin);
  if(authority.empty()||authority.find('@')!=std::string::npos)return false;
  auto* parsed=curl_url();if(!parsed)return false;
  bool valid=curl_url_set(parsed,CURLUPART_URL,url.c_str(),0)==CURLUE_OK;
  char* value=nullptr;
  if(valid){
    valid=curl_url_get(parsed,CURLUPART_HOST,&value,0)==CURLUE_OK&&value&&*value;
    curl_free(value);value=nullptr;
  }
  if(valid){
    valid=curl_url_get(parsed,CURLUPART_PATH,&value,0)==CURLUE_OK&&value;
    if(valid){const std::string path=value;valid=path.size()>=20&&path.substr(path.size()-20)=="/v1/chat/completions";}
    curl_free(value);value=nullptr;
  }
  for(auto part:{CURLUPART_USER,CURLUPART_PASSWORD,CURLUPART_OPTIONS}){
    if(curl_url_get(parsed,part,&value,0)==CURLUE_OK)valid=false;
    curl_free(value);value=nullptr;
  }
  if(curl_url_get(parsed,CURLUPART_PORT,&value,0)==CURLUE_OK&&value&&std::string(value)=="0")valid=false;
  curl_free(value);curl_url_cleanup(parsed);return valid;
}
struct Buffer {std::string data;bool failed{};};
std::size_t write_callback(char* data,std::size_t size,std::size_t count,void* target) noexcept{
  try{
    auto& out=*static_cast<Buffer*>(target);
    if(count&&size>std::numeric_limits<std::size_t>::max()/count){out.failed=true;return 0;}
    auto bytes=size*count;
    if(bytes>kBodyLimit-out.data.size()){out.failed=true;return 0;}
    out.data.append(data,bytes);return bytes;
  }catch(...){return 0;}
}
int progress(void* check,curl_off_t,curl_off_t,curl_off_t,curl_off_t)noexcept{
  try{return (*static_cast<const CancelCheck*>(check))()?1:0;}catch(...){return 1;}
}
struct HeaderList {
  curl_slist* value{};
  ~HeaderList(){curl_slist_free_all(value);}
  void add(const std::string& s){
    auto next=curl_slist_append(value,s.c_str());if(!next)throw std::bad_alloc();value=next;
  }
};
bool retryable(const HttpReply& reply){
  return !reply.forwarded_success&&!reply.cancelled&&
    (reply.status==0||reply.status==502||reply.status==504||
     // Retry only explicitly identified upstream 503 failures. Audit failures
     // and unknown 503 bodies retain their existing non-retry behavior.
     (reply.status==503&&(reply.error_code=="upstream_unavailable"||reply.error_code=="upstream_timeout"))||
     (reply.status==429&&(reply.error_code=="rate_limit"||reply.error_code=="concurrency_limit")));
}
} // namespace
const char* completion_finish_name(CompletionFinish value) noexcept{
  switch(value){
    case CompletionFinish::Stop:return "stop";
    case CompletionFinish::Length:return "length";
    case CompletionFinish::ContentFilter:return "content_filter";
    case CompletionFinish::Other:return "other";
    default:return "unknown";
  }
}
const char* model_output_state_name(ModelOutputState value) noexcept{
  switch(value){
    case ModelOutputState::TransportError:return "transport_error";
    case ModelOutputState::EmptyContent:return "empty_content";
    case ModelOutputState::Truncated:return "truncated";
    case ModelOutputState::Filtered:return "filtered";
    case ModelOutputState::ParseRejected:return "parse_rejected";
    case ModelOutputState::EnvelopeRejected:return "envelope_rejected";
    case ModelOutputState::ItemsRejected:return "items_rejected";
    case ModelOutputState::PartiallyAccepted:return "partially_accepted";
    case ModelOutputState::Unresolved:return "unresolved";
    case ModelOutputState::NoEvents:return "no_events";
    case ModelOutputState::Validated:return "validated";
    case ModelOutputState::ValidationFailed:return "validation_failed";
    default:return "not_requested";
  }
}
HttpReply decode_chat_completion(long status,std::string_view body) noexcept{
  HttpReply reply;reply.status=status;reply.forwarded_success=status>=200&&status<300;
  reply.response_bytes=body.size();
  try{
    if(body.size()>kBodyLimit){reply.error_code="response_too_large";return reply;}
    auto result=Json::parse(body,nullptr,false);
    if(result.is_discarded()||!result.is_object()){reply.error_code="non_json_body";return reply;}
    if(result.contains("error")&&result["error"].is_object()&&result["error"].contains("code")&&result["error"]["code"].is_string())
      reply.error_code=result["error"]["code"].get<std::string>();
    // The practice/compatible gateway can identify temporary throttling using
    // error.type without an error.code. Read only this exact structured type;
    // explicit quota/auth/unknown codes keep their conservative stop behavior.
    if(status==429&&reply.error_code.empty()&&result.contains("error")&&result["error"].is_object()){
      const auto& error=result["error"];
      if(error.contains("type")&&error["type"].is_string()&&error["type"]=="rate_limit_error")
        reply.error_code="rate_limit";
    }
    if(reply.forwarded_success){
      if(result.contains("choices")&&result["choices"].is_array()&&!result["choices"].empty()){
        const auto& first=result["choices"][0];
        if(first.is_object()){
          if(first.contains("finish_reason")&&first["finish_reason"].is_string()){
            const auto& reason=first["finish_reason"].get_ref<const std::string&>();
            reply.finish=reason=="stop"?CompletionFinish::Stop:reason=="length"?CompletionFinish::Length:
              reason=="content_filter"?CompletionFinish::ContentFilter:CompletionFinish::Other;
          }
          if(first.contains("message")&&first["message"].is_object()&&first["message"].contains("content")&&first["message"]["content"].is_string())
            reply.content=first["message"]["content"].get<std::string>();
        }
      }
      if(reply.content.empty())reply.error_code="missing_model_content";
    }
  }catch(...){reply.content.clear();reply.error_code="response_decode_failed";}
  return reply;
}
void inspect_model_reply(LlmResult& result) noexcept{
  result.interpreted.reset();result.output_error.clear();result.diagnostics={};
  if(!result.job.event_context)return;
  auto& diagnostics=result.diagnostics;
  try{
    if(result.reply.cancelled||!result.reply.forwarded_success||
        (!result.reply.error_code.empty()&&result.reply.error_code!="missing_model_content")){
      diagnostics.state=ModelOutputState::TransportError;return;
    }
    if(result.reply.finish==CompletionFinish::Length||result.reply.finish==CompletionFinish::ContentFilter){
      diagnostics.state=result.reply.finish==CompletionFinish::Length?ModelOutputState::Truncated:ModelOutputState::Filtered;
      result.output_error="model_output_incomplete";return;
    }
    if(result.reply.content.empty()){
      diagnostics.state=ModelOutputState::EmptyContent;result.output_error="model_output_empty";return;
    }
    auto normalized=normalize_model_output(result.reply.content);
    if(const auto* error=std::get_if<Error>(&normalized)){
      diagnostics.state=ModelOutputState::ParseRejected;
      for(const char* code:{"model_output","model_output.empty","model_output.fence","model_output.depth",
          "model_output.objects","model_output.duplicate_key","model_output.json","model_output.exception"})
        if(error->field==code){diagnostics.parse_detail=code;break;}
      result.output_error="model_output_parse_rejected";return;
    }
    auto& model=std::get<ModelOutput>(normalized);
    diagnostics.ignored_fields=static_cast<std::uint32_t>(std::min<std::size_t>(model.dropped_fields.size(),65535));
    result.interpreted=std::make_shared<const InterpretedEvents>(validate_event_semantics(model,*result.job.event_context));
    diagnostics.accepted=static_cast<std::uint32_t>(result.interpreted->accepted.size());
    for(const auto& detail:result.interpreted->details){
      ++diagnostics.rejected[static_cast<std::size_t>(detail.issue)];
      if(diagnostics.first_rejection_field.empty())diagnostics.first_rejection_field=detail.field;
    }
    const bool envelope_bad=diagnostics.rejected[static_cast<std::size_t>(EventIssue::Envelope)]>0;
    if(!envelope_bad&&model.canonical.contains("unresolved_source_ids")&&model.canonical["unresolved_source_ids"].is_array())
      for(const auto& source:model.canonical["unresolved_source_ids"])
        if(source.is_string()&&result.job.event_context->source_ids.count(source.get<std::string>()))++diagnostics.unresolved;
    if(envelope_bad)diagnostics.state=ModelOutputState::EnvelopeRejected;
    else if(!result.interpreted->rejected.empty())diagnostics.state=diagnostics.accepted?ModelOutputState::PartiallyAccepted:ModelOutputState::ItemsRejected;
    else if(diagnostics.accepted)diagnostics.state=ModelOutputState::Validated;
    else diagnostics.state=diagnostics.unresolved?ModelOutputState::Unresolved:ModelOutputState::NoEvents;
  }catch(...){
    result.interpreted.reset();diagnostics={};diagnostics.state=ModelOutputState::ValidationFailed;
    result.output_error="model_output_validation_failed";
  }
}
bool valid_llm_config(const LlmConfig& c){
  return valid_gateway_url(c.url)&&safe_header(c.token)&&!c.token.empty()&&c.max_tokens>0&&
    (!c.model.empty()||c.official_default_model_authorized);
}
bool uses_fast_model_profile(const LlmConfig& c,const LlmJob& job) noexcept{
  return c.model=="deepseek-flash"||(!job.portable_fallback&&c.model=="deepseek-v4-pro");
}
Json make_chat_request(const LlmConfig& config,const LlmJob& job){
  Json request={{"messages",job.messages},{"max_tokens",config.max_tokens},{"stream",false}};
  if(!config.model.empty())request["model"]=config.model;
  if(uses_fast_model_profile(config,job))request["thinking"]={{"type","disabled"}};
  if(config.model=="deepseek-flash")request["response_format"]={{"type","json_object"}};
  return request;
}
CurlTransport::CurlTransport(){
  static const int initialized=[](){return curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK?1:0;}();
  if(!initialized)throw std::runtime_error("curl initialization failed");
  handle_=curl_easy_init();if(!handle_)throw std::runtime_error("curl handle allocation failed");
}
CurlTransport::~CurlTransport(){if(handle_)curl_easy_cleanup(static_cast<CURL*>(handle_));}
HttpReply CurlTransport::send(const LlmConfig& config,const LlmJob& job,const CancelCheck& cancelled){
  HttpReply reply;
  if(!valid_llm_config(config)||!job.messages.is_array()||job.messages.empty()){
    reply.error_code="invalid_local_configuration";return reply;
  }
  Json request=make_chat_request(config,job);
  std::string body=request.dump();if(body.size()>kBodyLimit){reply.error_code="local_payload_too_large";return reply;}
  auto* curl=static_cast<CURL*>(handle_);curl_easy_reset(curl);
  HeaderList headers;headers.add("Content-Type: application/json");headers.add("Expect:");
  headers.add("Authorization: Bearer "+config.token);
  Buffer response;response.data.reserve(8192);
  curl_easy_setopt(curl,CURLOPT_URL,config.url.c_str());
  curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers.value);
  curl_easy_setopt(curl,CURLOPT_POST,1L);curl_easy_setopt(curl,CURLOPT_POSTFIELDS,body.data());
  curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(body.size()));
  curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,1000L);
  curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,job.qualification?25000L:10000L);
  curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
  curl_easy_setopt(curl,CURLOPT_PROTOCOLS,static_cast<long>(CURLPROTO_HTTP|CURLPROTO_HTTPS));
  curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
  curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);
  curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&cancelled);
  auto code=curl_easy_perform(curl);curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&reply.status);
  reply.forwarded_success=reply.status>=200&&reply.status<300;
  long request_bytes=0;curl_easy_getinfo(curl,CURLINFO_REQUEST_SIZE,&request_bytes);
  if(code!=CURLE_OK){
    reply.response_bytes=response.data.size();
    reply.cancelled=cancelled();reply.uncertain_inflight=request_bytes>0&&reply.status==0;
    reply.error_code=response.failed?"response_too_large":code==CURLE_OPERATION_TIMEDOUT?"client_timeout":"transport_failure";
    return reply;
  }
  return decode_chat_completion(reply.status,response.data);
}
LlmScheduler::LlmScheduler(LlmConfig c,std::unique_ptr<ModelTransport> transport):
    config_(std::move(c)),transport_(std::move(transport)),enabled_(valid_llm_config(config_)&&transport_!=nullptr),
    worker_([this]{run();}){}
LlmScheduler::~LlmScheduler(){shutdown();}
void LlmScheduler::shutdown(){
  stopping_=true;cv_.notify_all();if(worker_.joinable())worker_.join();
}
void LlmScheduler::begin_session(std::string session,std::uint64_t generation){
  std::lock_guard<std::mutex> lock(mutex_);session_=std::move(session);generation_=generation;
  queue_.clear();results_.clear();submitted_.clear();cv_.notify_all();
}
SubmitResult LlmScheduler::submit(LlmJob job){
  std::lock_guard<std::mutex> lock(mutex_);
  if(!enabled_)return SubmitResult::Disabled;
  if(stopping_||requests_stopped_)return SubmitResult::Stopped;
  if(config_.call_quota&&usage_.successful+usage_.uncertain_reserved>=config_.call_quota)return SubmitResult::Stopped;
  if(job.generation!=generation_||job.session_id!=session_)return SubmitResult::SessionMismatch;
  if(submitted_.count(job.source_id))return SubmitResult::Duplicate;
  if(job.source_id.empty()||!job.messages.is_array()||job.messages.empty())return SubmitResult::Disabled;
  if(!queue_.empty()){
    const auto& queued=queue_.front();
    // One pending request. New public arrival time is the ONLY priority signal;
    // no event type, coordinates, numbers or words are inferred from raw text.
    if(job.qualification||queued.qualification||!job.event_context||!queued.event_context||
        job.event_context->observed_tick<=queued.event_context->observed_tick)return SubmitResult::Full;
    queue_.clear();++usage_.superseded_pending; // Unsent source stays deduplicated.
  }
  job.attempt=0;submitted_.insert(job.source_id);
  queue_.push_back(std::move(job));
  cv_.notify_all();return SubmitResult::Accepted;
}
std::vector<LlmResult> LlmScheduler::take_results(std::size_t max){
  std::lock_guard<std::mutex> lock(mutex_);std::vector<LlmResult> out;max=std::min<std::size_t>(max,16);
  while(!results_.empty()&&out.size()<max){out.push_back(std::move(results_.front()));results_.pop_front();}return out;
}
LlmUsage LlmScheduler::usage(){std::lock_guard<std::mutex> lock(mutex_);return usage_;}
bool LlmScheduler::event_work_pending(){
  std::lock_guard<std::mutex> lock(mutex_);
  // Include replies not yet consumed by control: completion may race with after_publish().
  return event_active_||std::any_of(queue_.begin(),queue_.end(),[](const auto& job){return !job.qualification;})||
    std::any_of(results_.begin(),results_.end(),[](const auto& result){return !result.job.qualification;});
}
void LlmScheduler::run()noexcept{
  try{loop();}
  catch(...){std::lock_guard<std::mutex> lock(mutex_);event_active_=false;requests_stopped_=true;queue_.clear();}
}
void LlmScheduler::loop(){
  auto available=std::chrono::steady_clock::now();
  std::uint64_t available_generation=0;
  std::deque<MonoTime> sent_times;
  while(!stopping_){
    LlmJob job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock,[&]{return stopping_||!queue_.empty();});if(stopping_)break;
      if(config_.call_quota&&usage_.successful+usage_.uncertain_reserved>=config_.call_quota){
        requests_stopped_=true;queue_.clear();continue;
      }
      // Config/token is immutable for this scheduler; changing session must NOT
      // forgive an uncertain in-flight request under the same token.
      if(available_generation!=generation_)available_generation=generation_;
      auto now=std::chrono::steady_clock::now();
      while(!sent_times.empty()&&now-sent_times.front()>=std::chrono::seconds(10))sent_times.pop_front();
      auto wake=available;
      if(sent_times.size()>=20)wake=std::max(wake,sent_times.front()+std::chrono::seconds(10));
      if(now<wake){cv_.wait_until(lock,wake,[&]{return stopping_||available_generation!=generation_;});continue;}
      job=std::move(queue_.front());queue_.pop_front();++job.attempt;
      job.portable_fallback=job.portable_fallback||optional_extensions_rejected_;
      event_active_=!job.qualification;
      ++usage_.attempts;
    }
    CancelCheck cancelled=[&]{return stopping_.load()||job.generation!=generation_.load();};
    if(cancelled()){std::lock_guard<std::mutex> lock(mutex_);event_active_=false;continue;}
    sent_times.push_back(std::chrono::steady_clock::now());
    const auto request_begin=std::chrono::steady_clock::now();HttpReply reply;
    try{reply=transport_->send(config_,job,cancelled);}
    catch(...){reply.error_code="transport_exception";reply.uncertain_inflight=true;}
    reply.request_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-request_begin).count();
    LlmResult result{job,std::move(reply),{}, {}, {}};
    // Validation is worker-only. Release raw model content before handing results to control.
    if(!cancelled())inspect_model_reply(result);
    std::string{}.swap(result.reply.content);
    auto completed=std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    event_active_=false;
    // Quota belongs to the immutable token, even when a session was cancelled.
    const auto& received=result.reply;
    if(received.forwarded_success)++usage_.successful;
    else if(received.uncertain_inflight)++usage_.uncertain_reserved;
    if(cancelled()){
      if(received.uncertain_inflight){available=completed+std::chrono::seconds(35);available_generation=generation_;}
      continue;
    }
    available_generation=job.generation;
    available=completed+(received.uncertain_inflight?std::chrono::seconds(35):std::chrono::seconds(0));
    if(received.status==401||received.status==403||(received.status==429&&
        received.error_code!="rate_limit"&&received.error_code!="concurrency_limit")){
      requests_stopped_=true;queue_.clear();
    }
    // Official flash parameters are required by the confirmed gateway contract.
    // A 400 is a request error, never permission to re-enable default thinking.
    const bool rejected_extension=config_.model!="deepseek-flash"&&received.status==400&&uses_fast_model_profile(config_,job);
    if(rejected_extension){optional_extensions_rejected_=true;job.portable_fallback=true;}
    const bool retry=(retryable(received)||rejected_extension)&&job.attempt<2&&!requests_stopped_;
    // Classify before moving result: error_code is moved into the result queue.
    const bool upstream_retry_only=!received.uncertain_inflight&&
      (received.status==502||received.status==504||
       (received.status==503&&(received.error_code=="upstream_unavailable"||received.error_code=="upstream_timeout")));
    if(results_.size()<64)results_.push_back(std::move(result));else ++dropped_results_;
    if(retry){
      // A completed upstream failure only backs off its own retry. If newer work
      // already displaced that retry, do not transfer the discarded job's delay.
      // Rate/concurrency limits, connection errors and uncertain in-flight work
      // still cool down the token exactly as before.
      if(queue_.empty()||!upstream_retry_only)
        available+=std::chrono::seconds(job.qualification?2:1);
      // A retry of an older announcement never displaces a fresher pending one.
      if(queue_.empty())queue_.push_back(std::move(job));else ++usage_.skipped_retries;
    }
  }
}
} // namespace rmtc
