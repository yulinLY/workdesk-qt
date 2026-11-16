#pragma once
#include <string>
#include <vector>

#include "config.h"

namespace wd {

struct ChatImage {
  std::string name;
  std::string b64;      // 纯 base64（不带 data: 前缀），发给模型 & 落盘
  std::wstring path;    // 原始文件路径（高清预览用）
  int w = 0, h = 0;
};

struct ChatFile {
  std::string name;
  std::wstring path;
  unsigned long long size = 0;
};

struct ChatMessage {
  bool user = false;
  std::string text;
  std::vector<ChatImage> images;
  std::vector<ChatFile> files;
};

struct SessionInfo {
  std::string id;
  std::string title;
  double updated = 0.0;
  std::wstring path;
  int msgCount = 0;
};

std::wstring ChatDir();
std::string NewSessionId();
std::vector<SessionInfo> ScanSessions();          // 按 updated 降序
bool LoadSession(const SessionInfo& info, std::vector<ChatMessage>& out);
bool SaveSession(SessionInfo& info, const std::vector<ChatMessage>& msgs);
bool DeleteSessionFile(const SessionInfo& info);

std::string MakeTitle(const std::vector<ChatMessage>& msgs);
std::string MakeSessionTitle(const std::vector<ChatMessage>& msgs);
std::string HumanSize(unsigned long long n);

// 组装 OpenAI 兼容请求体（含历史、图片 vision、文本附件内联）
std::string BuildRequestBody(const Config& cfg, const std::vector<ChatMessage>& msgs, int upToIndex);

// 导出 / 导入：格式与落盘会话完全一致
bool ExportSessionJson(const std::wstring& path, const std::vector<ChatMessage>& msgs,
                       const std::string& title);
bool ImportSessionJson(const std::wstring& path, SessionInfo& info,
                       std::vector<ChatMessage>& msgs);

}  // namespace wd
