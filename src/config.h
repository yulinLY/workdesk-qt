#pragma once
#include <string>
#include <vector>

namespace wd {

// 一套完整的「AI 配置」：接口 + 性格。可以存多套，随时切换哪套生效。
struct Profile {
  std::string name = "默认";
  std::string provider;
  std::string baseUrl;
  std::string model;
  std::string apiKey;
  std::string systemPrompt;
  double temperature = 0.7;

  // 性格调试
  std::string persona = "rational";  // rational/warm/humor/strict/concise/mentor/custom
  std::string tone;                  // 语气补充
  int verbosity = 1;                 // 0 简洁 / 1 适中 / 2 详细
  bool useEmoji = false;
  bool useMarkdown = true;
  std::string addressAs;             // 称呼用户
  std::string extraPrompt;           // 额外要求
};

// 配置与 ImGui 版共用 %APPDATA%\WorkDesk\config.json：
// 顶层字段（provider / base_url / model / api_key / system_prompt / temperature）保持原名，
// 顶层始终等于「当前生效的那套」，所以切到 Qt 版或回退旧版都不用重填。
struct Config {
  std::string provider;
  std::string baseUrl;
  std::string model;
  std::string apiKey;
  std::string systemPrompt =
      "你是 WorkDesk 桌面工作台的内置助手。回答简洁、直接、可执行，优先给结论，必要时用列表。";
  double temperature = 0.7;

  // 顶层性格字段（同 Profile）
  std::string persona = "rational";
  std::string tone;
  int verbosity = 1;
  bool useEmoji = false;
  bool useMarkdown = true;
  std::string addressAs;
  std::string extraPrompt;

  // 多套配置
  std::vector<Profile> profiles;
  int activeProfile = 0;

  // 用户生日（YYYY-MM-DD，空串=没填）。只存在顶层，不跟 profile 走。
  // 用于生日当天的彩蛋，以及给 AI 的称呼上下文。
  std::string birthday;
  // 上一次弹过生日彩蛋的年份（0=从未），保证一年只弹一次
  int birthdayShownYear = 0;

  bool hasBirthday() const { return birthday.size() >= 10; }
  int birthdayMonth() const;  // 1-12，没填返回 0
  int birthdayDay() const;    // 1-31，没填返回 0

  bool hasApiKey() const { return !apiKey.empty(); }
  bool ok() const { return !baseUrl.empty() && !model.empty() && !apiKey.empty(); }
  std::string Endpoint() const;

  // 把系统提示词与性格设定拼成最终下发的 system 内容
  std::string BuildSystemPrompt() const;

  // 顶层 <-> profiles[activeProfile]
  void SyncTopFromActive();
  void SyncActiveFromTop();
  void EnsureProfiles();  // profiles 为空时用顶层字段造一份

  static std::wstring Path();
  void Load();
  bool Save();
};

// 性格预设：key / 中文名 / 提示词片段
struct PersonaPreset {
  const char* key;
  const char* label;
  const char* hint;
};
const PersonaPreset* PersonaPresets(int& count);

}  // namespace wd
