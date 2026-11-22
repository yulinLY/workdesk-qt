#include "session.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include <nlohmann/json.hpp>

#include "util.h"

using json = nlohmann::json;

namespace wd {
namespace {

double NowUnix() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  return (double)(t - 116444736000000000ull) / 10000000.0;
}

// 按 UTF-8 码点截断，避免切坏多字节字符
std::string TruncateCp(const std::string& s, size_t maxCp) {
  size_t i = 0, cp = 0;
  while (i < s.size() && cp < maxCp) {
    const unsigned char c = (unsigned char)s[i];
    i += (c < 0x80) ? 1 : ((c >> 5) == 0x6 ? 2 : ((c >> 4) == 0xE ? 3 : 4));
    ++cp;
  }
  if (i >= s.size()) return s;
  return s.substr(0, i) + "…";
}

json MsgsToJson(const std::vector<ChatMessage>& msgs) {
  json arr = json::array();
  for (const auto& m : msgs) {
    json jm;
    jm["user"] = m.user;
    jm["text"] = m.text;
    if (!m.images.empty()) {
      json imgs = json::array();
      for (const auto& im : m.images) {
        json ji = {{"name", im.name}, {"b64", im.b64}};
        if (!im.path.empty()) ji["path"] = WideToUtf8(im.path);
        imgs.push_back(ji);
      }
      jm["images"] = imgs;
    }
    if (!m.files.empty()) {
      json files = json::array();
      for (const auto& f : m.files) {
        files.push_back({{"name", f.name},
                         {"path", WideToUtf8(f.path)},
                         {"size", (unsigned long long)f.size}});
      }
      jm["files"] = files;
    }
    arr.push_back(jm);
  }
  return arr;
}

void JsonToMsgs(const json& arr, std::vector<ChatMessage>& out) {
  if (!arr.is_array()) return;
  for (const auto& jm : arr) {
    if (!jm.is_object()) continue;
    ChatMessage m;
    m.user = jm.value("user", false);
    m.text = jm.value("text", std::string());
    if (jm.contains("images") && jm["images"].is_array()) {
      for (const auto& ji : jm["images"]) {
        ChatImage im;
        im.name = ji.value("name", std::string("图片"));
        im.b64 = ji.value("b64", std::string());
        const std::string p = ji.value("path", std::string());
        if (!p.empty()) im.path = Utf8ToWide(p);
        m.images.push_back(std::move(im));
      }
    }
    if (jm.contains("files") && jm["files"].is_array()) {
      for (const auto& jf : jm["files"]) {
        ChatFile f;
        f.name = jf.value("name", std::string("文件"));
        const std::string p = jf.value("path", std::string());
        if (!p.empty()) f.path = Utf8ToWide(p);
        f.size = (unsigned long long)jf.value("size", (unsigned long long)0);
        m.files.push_back(std::move(f));
      }
    }
    out.push_back(std::move(m));
  }
}

}  // namespace

std::wstring ChatDir() {
  static std::wstring dir;
  if (dir.empty()) {
    dir = AppDataDir() + L"\\chats";
    CreateDirectoryW(dir.c_str(), nullptr);
  }
  return dir;
}

std::string NewSessionId() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[32];
  sprintf_s(buf, "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond);
  return std::string(buf);
}

std::vector<SessionInfo> ScanSessions() {
  std::vector<SessionInfo> list;
  const std::wstring dir = ChatDir();
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return list;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    const std::wstring name = fd.cFileName;
    SessionInfo info;
    info.path = dir + L"\\" + name;
    info.id = WideToUtf8(name);
    if (info.id.size() > 5) info.id = info.id.substr(0, info.id.size() - 5);
    std::string raw;
    if (!ReadFileUtf8(info.path, raw)) continue;
    json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) continue;
    info.title = j.value("title", std::string("未命名对话"));
    info.updated = j.value("updated", 0.0);
    if (j.contains("messages") && j["messages"].is_array())
      info.msgCount = (int)j["messages"].size();
    if (info.msgCount <= 0) continue;  // 空壳不参与列表
    list.push_back(std::move(info));
  } while (FindNextFileW(h, &fd));
  FindClose(h);

  std::sort(list.begin(), list.end(),
            [](const SessionInfo& a, const SessionInfo& b) { return a.updated > b.updated; });
  return list;
}

