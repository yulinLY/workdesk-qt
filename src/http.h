#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace wd {

// 通用 HTTP POST（WinHTTP，无第三方依赖）。
// sink 在后台线程被调用，用于流式接收响应体分片；非流式时传空即可。
using HttpChunkSink = std::function<void(const char*, size_t)>;

bool HttpPostJson(const std::string& url, const std::string& apiKey, const std::string& bodyJson,
                  const HttpChunkSink& sink, int& statusOut, std::string& errorOut,
                  const std::atomic<bool>* abortFlag = nullptr);

// 把用户填的 base_url 归一化成完整的 chat/completions 地址：
//   https://api.deepseek.com                        -> .../v1/chat/completions
//   https://api.deepseek.com/v1                     -> .../v1/chat/completions
//   https://open.bigmodel.cn/api/paas/v4            -> .../v4/chat/completions
//   https://x/v1/chat/completions                   -> 原样
std::string NormalizeEndpoint(const std::string& baseUrl);

/** OpenAI 兼容的流式对话（SSE）。后台线程收流，主线程每帧 Consume 取增量。 */
class ChatStream {
 public:
  ChatStream() = default;
  ~ChatStream();

  void Start(const std::string& endpoint, const std::string& apiKey, const std::string& bodyJson);
  void Stop();

  bool Running() const { return running_.load(); }

  /** 取走累积增量；finished 置位表示本轮结束（正常结束或出错，错误见 error） */
  bool Consume(std::string& delta, std::string& error, bool& finished);
  /** 立刻中断并等待线程收尾 */
  void Shutdown();

 private:
  void Worker(std::string endpoint, std::string apiKey, std::string body);

  std::thread worker_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  std::string pending_;
  std::string error_;
  bool finished_ = true;
};

/** 一次性的连通性测试（非流式），供设置面板的「测试连接」按钮使用。 */
class Probe {
 public:
  Probe() = default;
  ~Probe();

  void Start(const std::string& endpoint, const std::string& apiKey, const std::string& model);
  bool Running() const { return running_.load(); }
  /** 有新结果时返回 true（只返回一次） */
  bool Consume(std::string& status, bool& ok);
  void Shutdown();

 private:
  void Worker(std::string endpoint, std::string apiKey, std::string model);

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};

  mutable std::mutex mu_;
  std::string status_;
  bool ok_ = false;
  bool fresh_ = false;
};

}  // namespace wd
