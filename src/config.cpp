#include "config.h"

#include <windows.h>

#include <cstdlib>

#include <nlohmann/json.hpp>

#include "http.h"
#include "util.h"

using json = nlohmann::json;

namespace wd {
namespace {

const PersonaPreset kPersonas[] = {
    {"rational", "理智高效", "理智、高效。先给结论再给依据，不寒暄、不铺垫、不复述问题。"},
    {"warm", "温和耐心", "温和、有耐心。像同事聊天那样把话讲清楚，照顾对方感受，但不说空话。"},
    {"humor", "幽默轻松", "轻松、有点幽默感。可以适度玩笑，但不轻浮，不牺牲准确性。"},
    {"strict", "严谨学术", "严谨。给出依据、前提与边界条件，区分事实与推测，不确定就直说不确定。"},
    {"concise", "极简直给", "极简。一句话能说完就不说两句，不解释显而易见的东西。"},
    {"mentor", "导师带教", "导师式。先讲原理再给做法，必要时反问澄清，帮对方建立判断力。"},
    {"custom", "自定义", "按用户写的语气与要求来，不要套固定人设。"},
};
const int kPersonaCount = (int)(sizeof(kPersonas) / sizeof(kPersonas[0]));

const char* kDefaultSystemPrompt =
    "你是 WorkDesk 桌面工作台的内置助手。回答简洁、直接、可执行，优先给结论，必要时用列表。";

const char* PersonaHint(const std::string& key) {
  for (int i = 0; i < kPersonaCount; ++i)
    if (key == kPersonas[i].key) return kPersonas[i].hint;
  return kPersonas[0].hint;
}

void ReadStr(const json& j, const char* key, std::string& dst) {
  auto it = j.find(key);
  if (it != j.end() && it->is_string()) dst = it->get<std::string>();
}

void ReadBool(const json& j, const char* key, bool& dst) {
  auto it = j.find(key);
  if (it != j.end() && it->is_boolean()) dst = it->get<bool>();
}

void ReadInt(const json& j, const char* key, int& dst) {
  auto it = j.find(key);
  if (it != j.end() && it->is_number_integer()) dst = it->get<int>();
}

void ReadProfile(const json& j, Profile& p) {
  ReadStr(j, "name", p.name);
  ReadStr(j, "provider", p.provider);
  ReadStr(j, "base_url", p.baseUrl);
  ReadStr(j, "model", p.model);
  ReadStr(j, "api_key", p.apiKey);
  ReadStr(j, "system_prompt", p.systemPrompt);
  ReadStr(j, "persona", p.persona);
  ReadStr(j, "tone", p.tone);
  ReadStr(j, "address_as", p.addressAs);
  ReadStr(j, "extra_prompt", p.extraPrompt);
  ReadInt(j, "verbosity", p.verbosity);
  ReadBool(j, "use_emoji", p.useEmoji);
  ReadBool(j, "use_markdown", p.useMarkdown);
  auto it = j.find("temperature");
  if (it != j.end() && it->is_number()) p.temperature = it->get<double>();

  if (p.systemPrompt.empty()) p.systemPrompt = kDefaultSystemPrompt;
  if (p.persona.empty()) p.persona = "rational";
  if (p.verbosity < 0 || p.verbosity > 2) p.verbosity = 1;
  if (!(p.temperature > 0.0)) p.temperature = 0.7;
  if (p.name.empty()) p.name = p.model.empty() ? std::string("默认") : p.model;
}

void WriteProfile(json& j, const Profile& p) {
  j["name"] = p.name;
  j["provider"] = p.provider;
  j["base_url"] = p.baseUrl;
  j["model"] = p.model;
  j["api_key"] = p.apiKey;
  j["system_prompt"] = p.systemPrompt;
  j["persona"] = p.persona;
  j["tone"] = p.tone;
  j["verbosity"] = p.verbosity;
  j["use_emoji"] = p.useEmoji;
  j["use_markdown"] = p.useMarkdown;
  j["address_as"] = p.addressAs;
  j["extra_prompt"] = p.extraPrompt;
  j["temperature"] = p.temperature;
}

}  // namespace

const PersonaPreset* PersonaPresets(int& count) {
  count = kPersonaCount;
  return kPersonas;
}

std::string Config::Endpoint() const { return NormalizeEndpoint(baseUrl); }

// birthday 存成 "YYYY-MM-DD"；解析失败一律按"没填"处理，不让脏数据影响彩蛋判断
int Config::birthdayMonth() const {
  if (birthday.size() < 10) return 0;
  const int m = std::atoi(birthday.substr(5, 2).c_str());
  return (m >= 1 && m <= 12) ? m : 0;
}

int Config::birthdayDay() const {
  if (birthday.size() < 10) return 0;
  const int d = std::atoi(birthday.substr(8, 2).c_str());
  return (d >= 1 && d <= 31) ? d : 0;
}

std::wstring Config::Path() {
  const std::wstring inAppData = AppDataDir() + L"\\config.json";
  const std::wstring inExe = Utf8ToWide(ExeDir()) + L"\\config.json";
  if (GetFileAttributesW(inAppData.c_str()) != INVALID_FILE_ATTRIBUTES) return inAppData;
  if (GetFileAttributesW(inExe.c_str()) != INVALID_FILE_ATTRIBUTES) return inExe;
  return inAppData;
}

void Config::EnsureProfiles() {
  if (!profiles.empty()) return;
  Profile p;
  p.name = std::string("默认");
  p.provider = provider;
  p.baseUrl = baseUrl;
  p.model = model;
  p.apiKey = apiKey;
  p.systemPrompt = systemPrompt;
  p.temperature = temperature;
  p.persona = persona;
  p.tone = tone;
  p.verbosity = verbosity;
  p.useEmoji = useEmoji;
  p.useMarkdown = useMarkdown;
  p.addressAs = addressAs;
  p.extraPrompt = extraPrompt;
  profiles.push_back(p);
  activeProfile = 0;
}

void Config::SyncTopFromActive() {
  if (activeProfile < 0 || activeProfile >= (int)profiles.size()) return;
  const Profile& p = profiles[(size_t)activeProfile];
  provider = p.provider;
  baseUrl = p.baseUrl;
  model = p.model;
  apiKey = p.apiKey;
  systemPrompt = p.systemPrompt;
  temperature = p.temperature;
  persona = p.persona;
  tone = p.tone;
  verbosity = p.verbosity;
  useEmoji = p.useEmoji;
  useMarkdown = p.useMarkdown;
  addressAs = p.addressAs;
  extraPrompt = p.extraPrompt;
}

void Config::SyncActiveFromTop() {
  if (activeProfile < 0 || activeProfile >= (int)profiles.size()) return;
  Profile& p = profiles[(size_t)activeProfile];
  p.provider = provider;
  p.baseUrl = baseUrl;
  p.model = model;
  p.apiKey = apiKey;
  p.systemPrompt = systemPrompt;
  p.temperature = temperature;
  p.persona = persona;
  p.tone = tone;
  p.verbosity = verbosity;
  p.useEmoji = useEmoji;
  p.useMarkdown = useMarkdown;
  p.addressAs = addressAs;
  p.extraPrompt = extraPrompt;
}

std::string Config::BuildSystemPrompt() const {
  std::string base = Trim(systemPrompt);
  if (base.empty()) base = kDefaultSystemPrompt;

  std::string style;
  auto add = [&style](const std::string& line) {
    if (line.empty()) return;
    if (!style.empty()) style += "\n";
    style += "- " + line;
  };

  // 用户手填的语气/额外要求优先级最高，必须排在最前；否则排在前面的性格预设
  // （如「幽默轻松」）会把用户写的语气盖掉，表现为"语气补充没用"。
  const std::string t = Trim(tone);
  if (!t.empty())
    add("语气（用户指定，优先级最高，与下方性格基调冲突时一律以本句为准）：" + t);
  const std::string extra = Trim(extraPrompt);
  if (!extra.empty()) add("额外要求（必须遵守，优先级同上一行）：" + extra);
  add(std::string("性格基调：") + PersonaHint(persona));
  if (verbosity <= 0)
    add("回答长度：尽量短，一到三句说清楚。");
  else if (verbosity >= 2)
    add("回答长度：可以展开，把步骤和细节写全。");
  else
    add("回答长度：适中，说清关键点即可。");
  add(useMarkdown ? "排版：可以用 Markdown（列表、表格、代码块）。"
                  : "排版：用纯文本，不要 Markdown 符号。");
  add(useEmoji ? "表情：可以少量用 emoji。" : "表情：不要用 emoji。");
  const std::string who = Trim(addressAs);
  if (!who.empty()) add("称呼用户为「" + who + "」。");

  std::string out = base;
  if (!style.empty())
    out += "\n\n【对话风格】（自上而下优先级递减，前一条与后一条冲突时以前一条为准）\n" + style;
  return out;
}

void Config::Load() {
  std::string raw;
  if (ReadFileUtf8(Path(), raw) && !raw.empty()) {
    try {
      const json j = json::parse(raw);

      Profile top;
      ReadProfile(j, top);  // 顶层字段名与 profile 一致（除 name）
      provider = top.provider;
      baseUrl = top.baseUrl;
      model = top.model;
      apiKey = top.apiKey;
      systemPrompt = top.systemPrompt;
      temperature = top.temperature;
      persona = top.persona;
      tone = top.tone;
      verbosity = top.verbosity;
      useEmoji = top.useEmoji;
      useMarkdown = top.useMarkdown;
      addressAs = top.addressAs;
      extraPrompt = top.extraPrompt;

      profiles.clear();
      auto pit = j.find("profiles");
      if (pit != j.end() && pit->is_array()) {
        for (const auto& item : *pit) {
          if (!item.is_object()) continue;
          Profile p;
          ReadProfile(item, p);
          profiles.push_back(p);
        }
      }
      auto ait = j.find("active_profile");
      if (ait != j.end() && ait->is_number_integer()) activeProfile = ait->get<int>();

      ReadStr(j, "birthday", birthday);
      ReadInt(j, "birthday_shown_year", birthdayShownYear);
    } catch (...) {
    }
  }

  if (apiKey.empty()) {
    std::string k = GetEnvUtf8("WORKDESK_API_KEY");
    if (k.empty()) k = GetEnvUtf8("OPENAI_API_KEY");
    if (!k.empty()) apiKey = k;
  }
  if (baseUrl.empty()) baseUrl = GetEnvUtf8("WORKDESK_BASE_URL");
  if (model.empty()) model = GetEnvUtf8("WORKDESK_MODEL");
  if (Trim(systemPrompt).empty()) systemPrompt = kDefaultSystemPrompt;
  if (!(temperature > 0.0)) temperature = 0.7;

  EnsureProfiles();
  if (activeProfile < 0 || activeProfile >= (int)profiles.size()) activeProfile = 0;
  SyncTopFromActive();
}

bool Config::Save() {
  EnsureProfiles();
  if (activeProfile < 0 || activeProfile >= (int)profiles.size()) activeProfile = 0;
  SyncActiveFromTop();

  json j;
  j["provider"] = provider;
  j["base_url"] = baseUrl;
  j["model"] = model;
  j["api_key"] = apiKey;
  j["system_prompt"] = systemPrompt;
  j["temperature"] = temperature;
  j["persona"] = persona;
  j["tone"] = tone;
  j["verbosity"] = verbosity;
  j["use_emoji"] = useEmoji;
  j["use_markdown"] = useMarkdown;
  j["address_as"] = addressAs;
  j["extra_prompt"] = extraPrompt;
  j["active_profile"] = activeProfile;
  j["birthday"] = birthday;
  j["birthday_shown_year"] = birthdayShownYear;

  json arr = json::array();
  for (const auto& p : profiles) {
    json o;
    WriteProfile(o, p);
    arr.push_back(o);
  }
  j["profiles"] = arr;
  return WriteFileUtf8(Path(), j.dump(2));
}

}  // namespace wd