bool LoadSession(const SessionInfo& info, std::vector<ChatMessage>& out) {
  std::string raw;
  if (!ReadFileUtf8(info.path, raw)) return false;
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  out.clear();
  if (j.contains("messages")) JsonToMsgs(j["messages"], out);
  return true;
}

bool SaveSession(SessionInfo& info, const std::vector<ChatMessage>& msgs) {
  if (msgs.empty()) return false;
  info.title = MakeSessionTitle(msgs);
  info.updated = NowUnix();
  info.msgCount = (int)msgs.size();
  if (info.id.empty()) info.id = NewSessionId();
  if (info.path.empty()) info.path = ChatDir() + L"\\" + Utf8ToWide(info.id) + L".json";

  json j;
  j["id"] = info.id;
  j["title"] = info.title;
  j["updated"] = info.updated;
  j["messages"] = MsgsToJson(msgs);
  return WriteFileUtf8(info.path, j.dump());
}

bool DeleteSessionFile(const SessionInfo& info) {
  if (info.path.empty()) return false;
  return DeleteFileW(info.path.c_str()) != 0;
}

std::string HumanSize(unsigned long long n) {
  char buf[64];
  if (n < 1024ull) sprintf_s(buf, "%llu B", n);
  else if (n < 1024ull * 1024ull) sprintf_s(buf, "%.1f KB", n / 1024.0);
  else if (n < 1024ull * 1024ull * 1024ull) sprintf_s(buf, "%.1f MB", n / (1024.0 * 1024.0));
  else sprintf_s(buf, "%.2f GB", n / (1024.0 * 1024.0 * 1024.0));
  return std::string(buf);
}

std::string MakeTitle(const std::vector<ChatMessage>& msgs) {
  for (const auto& m : msgs) {
    if (!m.user) continue;
    std::string t = Trim(m.text);
    if (t.empty()) {
      if (!m.images.empty())
        t = "[图片]";
      else if (!m.files.empty())
        t = "[文件] " + m.files[0].name;
      else
        continue;
    }
    for (auto& c : t)
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return TruncateCp(t, 16);
  }
  return "新对话";
}

std::string MakeSessionTitle(const std::vector<ChatMessage>& msgs) { return MakeTitle(msgs); }

