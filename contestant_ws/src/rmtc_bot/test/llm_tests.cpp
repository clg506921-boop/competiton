#include "rmtc_bot/llm.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
using namespace rmtc;
using Json=nlohmann::json;
namespace {
struct Fake:ModelTransport {
  std::function<HttpReply(const LlmJob&,const CancelCheck&)> call;
  explicit Fake(decltype(call) f):call(std::move(f)){}
  HttpReply send(const LlmConfig&,const LlmJob& job,const CancelCheck& cancel)override{return call(job,cancel);}
};
LlmConfig config(){return {"http://127.0.0.1:8080/v1/chat/completions","test-model","test-token-not-real",256,false};}
LlmJob job(std::string source="e1",std::string session="s1",std::uint64_t generation=1){
  return {session,source,generation,false,Json::array({{{"role","user"},{"content","test"}}}),0};
}
bool until(const std::function<bool()>& predicate,int timeout_ms=1500){
  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
  do{if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<end);
  return predicate();
}
}
TEST(LlmConfig,MissingLimitBlocksAndDefaultModelNeedsAuthorization){
  auto c=config();EXPECT_TRUE(valid_llm_config(c));c.max_tokens=0;EXPECT_FALSE(valid_llm_config(c));
  c=config();c.model.clear();EXPECT_FALSE(valid_llm_config(c));c.official_default_model_authorized=true;EXPECT_TRUE(valid_llm_config(c));
  c=config();c.token="bad\r\nheader";EXPECT_FALSE(valid_llm_config(c));
  c=config();c.url="http://localhost:8080/admin";EXPECT_FALSE(valid_llm_config(c));
}
TEST(LlmConfig,GatewayRejectsMalformedAuthorityBeforeAnyRequest){
  for(const auto* url:{"http://:8080/v1/chat/completions","http:///v1/chat/completions",
      "http://localhost:70000/v1/chat/completions","http://localhost:0/v1/chat/completions",
      "http://user:secret@localhost:8080/v1/chat/completions","http://@localhost/v1/chat/completions",
      "http://local host/v1/chat/completions","http://localhost/v1/chat/completions?key=secret",
      "https://localhost/v1/chat/completions#fragment","file:///v1/chat/completions"}){
    auto c=config();c.url=url;EXPECT_FALSE(valid_llm_config(c));
  }
  for(const auto* url:{"https://gateway.example/v1/chat/completions",
      "http://localhost:8080/v1/chat/completions","http://[::1]:8080/v1/chat/completions",
      "https://gateway.example/proxy/v1/chat/completions"}){
    auto c=config();c.url=url;EXPECT_TRUE(valid_llm_config(c));
  }
}
TEST(LlmScheduler,SuccessDoesNotBlockSubmitOrRetryBadContent){
  std::atomic<int> calls{0};
  auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){++calls;return HttpReply{200,"missing_model_content","",true,false,false};});
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);
  EXPECT_EQ(s.submit(job()),SubmitResult::Accepted);EXPECT_EQ(s.submit(job()),SubmitResult::Duplicate);
  std::vector<LlmResult> results;
  ASSERT_TRUE(until([&]{results=s.take_results();return !results.empty();}));
  EXPECT_TRUE(results[0].reply.forwarded_success);EXPECT_EQ(results[0].job.attempt,1);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));EXPECT_EQ(calls,1);
}
TEST(LlmScheduler,AuthQuotaAndAuditErrorsDoNotRetry){
  for(auto reply:{HttpReply{401,"invalid_token",{},false,false,false},HttpReply{429,"quota_exceeded",{},false,false,false},HttpReply{503,"audit_unavailable",{},false,false,false}}){
    std::atomic<int> calls{0};
    auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){++calls;return reply;});
    LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);ASSERT_EQ(s.submit(job()),SubmitResult::Accepted);
    ASSERT_TRUE(until([&]{return !s.take_results().empty();}));
    if(reply.status!=503){EXPECT_EQ(s.submit(job("other")),SubmitResult::Stopped);}
    std::this_thread::sleep_for(std::chrono::milliseconds(20));EXPECT_EQ(calls,1);
  }
}
TEST(LlmScheduler,KnownUpstreamFailureRetriesAtMostTwice){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){++calls;return HttpReply{502,"upstream_unavailable",{},false,false,false};});
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);s.submit(job());
  ASSERT_TRUE(until([&]{return calls==2;},2500));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));EXPECT_EQ(calls,2);
}
TEST(LlmScheduler,OldGenerationResultsAreNotDelivered){
  std::atomic<int> calls{0};
  auto fake=std::make_unique<Fake>([&](const LlmJob& request,const CancelCheck& cancelled){
    ++calls;
    if(request.generation==1){while(!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(2));return HttpReply{0,"cancelled",{},false,false,true};}
    return HttpReply{200,{},"{}",true,false,false};
  });
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);s.submit(job());ASSERT_TRUE(until([&]{return calls==1;}));
  s.begin_session("s2",2);ASSERT_EQ(s.submit(job("e1","s2",2)),SubmitResult::Accepted);
  std::vector<LlmResult> results;ASSERT_TRUE(until([&]{results=s.take_results();return !results.empty();}));
  ASSERT_EQ(results.size(),1U);EXPECT_EQ(results[0].job.generation,2U);
}
TEST(LlmScheduler,QueueIsBoundedAndShutdownCancelsWorker){
  std::atomic<bool> started{false};
  auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck& cancelled){
    started=true;while(!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return HttpReply{0,"cancelled",{},false,false,true};
  });
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);s.submit(job());ASSERT_TRUE(until([&]{return started.load();}));
  EXPECT_EQ(s.submit(job("queued")),SubmitResult::Accepted);
  EXPECT_EQ(s.submit(job("overflow")),SubmitResult::Full);
  auto begin=std::chrono::steady_clock::now();s.shutdown();EXPECT_LT(std::chrono::steady_clock::now()-begin,std::chrono::seconds(1));
}
TEST(LlmRequest,OfficialFlashAlwaysDisablesThinkingAndUsesJsonIncludingFallback){
 auto c=config();c.model="deepseek-flash";auto j=job();
 for(bool fallback:{false,true})for(bool qualification:{false,true}){j.portable_fallback=fallback;j.qualification=qualification;auto request=make_chat_request(c,j);
  EXPECT_EQ(request.size(),6U);EXPECT_EQ(request["model"],"deepseek-flash");EXPECT_EQ(request["thinking"],(Json{{"type","disabled"}}));EXPECT_EQ(request["response_format"],(Json{{"type","json_object"}}));EXPECT_EQ(request["max_tokens"],256);EXPECT_EQ(request["messages"],j.messages);EXPECT_EQ(request["stream"],false);
 }
}
TEST(LlmRequest,ResearchProfilesRemainPortableAndLegacyProCanFallBack){
 auto c=config();auto j=job();auto request=make_chat_request(c,j);EXPECT_EQ(request.size(),4U);EXPECT_FALSE(request.contains("thinking"));EXPECT_FALSE(request.contains("response_format"));
 c.model="deepseek-v4-pro";EXPECT_EQ(make_chat_request(c,j)["thinking"]["type"],"disabled");j.portable_fallback=true;EXPECT_FALSE(make_chat_request(c,j).contains("thinking"));
 c.model="deepseek-flash-future";EXPECT_FALSE(make_chat_request(c,j).contains("thinking"));
}
TEST(LlmScheduler,NewerPendingArrivalReplacesOnlyUnsentEventAndOlderArrivalCannotDisplaceIt){
  std::atomic<bool> started{false},release{false};std::mutex lock;std::vector<std::string> sources;
  auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck& cancelled){
    {std::lock_guard<std::mutex> guard(lock);sources.push_back(j.source_id);}
    if(j.source_id=="active"){started=true;while(!release&&!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return HttpReply{200,{},"{}",true,false,false};
  });
  auto event=[](const char* id,Tick t){auto j=job(id);j.event_context=EventContext{{24,24},t,{id},{}};return j;};
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);
  ASSERT_EQ(s.submit(event("active",1)),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return started.load();}));
  EXPECT_EQ(s.submit(event("older",10)),SubmitResult::Accepted);
  EXPECT_EQ(s.submit(event("fresh",20)),SubmitResult::Accepted);
  EXPECT_EQ(s.submit(event("not_fresher",15)),SubmitResult::Full);
  EXPECT_EQ(s.submit(event("same_arrival",20)),SubmitResult::Full);
  EXPECT_EQ(s.usage().superseded_pending,1U);
  release=true;ASSERT_TRUE(until([&]{return s.usage().successful==2;}));
  {std::lock_guard<std::mutex> guard(lock);EXPECT_EQ(sources,(std::vector<std::string>{"active","fresh"}));}
  EXPECT_EQ(s.submit(event("older",10)),SubmitResult::Duplicate);
}
TEST(LlmScheduler,LegacyProRejectedOptionalExtensionFallsBackOnceAndPersistsForSameGateway){
  std::atomic<int> calls{0};std::atomic<bool> valid{true};auto c=config();c.model="deepseek-v4-pro";
  auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck&){
    int n=++calls;
    if(n==1){if(j.portable_fallback)valid=false;return HttpReply{400,"invalid_request",{},false,false,false};}
    if(!j.portable_fallback)valid=false;
    return HttpReply{200,{},"{}",true,false,false};
  });
  LlmScheduler s(c,std::move(fake));s.begin_session("s1",1);s.submit(job());
  ASSERT_TRUE(until([&]{return s.usage().successful==1;},2500));EXPECT_EQ(calls,2);EXPECT_TRUE(valid.load());
  s.begin_session("s2",2);s.submit(job("new","s2",2));
  ASSERT_TRUE(until([&]{return s.usage().successful==2;}));EXPECT_EQ(calls,3);EXPECT_TRUE(valid.load());
}
TEST(LlmScheduler,UncertainSlotCooldownSurvivesSameTokenSessionChange){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){++calls;return HttpReply{0,"client_timeout",{},false,true,false};});
  LlmScheduler s(config(),std::move(fake));s.begin_session("s1",1);s.submit(job());
  ASSERT_TRUE(until([&]{return !s.take_results().empty();}));s.begin_session("s2",2);
  ASSERT_EQ(s.submit(job("new","s2",2)),SubmitResult::Accepted);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));EXPECT_EQ(calls,1);
}

