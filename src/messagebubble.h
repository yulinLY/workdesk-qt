#pragma once
#include <QFrame>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

#include "session.h"

class QVBoxLayout;
class QHBoxLayout;
class QLabel;

namespace wd {

class MdBlockWidget;

// 单条消息气泡。
// 文本用 QTextBrowser（原生选中 / Ctrl+C / 链接），代码块用只读 QPlainTextEdit
// （原生横向滚动 + 原生选中，彻底避开 ImGui 自绘那套"拖不动、选不准"的问题），
// 表格用 QTableWidget（可横向滚动、可复制单元格）。
class MessageBubble : public QWidget {
  Q_OBJECT
 public:
  explicit MessageBubble(const ChatMessage& msg, QWidget* parent = nullptr);
  ~MessageBubble() override;

  // 流式更新正文（同一实例上增量刷新，避免整条重建导致滚动跳动）
  void SetText(const QString& text);
  void Finalize();
  const ChatMessage& Message() const { return msg_; }
  QString PlainText() const;

 signals:
  void ImageClicked(const QString& filePath);
  // 附件/图片被点开：统一交给 ChatPanel 用系统默认程序打开（图片在原始文件丢失时回落到内存数据）
  void FileClicked(const QString& filePath, int imageIndex);

 protected:
  void resizeEvent(QResizeEvent* e) override;
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  void BuildUi();
  void SyncBlocks(bool force);
  void RelayoutForWidth();
  void UpdateBubbleWidth();

  ChatMessage msg_;
  bool streaming_ = false;
  QString text_;

  QWidget* row_ = nullptr;        // 整行（用于左右对齐）
  QLabel* avatar_ = nullptr;      // 头像占位
  QFrame* bubble_ = nullptr;      // 气泡本体
  QVBoxLayout* bubbleLay_ = nullptr;
  QLabel* meta_ = nullptr;        // 时间/角色
  QWidget* content_ = nullptr;    // 块容器
  QVBoxLayout* contentLay_ = nullptr;
  std::vector<MdBlockWidget*> blocks_;

  // 附件区
  QWidget* attach_ = nullptr;
  QHBoxLayout* attachLay_ = nullptr;

  int lastBubbleW_ = -1;  // 上次设置的气泡宽度，避免每帧重复 setFixedWidth 触发 relayout
  int blockSignature_ = 0;
  bool syncing_ = false;
};

}  // namespace wd
