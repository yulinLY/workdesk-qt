#include "markdown.h"

#include <algorithm>
#include <cctype>

namespace wd {
namespace {

std::string TrimCopy(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (unsigned char)s[a] <= ' ') ++a;
  while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
  return s.substr(a, b - a);
}

std::string LowerCopy(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool StartsWith(const std::string& s, const char* p) {
  const size_t n = std::char_traits<char>::length(p);
  return s.size() >= n && s.compare(0, n, p) == 0;
}

// 是否是表格分隔行，例如 |---|:--:|
bool IsTableSeparator(const std::string& line) {
  const std::string t = TrimCopy(line);
  if (t.find('|') == std::string::npos) return false;
  bool hasDash = false;
  for (char c : t) {
    if (c == '-') hasDash = true;
    else if (c != '|' && c != ':' && c != ' ') return false;
  }
  return hasDash;
}

std::vector<std::string> SplitRow(const std::string& line) {
  std::string t = TrimCopy(line);
  if (!t.empty() && t.front() == '|') t.erase(t.begin());
  if (!t.empty() && t.back() == '|') t.pop_back();
  std::vector<std::string> cells;
  std::string cur;
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
      cur += '|';
      ++i;
    } else if (t[i] == '|') {
      cells.push_back(TrimCopy(cur));
      cur.clear();
    } else {
      cur += t[i];
    }
  }
  cells.push_back(TrimCopy(cur));
  return cells;
}

bool LooksLikeRow(const std::string& line) {
  const std::string t = TrimCopy(line);
  if (t.empty()) return false;
  return t.find('|') != std::string::npos && t.front() == '|';
}

}  // namespace

std::string NormalizeLang(const std::string& tag) {
  std::string t = LowerCopy(TrimCopy(tag));
  while (!t.empty() && (t.front() == '{' || t.front() == '.' || t.front() == ':')) t.erase(t.begin());
  while (!t.empty() && (t.back() == '}' || t.back() == ';')) t.pop_back();
  const size_t sp = t.find_first_of(" \t,;");
  if (sp != std::string::npos) t = t.substr(0, sp);

  if (t == "c++" || t == "cxx" || t == "cc" || t == "hpp" || t == "cpp" || t == "h" ||
      t == "c")
    return "cpp";
  if (t == "c#" || t == "cs" || t == "csharp") return "csharp";
  if (t == "py" || t == "python3" || t == "python") return "python";
  if (t == "js" || t == "javascript" || t == "mjs" || t == "node") return "javascript";
  if (t == "ts" || t == "typescript") return "typescript";
  if (t == "sh" || t == "shell" || t == "bash" || t == "zsh" || t == "console") return "bash";
  if (t == "ps1" || t == "powershell" || t == "pwsh") return "powershell";
  if (t == "yml" || t == "yaml") return "yaml";
  if (t == "json5" || t == "json") return "json";
  if (t == "htm" || t == "html" || t == "xml" || t == "svg") return "html";
  if (t == "md" || t == "markdown") return "markdown";
  if (t == "rs" || t == "rust") return "rust";
  if (t == "go" || t == "golang") return "go";
  if (t == "java") return "java";
  if (t == "kt" || t == "kotlin") return "kotlin";
  if (t == "sql") return "sql";
  if (t == "bat" || t == "cmd" || t == "batch") return "batch";
  if (t == "cmake") return "cmake";
  if (t == "ini" || t == "toml" || t == "conf") return "ini";
  if (t == "diff" || t == "patch") return "diff";
  if (t == "mermaid") return "mermaid";
  if (t == "text" || t == "txt" || t == "plain" || t == "plaintext") return "text";
  return t;
}

std::string GuessLang(const std::string& code) {
  const std::string s = code;
  auto has = [&s](const char* p) { return s.find(p) != std::string::npos; };
  if (has("#include") || has("std::") || has("int main(")) return "cpp";
  if (has("def ") && has(":")) return "python";
  if (has("import ") && (has("from ") || has("print("))) return "python";
  if (has("function ") || has("const ") || has("=>")) return "javascript";
  if (has("{\"") && (has("\":") || has("\": "))) return "json";
  if (has("<?xml") || has("<html") || has("<div")) return "html";
  if (has("SELECT ") || has("select ") || has("FROM ")) return "sql";
  if (has("#!/bin/") || has("$ " ) || has("sudo ")) return "bash";
  return "text";
}