TEST(LlmScheduler,ExplicitTransient503RecoversQualificationWithoutUnboundedResubmission){
  for(const auto* code:{"upstream_unavailable","upstream_timeout"}){
    std::atomic<int> calls{0};
    auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){
      if(++calls==1)return HttpReply{503,code,{},false,false,false};
      return HttpReply{200,{},"Mission summary",true,false,false};
    });
    LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);
    auto request=job("qualification");request.qualification=true;
    ASSERT_EQ(scheduler.submit(request),SubmitResult::Accepted);
    ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;},3000));
    EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().attempts,2U);
    EXPECT_EQ(scheduler.usage().uncertain_reserved,0U);
    EXPECT_EQ(scheduler.submit(request),SubmitResult::Duplicate);
  }
}
TEST(LlmScheduler,PersistentExplicit503StillStopsAfterTwoAttempts){
  std::atomic<int> calls{0};
  auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){
    ++calls;return HttpReply{503,"upstream_unavailable",{},false,false,false};
  });
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);
  ASSERT_EQ(scheduler.submit(job()),SubmitResult::Accepted);
  ASSERT_TRUE(until([&]{return calls==2;},2500));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().successful,0U);
  EXPECT_EQ(scheduler.submit(job()),SubmitResult::Duplicate);
}

TEST(LlmHttp,TypedRateLimitWithoutCodeUsesStructuredTemporaryClassification){
  auto reply=decode_chat_completion(429,R"({"error":{"type":"rate_limit_error","message":"opaque untrusted body"}})");
  EXPECT_EQ(reply.error_code,"rate_limit");EXPECT_FALSE(reply.forwarded_success);
}
TEST(LlmHttp,ExplicitQuotaAndUnknownRateLimitsAreNotOverriddenByType){
  EXPECT_EQ(decode_chat_completion(429,R"({"error":{"type":"rate_limit_error","code":"quota_exceeded"}})").error_code,"quota_exceeded");
  EXPECT_EQ(decode_chat_completion(429,R"({"error":{"type":"quota_error"}})").error_code,"");
  EXPECT_EQ(decode_chat_completion(429,R"({"error":{"type":true}})").error_code,"");
  EXPECT_EQ(decode_chat_completion(403,R"({"error":{"type":"rate_limit_error"}})").error_code,"");
}
TEST(LlmScheduler,TypedGatewayRateLimitRecoversInsteadOfDisablingTheRestOfTheMatch){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){
    if(++calls==1)return decode_chat_completion(429,R"({"error":{"type":"rate_limit_error","message":"opaque"}})");
    return HttpReply{200,{},"Mission summary",true,false,false};
  });
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);
  auto request=job("qualification");request.qualification=true;
  ASSERT_EQ(scheduler.submit(request),SubmitResult::Accepted);
  ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;},3000));
  EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().attempts,2U);
  EXPECT_EQ(scheduler.submit(request),SubmitResult::Duplicate);
  EXPECT_EQ(scheduler.submit(job("later-notice")),SubmitResult::Accepted);
}
TEST(LlmScheduler,PersistentTypedRateLimitKeepsPerJobAttemptLimit){
  std::atomic<int> calls{0};auto fake=std::make_unique<Fake>([&](const LlmJob&,const CancelCheck&){
    ++calls;return decode_chat_completion(429,R"({"error":{"type":"rate_limit_error"}})");
  });
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);
  ASSERT_EQ(scheduler.submit(job()),SubmitResult::Accepted);
  ASSERT_TRUE(until([&]{return calls==2;},2500));
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().successful,0U);
  EXPECT_EQ(scheduler.submit(job()),SubmitResult::Duplicate);
}

