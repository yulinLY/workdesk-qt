#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>
#include <string>

namespace wd {

// 轻量语法高亮：按语言加载关键字表，规则是"注释 / 字符串 / 数字 / 关键字"四类。
// 目标不是 IDE 级着色，而是让 AI 回复里的代码块一眼能分清结构。
class CodeHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
 public:
  CodeHighlighter(QTextDocument* doc, const std::string& lang);
  // 主题切换后重新取色并全量重高亮
  void Recolor();

 protected:
  void highlightBlock(const QString& text) override;

 private:
  struct Rule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };
  void AddRule(const QString& pattern, const QColor& color, bool bold = false);
  void BuildFor(const std::string& lang);

  std::string lang_;
  QVector<Rule> rules_;
  QRegularExpression commentStart_;
  QRegularExpression commentEnd_;
  QTextCharFormat commentFormat_;
  bool multiLineComment_ = false;
};

}  // namespace wd
