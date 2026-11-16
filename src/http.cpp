#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include "util.h"

using nlohmann::json;

namespace wd {

namespace {

// RAII：WinHTTP 句柄
struct Handle {
  HINTERNET h = nullptr;
  Handle() = default;
  explicit Handle(HINTERNET v) : h(v) {}
  ~Handle() { Reset(); }
  void Reset() {
    if (h) {
      WinHttpCloseHandle(h);
      h = nullptr;
    }
  }
  operator HINTERNET() const { return h; }
  explicit operator bool() const { return h != nullptr; }
};

std::string JsonFieldOrEmpty(const json& obj, const char* k) {
  auto it = obj.find(k);
  if (it == obj.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

// 从完整 URL 取主机名，仅用于错误提示
std::string HostOf(const std::string& url) {
  size_t scheme = url.find("://");
  size_t start = (scheme == std::string::npos) ? 0 : scheme + 3;
  size_t end = url.find_first_of("/?#", start);
  if (end == std::string::npos) end = url.size();
  return url.substr(start, end - start);
}

// 把上游错误体压成一句人话
std::string BuildHttpError(const std::string& raw, int status) {
  std::string msg = raw;
  try {
    auto j = json::parse(raw);
    if (j.contains("error")) {
      const auto& e = j["error"];
      if (e.is_string())
        msg = e.get<std::string>();
      else if (e.is_object() && e.contains("message") && e["message"].is_string())
        msg = e["message"].get<std::string>();
    } else if (j.contains("message") && j["message"].is_string()) {
      msg = j["message"].get<std::string>();
    }
  } catch (...) {
  }
  if (msg.size() > 800) msg = msg.substr(0, 800) + "…";
  return "HTTP " + std::to_string(status) + "：" + (msg.empty() ? "请求被拒绝" : msg);
}

}  // namespace

std::string NormalizeEndpoint(const std::string& baseUrl) {
  std::string s = Trim(baseUrl);
  if (s.empty()) return {};
  while (!s.empty() && s.back() == '/') s.pop_back();
  if (Lower(s).find("/chat/completions") != std::string::npos) return s;

  size_t scheme = s.find("://");
  size_t pathStart = (scheme == std::string::npos) ? s.find('/') : s.find('/', scheme + 3);
  if (pathStart == std::string::npos) return s + "/v1/chat/completions";  // 只给了域名
  return s + "/chat/completions";                                        // 已带 /v1 之类的路径
}

bool HttpPostJson(const std::string& url, const std::string& apiKey, const std::string& bodyJson,
                  const HttpChunkSink& sink, int& statusOut, std::string& errorOut,
                  const std::atomic<bool>* abortFlag) {
  statusOut = 0;
  errorOut.clear();
  auto aborted = [abortFlag]() { return abortFlag && abortFlag->load(); };

  if (Trim(url).empty()) {
    errorOut = "接口地址为空";
    return false;
  }

  std::wstring wurl = Utf8ToWide(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {0};
  wchar_t path[4096] = {0};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 4095;
  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
    errorOut = "接口地址不合法：" + url;
    return false;
  }

  Handle session(WinHttpOpen(L"WorkDesk/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    errorOut = "WinHttpOpen 失败";
    return false;
  }
  // 流式响应可能很长，读超时放宽
  WinHttpSetTimeouts(session, 15000, 15000, 60000, 180000);

  Handle connect(WinHttpConnect(session, host, uc.nPort, 0));
  if (!connect) {
    errorOut = "无法连接 " + HostOf(url) + "（检查网络、代理或地址）";
    return false;
  }

  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  Handle request(WinHttpOpenRequest(connect, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
  if (!request) {
    errorOut = "创建请求失败";
    return false;
  }

  std::wstring headers = L"Content-Type: application/json\r\n";
  if (!apiKey.empty()) headers += L"Authorization: Bearer " + Utf8ToWide(apiKey) + L"\r\n";
  headers += L"Accept: text/event-stream\r\n";

  BOOL ok = WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L, (LPVOID)bodyJson.data(),
                               (DWORD)bodyJson.size(), (DWORD)bodyJson.size(), 0);
  if (ok && !aborted()) ok = WinHttpReceiveResponse(request, nullptr);
  if (!ok) {
    if (aborted()) {
      errorOut = "已停止";
    } else {
      errorOut = "请求发送失败（检查网络或代理设置）";
    }
    return false;
  }

  DWORD status = 0;
  DWORD len = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
  statusOut = (int)status;

  char buf[8192];
  DWORD read = 0;

  if (status != 200) {
    std::string detail;
    while (!aborted() && WinHttpReadData(request, buf, sizeof(buf) - 1, &read) && read > 0) {
      buf[read] = 0;
      detail += buf;
      if (detail.size() > 6000) break;
    }
    errorOut = BuildHttpError(detail, (int)status);
    return false;
  }

  while (!aborted()) {
    if (!WinHttpReadData(request, buf, sizeof(buf), &read)) break;
    if (read == 0) break;
    if (sink) sink(buf, (size_t)read);
  }
  return true;
}

// ---------------------------------------------------------------- ChatStream
ChatStream::~ChatStream() { Shutdown(); }

void ChatStream::Shutdown() {
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  running_.store(false);
}

void ChatStream::Stop() { stop_.store(true); }

void ChatStream::Start(const std::string& endpoint, const std::string& apiKey,
                       const std::string& bodyJson) {
  Shutdown();
  {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.clear();
    error_.clear();
    finished_ = false;
  }
  stop_.store(false);
  running_.store(true);
  worker_ = std::thread(&ChatStream::Worker, this, endpoint, apiKey, bodyJson);
}

bool ChatStream::Consume(std::string& delta, std::string& error, bool& finished) {
  std::lock_guard<std::mutex> lk(mu_);
  bool has = !pending_.empty() || !error_.empty() || finished_;
  if (!has) return false;
  delta.swap(pending_);
  pending_.clear();
  error = error_;
  finished = finished_ && !running_.load();
  // finished 是一次性事件：上报后立刻清掉，否则调用方每帧都会收到 finished=true，
  // 触发重复的收尾动作（例如每帧把整段对话重写一遍磁盘）。
  if (finished) finished_ = false;
  return true;
}

void ChatStream::Worker(std::string endpoint, std::string apiKey, std::string body) {
  std::string lineBuf;

  auto sink = [&](const char* data, size_t n) {
    lineBuf.append(data, n);
    size_t pos;
    while ((pos = lineBuf.find('\n')) != std::string::npos) {
      std::string line = lineBuf.substr(0, pos);
      lineBuf.erase(0, pos + 1);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.rfind("data:", 0) != 0) continue;
      std::string payload = line.substr(5);
      while (!payload.empty() && payload.front() == ' ') payload.erase(payload.begin());
      if (payload.empty() || payload == "[DONE]") continue;
      try {
        auto j = json::parse(payload);
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) continue;
        const auto& c0 = j["choices"][0];
        std::string delta;
        if (c0.contains("delta")) delta = JsonFieldOrEmpty(c0["delta"], "content");
        if (delta.empty() && c0.contains("message"))
          delta = JsonFieldOrEmpty(c0["message"], "content");
        if (!delta.empty()) {
          std::lock_guard<std::mutex> lk(mu_);
          pending_ += delta;
        }
      } catch (...) {
        // 半包 / 心跳行，跳过
      }
    }
  };

  int status = 0;
  std::string err;
  HttpPostJson(endpoint, apiKey, body, sink, status, err, &stop_);

  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!err.empty() && err != "已停止" && error_.empty()) error_ = err;
    finished_ = true;
  }
  running_.store(false);
}

// ---------------------------------------------------------------- Probe
Probe::~Probe() { Shutdown(); }

void Probe::Shutdown() {
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  running_.store(false);
}

void Probe::Start(const std::string& endpoint, const std::string& apiKey,
                  const std::string& model) {
  Shutdown();
  stop_.store(false);
  {
    std::lock_guard<std::mutex> lk(mu_);
    fresh_ = false;
    ok_ = false;
    status_.clear();
  }
  running_.store(true);
  worker_ = std::thread(&Probe::Worker, this, endpoint, apiKey, model);
}

bool Probe::Consume(std::string& status, bool& ok) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!fresh_) return false;
  fresh_ = false;
  status = status_;
  ok = ok_;
  return true;
}