std::string LangDisplayName(const std::string& lang) {
  if (lang.empty() || lang == "text") return "纯文本";
  if (lang == "cpp") return "C/C++";
  if (lang == "csharp") return "C#";
  if (lang == "python") return "Python";
  if (lang == "javascript") return "JavaScript";
  if (lang == "typescript") return "TypeScript";
  if (lang == "bash") return "Shell";
  if (lang == "powershell") return "PowerShell";
  if (lang == "json") return "JSON";
  if (lang == "yaml") return "YAML";
  if (lang == "html") return "HTML/XML";
  if (lang == "markdown") return "Markdown";
  if (lang == "rust") return "Rust";
  if (lang == "go") return "Go";
  if (lang == "java") return "Java";
  if (lang == "kotlin") return "Kotlin";
  if (lang == "sql") return "SQL";
  if (lang == "batch") return "批处理";
  if (lang == "cmake") return "CMake";
  if (lang == "ini") return "配置";
  if (lang == "diff") return "Diff";
  if (lang == "write" || lang == "md-write") return "记事本写回";
  if (lang == "mermaid") return "Mermaid 图表";
  return lang;
}

std::vector<MdBlock> ParseBlocks(const std::string& md) {
  std::vector<MdBlock> out;
  std::string textAcc;
  size_t i = 0;
  const size_t n = md.size();

  auto flushText = [&]() {
    if (TrimCopy(textAcc).empty()) {
      textAcc.clear();
      return;
    }
    MdBlock b;
    b.kind = MdBlock::Text;
    b.text = textAcc;
    out.push_back(std::move(b));
    textAcc.clear();
  };

  // 逐行扫描
  size_t lineStart = 0;
  std::vector<std::string> lines;
  while (lineStart <= n) {
    size_t nl = md.find('\n', lineStart);
    if (nl == std::string::npos) {
      lines.push_back(md.substr(lineStart));
      break;
    }
    lines.push_back(md.substr(lineStart, nl - lineStart));
    lineStart = nl + 1;
    if (lineStart == n) {
      lines.push_back(std::string());
      break;
    }
  }

  for (i = 0; i < lines.size(); ++i) {
    std::string line = lines[i];
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = TrimCopy(line);

    if (StartsWith(trimmed, "```") || StartsWith(trimmed, "~~~")) {
      flushText();
      MdBlock b;
      b.kind = MdBlock::Code;
      b.lang = NormalizeLang(trimmed.substr(3));
      b.closed = false;
      std::string code;
      size_t j = i + 1;
      for (; j < lines.size(); ++j) {
        std::string l = lines[j];
        if (!l.empty() && l.back() == '\r') l.pop_back();
        const std::string t = TrimCopy(l);
        if (StartsWith(t, "```") || StartsWith(t, "~~~")) {
          b.closed = true;
          break;
        }
        if (!code.empty()) code += '\n';
        code += l;
      }
      if (b.lang.empty()) b.lang = GuessLang(code);
      b.text = code;
      out.push_back(std::move(b));
      i = j;  // 跳到围栏结束行
      continue;
    }

    // 表格：表头行 + 分隔行 + 数据行
    if (LooksLikeRow(line) && i + 1 < lines.size() && IsTableSeparator(lines[i + 1])) {
      flushText();
      MdBlock b;
      b.kind = MdBlock::Table;
      b.rows.push_back(SplitRow(line));
      size_t j = i + 2;
      for (; j < lines.size(); ++j) {
        std::string l = lines[j];
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (TrimCopy(l).empty() || !LooksLikeRow(l)) break;
        b.rows.push_back(SplitRow(l));
      }
      out.push_back(std::move(b));
      i = j - 1;
      continue;
    }

    textAcc += line;
    textAcc += '\n';
  }
  flushText();
  return out;
}

}  // namespace wd
