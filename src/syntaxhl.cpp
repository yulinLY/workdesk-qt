#include "syntaxhl.h"

#include <QColor>
#include <QStringList>

#include "theme.h"

namespace wd {
namespace {

// 代码着色两套：深色背景上用亮彩色，亮色背景上换成同色系但压暗、对比更高的版本，
// 否则浅底上原来的糖果色几乎看不清。
struct Palette {
  QColor keyword, type, string, number, comment, preproc, key;
  Palette() {
    if (theme::IsLightActive()) {
      keyword = QColor(0x8A, 0x31, 0xC9);  // 紫
      type = QColor(0x0E, 0x7C, 0x86);     // 青
      string = QColor(0x1A, 0x7F, 0x3C);   // 绿
      number = QColor(0xB5, 0x5D, 0x00);   // 橙
      comment = QColor(0x6B, 0x76, 0x85);  // 灰
      preproc = QColor(0xC0, 0x39, 0x39);  // 红
      key = QColor(0x1F, 0x5F, 0xD0);      // 蓝
    } else {
      keyword = QColor(0xC7, 0x8B, 0xF5);
      type = QColor(0x6F, 0xD1, 0xC4);
      string = QColor(0x9B, 0xD3, 0x8B);
      number = QColor(0xE8, 0xB1, 0x71);
      comment = QColor(0x76, 0x82, 0x8F);
      preproc = QColor(0xE0, 0x8B, 0x8B);
      key = QColor(0x7C, 0xB8, 0xFF);
    }
  }
};

QString Join(const char* const* words, size_t n) {
  QStringList list;
  for (size_t i = 0; i < n; ++i) list << QString::fromLatin1(words[i]);
  return list.join('|');
}

}  // namespace

CodeHighlighter::CodeHighlighter(QTextDocument* doc, const std::string& lang)
    : QSyntaxHighlighter(doc), lang_(lang) {
  BuildFor(lang);
}

void CodeHighlighter::Recolor() {
  rules_.clear();
  BuildFor(lang_);
  rehighlight();
}

void CodeHighlighter::AddRule(const QString& pattern, const QColor& color, bool bold) {
  Rule r;
  r.pattern = QRegularExpression(pattern);
  r.format.setForeground(color);
  if (bold) r.format.setFontWeight(QFont::DemiBold);
  rules_.push_back(r);
}

void CodeHighlighter::BuildFor(const std::string& lang) {
  const Palette p;
  commentFormat_.setForeground(p.comment);
  commentFormat_.setFontItalic(true);

  const QString strRule = QStringLiteral(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`[^`]*`)");
  const QString numRule = QStringLiteral(R"(\b(?:0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][-+]?\d+)?)\b)");

  if (lang == "cpp" || lang == "csharp" || lang == "java" || lang == "rust" || lang == "go" ||
      lang == "kotlin") {
    static const char* kw[] = {"alignas", "alignof", "asm", "auto",    "bool",     "break",
                               "case",    "catch",  "char", "class",   "const",    "constexpr",
                               "continue","default","delete","do",      "double",   "else",
                               "enum",    "explicit","export","extern",  "false",    "float",
                               "for",     "friend", "goto", "if",       "inline",   "int",
                               "long",    "mutable","namespace","new",   "noexcept", "nullptr",
                               "operator","private","protected","public","return",   "short",
                               "signed",  "sizeof", "static", "struct", "switch",   "template",
                               "this",    "throw",  "true", "try",       "typedef",  "typename",
                               "union",   "unsigned","using", "virtual", "void",     "volatile",
                               "while",   "let",    "mut",  "fn",        "impl",     "pub",
                               "match",   "trait",  "use",  "mod",       "func",     "defer",
                               "go",      "chan",   "interface","package","extends",  "final",
                               "var",     "val",    "object","override"};
    static const char* tp[] = {"size_t",  "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                               "int8_t",  "int16_t", "int32_t",  "int64_t",  "string",
                               "wstring", "vector",  "map",      "unordered_map", "shared_ptr",
                               "unique_ptr", "optional", "u32", "i32", "usize", "str"};
    AddRule(QStringLiteral("\\b(%1)\\b").arg(Join(kw, sizeof(kw) / sizeof(kw[0]))), p.keyword, true);
    AddRule(QStringLiteral("\\b(%1)\\b").arg(Join(tp, sizeof(tp) / sizeof(tp[0]))), p.type);
    AddRule(QStringLiteral(R"(^\s*#\s*\w+)"), p.preproc);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    commentStart_ = QRegularExpression(QStringLiteral(R"(/\*)"));
    commentEnd_ = QRegularExpression(QStringLiteral(R"(\*/)"));
    AddRule(QStringLiteral(R"(//[^\n]*)"), p.comment);
  } else if (lang == "python") {
    static const char* kw[] = {"and",   "as",     "assert", "async",  "await",  "break",
                               "class", "continue","def",   "del",    "elif",   "else",
                               "except","finally","for",    "from",   "global", "if",
                               "import","in",     "is",     "lambda", "None",   "nonlocal",
                               "not",   "or",     "pass",   "raise",  "return", "True",
                               "False", "try",    "while",  "with",   "yield",  "self"};
    AddRule(QStringLiteral("\\b(%1)\\b").arg(Join(kw, sizeof(kw) / sizeof(kw[0]))), p.keyword, true);
    AddRule(QStringLiteral(R"(""".*?"""|'''.*?''')"), p.string);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    AddRule(QStringLiteral(R"(#[^\n]*)"), p.comment);
  } else if (lang == "javascript" || lang == "typescript") {
    static const char* kw[] = {"const", "let",  "var",     "function", "return", "if",
                               "else",  "for",  "while",   "class",    "extends","new",
                               "this",  "import","from",   "export",   "default","async",
                               "await", "try",  "catch",   "finally",  "throw",  "typeof",
                               "instanceof","null","undefined","true",  "false",  "of",
                               "in",    "delete","switch",  "case",     "break",  "continue"};
    AddRule(QStringLiteral("\\b(%1)\\b").arg(Join(kw, sizeof(kw) / sizeof(kw[0]))), p.keyword, true);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    commentStart_ = QRegularExpression(QStringLiteral(R"(/\*)"));
    commentEnd_ = QRegularExpression(QStringLiteral(R"(\*/)"));
    AddRule(QStringLiteral(R"(//[^\n]*)"), p.comment);
  } else if (lang == "json") {
    AddRule(QStringLiteral(R"("(?:\\.|[^"\\])*"(?=\s*:))"), p.key);
    AddRule(strRule, p.string);
    AddRule(QStringLiteral(R"(\b(?:true|false|null)\b)"), p.keyword, true);
    AddRule(numRule, p.number);
  } else if (lang == "yaml" || lang == "ini") {
    AddRule(QStringLiteral(R"(^\s*[\w.\-]+(?=\s*:))"), p.key);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    AddRule(QStringLiteral(R"(#[^\n]*)"), p.comment);
  } else if (lang == "bash" || lang == "powershell" || lang == "batch") {
    static const char* kw[] = {"if",   "then", "else", "elif", "fi",   "for",  "while",
                               "do",   "done", "case", "esac", "function", "return",
                               "export","echo", "cd",   "sudo", "tar",  "git",  "npm"};
    AddRule(QStringLiteral("\\b(%1)\\b").arg(Join(kw, sizeof(kw) / sizeof(kw[0]))), p.keyword, true);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    AddRule(QStringLiteral(R"(#[^\n]*)"), p.comment);
  } else if (lang == "html" || lang == "markdown" || lang == "diff") {
    AddRule(QStringLiteral(R"(<\/?[\w\-]+)"), p.keyword);
    AddRule(strRule, p.string);
    if (lang == "diff") {
      AddRule(QStringLiteral(R"(^\+[^\n]*)"), p.string);
      AddRule(QStringLiteral(R"(^-[^\n]*)"), p.preproc);
      AddRule(QStringLiteral(R"(^@@[^\n]*)"), p.type);
    }
  } else if (lang == "sql") {
    static const char* kw[] = {"SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE",
                               "SET",    "DELETE","JOIN",  "LEFT",   "RIGHT",  "INNER",  "ON",
                               "GROUP",  "BY",    "ORDER", "LIMIT",  "CREATE", "TABLE",  "DROP",
                               "ALTER",  "AND",   "OR",    "NOT",    "NULL",   "AS",     "DISTINCT"};
    AddRule(QStringLiteral("\\b(?i)(%1)\\b").arg(Join(kw, sizeof(kw) / sizeof(kw[0]))), p.keyword,
            true);
    AddRule(strRule, p.string);
    AddRule(numRule, p.number);
    AddRule(QStringLiteral(R"(--[^\n]*)"), p.comment);
  } else {
    AddRule(strRule, p.string);
  }
}

void CodeHighlighter::highlightBlock(const QString& text) {
  for (const auto& r : rules_) {
    auto it = r.pattern.globalMatch(text);
    while (it.hasNext()) {
      const auto m = it.next();
      setFormat(m.capturedStart(), m.capturedLength(), r.format);
    }
  }

  // 多行注释 /* ... */（跨行状态保存在 currentBlockState）
  if (commentStart_.pattern().isEmpty()) {
    setCurrentBlockState(0);
    return;
  }
  int state = 0;
  if (previousBlockState() == 1) {
    const auto endMatch = commentEnd_.match(text);
    if (endMatch.hasMatch()) {
      setFormat(0, endMatch.capturedEnd(), commentFormat_);
    } else {
      setFormat(0, text.length(), commentFormat_);
      setCurrentBlockState(1);
      return;
    }
  }
  int startIndex = 0;
  if (previousBlockState() != 1) startIndex = text.indexOf(commentStart_);
  while (startIndex >= 0) {
    const auto endMatch = commentEnd_.match(text, startIndex);
    int commentLength = 0;
    if (endMatch.hasMatch()) {
      commentLength = endMatch.capturedEnd() - startIndex;
      state = 0;
    } else {
      commentLength = text.length() - startIndex;
      state = 1;
    }
    setFormat(startIndex, commentLength, commentFormat_);
    startIndex = state == 0 ? text.indexOf(commentStart_, startIndex + commentLength) : -1;
  }
  setCurrentBlockState(state);
  (void)multiLineComment_;
}

}  // namespace wd