TEST(LlmScheduler,OfficialFlashBadRequestNeverDowngradesOrAutoRetries){
 std::atomic<int> calls{0};auto c=config();c.model="deepseek-flash";auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck&){++calls;EXPECT_FALSE(j.portable_fallback);auto request=make_chat_request(c,j);EXPECT_EQ(request["thinking"]["type"],"disabled");EXPECT_EQ(request["response_format"]["type"],"json_object");return HttpReply{400,"invalid_request",{},false,false,false};});
 LlmScheduler scheduler(c,std::move(fake));scheduler.begin_session("s1",1);scheduler.submit(job());ASSERT_TRUE(until([&]{return !scheduler.take_results().empty();}));std::this_thread::sleep_for(std::chrono::milliseconds(1100));EXPECT_EQ(calls,1);scheduler.begin_session("s2",2);scheduler.submit(job("fresh","s2",2));ASSERT_TRUE(until([&]{return calls.load()==2;}));EXPECT_EQ(scheduler.usage().successful,0U);
}
TEST(LlmScheduler,OfficialFlashTransientRetryRetainsRequiredFieldsAndBudget){
 std::atomic<int> calls{0};auto c=config();c.model="deepseek-flash";c.max_tokens=512;auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck&){auto request=make_chat_request(c,j);EXPECT_EQ(request["thinking"]["type"],"disabled");EXPECT_EQ(request["response_format"]["type"],"json_object");EXPECT_EQ(request["max_tokens"],512);if(++calls==1)return HttpReply{502,"upstream_error",{},false,false,false};return HttpReply{200,{},"{}",true,false,false};});
 LlmScheduler scheduler(c,std::move(fake));scheduler.begin_session("s1",1);scheduler.submit(job());ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;},2500));EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().attempts,2U);
}

