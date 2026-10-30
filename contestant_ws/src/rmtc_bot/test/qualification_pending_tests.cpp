// Prepared after the third integrated scenario failed. Compiled, NOT EXECUTED in this phase.
#include "rmtc_bot/llm.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
using namespace rmtc;
namespace {
struct Held:ModelTransport {
  std::atomic<bool>& started;std::atomic<bool>& release;
  Held(std::atomic<bool>& s,std::atomic<bool>& r):started(s),release(r){}
  HttpReply send(const LlmConfig&,const LlmJob&,const CancelCheck& cancel)override{
    started=true;while(!release&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {200,{},"test result",true,false,false};
  }
};
bool until(const std::function<bool()>& p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<end){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  return p();
}
}
TEST(QualificationPending,EventRemainsPendingUntilItsResultHasBeenConsumed){
  std::atomic<bool> started{false},release{false};
  LlmConfig config{"http://127.0.0.1:8080/v1/chat/completions","fake-only","fixed-fake-token",128,false};
  LlmScheduler scheduler(config,std::make_unique<Held>(started,release));scheduler.begin_session("pending",1);
  LlmJob job;job.session_id="pending";job.generation=1;job.source_id="event";
  job.messages=nlohmann::json::array({{{"role","user"},{"content","opaque test"}}});
  ASSERT_EQ(scheduler.submit(job),SubmitResult::Accepted);ASSERT_TRUE(until([&]{return started.load();}));
  EXPECT_TRUE(scheduler.event_work_pending());release=true;
  ASSERT_TRUE(until([&]{return scheduler.usage().successful==1;}));EXPECT_TRUE(scheduler.event_work_pending());
  EXPECT_EQ(scheduler.take_results().size(),1U);EXPECT_FALSE(scheduler.event_work_pending());
}
