#pragma once
#include <string>
#include <vector>

namespace wd {

// AI 回复里的"容器块"：普通 markdown 文本 / 代码块 / 表格块。
// ImGui 版是自绘这三个块，Qt 版按块各用一个原生控件，横向滚动与选中交给控件本身。
struct MdBlock {
  enum Kind { Text, Code, Table };
  Kind kind = Text;
  std::string text;              // Text: markdown 原文；Code: 代码正文
  std::string lang;              // Code: 规范化后的语言标识
  bool closed = true;            // Code: 围栏是否闭合（流式输出中途为 false）
  std::vector<std::vector<std::string>> rows;  // Table: 单元格
};

std::vector<MdBlock> ParseBlocks(const std::string& md);

// 语言标签规范化：C++ / cpp / cxx -> cpp，Python -> python ...
std::string NormalizeLang(const std::string& tag);
// 无语言标签时按内容猜测（够用即可，不追求准确）
std::string GuessLang(const std::string& code);
// 代码块标题栏显示用
std::string LangDisplayName(const std::string& lang);

}  // namespace wd