TEST(LlmScheduler,DiscardedUpstreamRetryDoesNotDelayAlreadyQueuedFreshEvent){
 for(bool qualification:{true,false})for(int status:{502,503,504}){
  std::atomic<bool> active{false},release{false},fresh{false};std::atomic<int> calls{0};
  auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck& cancel){++calls;if(j.source_id=="old"){active=true;while(!release&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(1));return HttpReply{status,"upstream_unavailable",{},false,false,false};}fresh=true;return HttpReply{200,{},"{}",true,false,false};});
  LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);auto old=job("old");old.qualification=qualification;ASSERT_EQ(scheduler.submit(old),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return active.load();}));auto newer=job("fresh");newer.event_context=EventContext{{24,24},10,{"fresh"},{}};ASSERT_EQ(scheduler.submit(newer),SubmitResult::Accepted);EXPECT_TRUE(scheduler.event_work_pending());release=true;
  EXPECT_TRUE(until([&]{return fresh.load();},600))<<"status="<<status<<" qualification="<<qualification;
  ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;},2500));EXPECT_EQ(calls,2);EXPECT_EQ(scheduler.usage().attempts,2U);EXPECT_EQ(scheduler.usage().skipped_retries,1U);EXPECT_EQ(scheduler.usage().uncertain_reserved,0U);EXPECT_EQ(scheduler.submit(old),SubmitResult::Duplicate);
 }
}
TEST(LlmScheduler,QueuedFreshEventCannotBypassTokenRateLimitCooldown){
 std::atomic<bool> active{false},release{false},fresh{false};auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck& cancel){if(j.source_id=="old"){active=true;while(!release&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(1));return HttpReply{429,"rate_limit",{},false,false,false};}fresh=true;return HttpReply{200,{},"{}",true,false,false};});
 LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);ASSERT_EQ(scheduler.submit(job("old")),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return active.load();}));ASSERT_EQ(scheduler.submit(job("fresh")),SubmitResult::Accepted);release=true;std::this_thread::sleep_for(std::chrono::milliseconds(150));EXPECT_FALSE(fresh.load());ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;},2500));EXPECT_EQ(scheduler.usage().attempts,2U);EXPECT_EQ(scheduler.usage().skipped_retries,1U);
}
TEST(LlmScheduler,QueuedFreshEventCannotBypassUncertainInflightCooldown){
 std::atomic<bool> active{false},release{false},fresh{false};auto fake=std::make_unique<Fake>([&](const LlmJob& j,const CancelCheck& cancel){if(j.source_id=="old"){active=true;while(!release&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(1));return HttpReply{0,"client_timeout",{},false,true,false};}fresh=true;return HttpReply{200,{},"{}",true,false,false};});
 LlmScheduler scheduler(config(),std::move(fake));scheduler.begin_session("s1",1);ASSERT_EQ(scheduler.submit(job("old")),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return active.load();}));ASSERT_EQ(scheduler.submit(job("fresh")),SubmitResult::Accepted);release=true;ASSERT_TRUE(until([&]{return scheduler.usage().uncertain_reserved==1;}));std::this_thread::sleep_for(std::chrono::milliseconds(150));EXPECT_FALSE(fresh.load());EXPECT_EQ(scheduler.usage().attempts,1U);scheduler.shutdown();
}