void Probe::Worker(std::string endpoint, std::string apiKey, std::string model) {
  json body;
  body["model"] = model;
  body["stream"] = false;
  body["max_tokens"] = 8;
  body["messages"] = json::array({{{"role", "user"}, {"content", "ping"}}});

  std::string collected;
  int status = 0;
  std::string err;
  auto sink = [&](const char* data, size_t n) {
    if (collected.size() < 8000) collected.append(data, n);
  };
  bool sent = HttpPostJson(endpoint, apiKey, body.dump(), sink, status, err, &stop_);

  std::string statusText;
  bool ok = false;
  if (!sent) {
    statusText = err.empty() ? "连接失败" : err;
  } else {
    bool sane = false;
    std::string hint;
    try {
      auto j = json::parse(collected);
      sane = j.contains("choices");
      if (!sane && j.contains("error") && j["error"].is_object() &&
          j["error"].contains("message")) {
        hint = j["error"]["message"].get<std::string>();
      }
    } catch (...) {
    }
    if (sane) {
      ok = true;
      statusText = "连接成功 · HTTP 200 · 模型 " + model + " 已响应";
    } else if (!hint.empty()) {
      statusText = "上游返回异常：" + hint;
    } else {
      ok = true;
      statusText = "已连通（HTTP 200），但响应不是标准 chat/completions 格式";
    }
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    status_ = statusText;
    ok_ = ok;
    fresh_ = true;
  }
  running_.store(false);
}

}  // namespace wd