std::string BuildRequestBody(const Config& cfg, const std::vector<ChatMessage>& msgs,
                             int upToIndex, const std::string& extraSystem) {
  json messages = json::array();

  // 系统提示词 + 性格设定拼装（见 Config::BuildSystemPrompt）
  std::string sys = cfg.BuildSystemPrompt();
  // 记事本互通：把当前打开文档随请求一起递给模型
  if (!extraSystem.empty()) sys += "\n\n" + extraSystem;
  messages.push_back({{"role", "system"}, {"content", sys}});

  // 只带最近 60 条，避免长会话 token 无限膨胀
  const int kMaxHistoryMsgs = 60;
  int first = 0;
  if (upToIndex + 1 > kMaxHistoryMsgs) first = upToIndex + 1 - kMaxHistoryMsgs;
  if (first < 0) first = 0;
  while (first < upToIndex && first < (int)msgs.size() && !msgs[first].user) ++first;

  for (int i = first; i <= upToIndex && i < (int)msgs.size(); ++i) {
    const auto& m = msgs[i];
    const char* role = m.user ? "user" : "assistant";

    std::string fileNote;
    if (m.user && !m.files.empty()) {
      for (const auto& f : m.files) {
        if (!fileNote.empty()) fileNote += "\n";
        fileNote += "【附件】" + f.name + "（" + HumanSize(f.size) + "）\n路径：" +
                    WideToUtf8(f.path);
        if (IsTextFilePath(f.path) && f.size <= 100ull * 1024ull * 1024ull) {
          std::string content, ferr;
          TextEncoding fenc = TextEncoding::Utf8;
          bool fcrlf = false;
          // 路径已经给出，这里只是把开头一段内联进来省一次读取；
          // 完整内容按上面的路径读，不受这段长度限制
          if (ReadTextFileAuto(f.path, content, fenc, fcrlf, ferr, 256ull * 1024ull)) {
            if (content.size() > 30000)
              content = content.substr(0, 30000) +
                        "\n…（下面还有更多内容，按上面的路径读取完整文件）";
            fileNote += "\n```\n" + content + "\n```";
          } else {
            fileNote += "\n（读取失败：" + ferr + "，请按上面的路径自行读取）";
          }
        } else if (!IsTextFilePath(f.path)) {
          fileNote +=
              "\n（非文本文件，正文未内联；需要内容请按上面的路径读取原文件）";
        } else {
          fileNote += "\n（文件超过 100MB，正文未内联；需要内容请按上面的路径读取原文件）";
        }
      }
    }
    std::string text = m.text;
    if (!fileNote.empty()) text = text.empty() ? fileNote : (text + "\n\n" + fileNote);

    if (m.user && !m.images.empty()) {
      // 图片同样要给出原始路径：base64 只是给模型看一眼，要原图/要处理就按路径取
      std::string imgNote;
      for (const auto& img : m.images) {
        if (!imgNote.empty()) imgNote += "\n";
        imgNote += "【图片】" + img.name;
        if (!img.path.empty())
          imgNote += "\n路径：" + WideToUtf8(img.path);
        if (img.w > 0 && img.h > 0)
          imgNote += "\n尺寸：" + std::to_string(img.w) + "x" + std::to_string(img.h);
      }
      std::string full = text;
      if (!imgNote.empty()) full = full.empty() ? imgNote : (full + "\n\n" + imgNote);
      json parts = json::array();
      if (!full.empty()) parts.push_back({{"type", "text"}, {"text", full}});
      for (const auto& img : m.images) {
        json item;
        item["type"] = "image_url";
        item["image_url"] = {{"url", "data:image/jpeg;base64," + img.b64}};
        parts.push_back(item);
      }
      messages.push_back({{"role", role}, {"content", parts}});
    } else {
      if (text.empty()) continue;
      messages.push_back({{"role", role}, {"content", text}});
    }
  }

  json body;
  body["model"] = cfg.model;
  body["stream"] = true;
  // 部分厂商（智谱 GLM）要求 temperature 最多两位小数
  body["temperature"] = std::round(cfg.temperature * 100.0) / 100.0;
  body["messages"] = messages;
  return body.dump();
}

bool ExportSessionJson(const std::wstring& path, const std::vector<ChatMessage>& msgs,
                       const std::string& title) {
  json j;
  j["id"] = NewSessionId();
  j["title"] = title.empty() ? MakeSessionTitle(msgs) : title;
  j["updated"] = NowUnix();
  j["messages"] = MsgsToJson(msgs);
  return WriteFileUtf8(path, j.dump(2));
}

bool ImportSessionJson(const std::wstring& path, SessionInfo& info,
                       std::vector<ChatMessage>& msgs) {
  std::string raw;
  if (!ReadFileUtf8(path, raw)) return false;
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  msgs.clear();
  if (j.contains("messages")) JsonToMsgs(j["messages"], msgs);
  if (msgs.empty()) return false;
  info.id = NewSessionId() + "-import";
  info.path = ChatDir() + L"\\" + Utf8ToWide(info.id) + L".json";
  info.title = MakeSessionTitle(msgs);
  info.msgCount = (int)msgs.size();
  SaveSession(info, msgs);
  return true;
}

}  // namespace wd
