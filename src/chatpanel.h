#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "session.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QTimer;

namespace wd {

class MessageBubble;

// 聊天模块：流式对话 + markdown/代码块/表格渲染 + 多会话 + 图片与文件附件。
class ChatPanel : public QWidget {
  Q_OBJECT
 public:
  explicit ChatPanel(Config* cfg, QWidget* parent = nullptr);
  ~ChatPanel() override;

  void AttachPaths(const QStringList& paths);  // 拖入 / 选择文件后统一入口
  void FocusInput();
  void NewSession();
  bool Busy() const;
  void StopStreaming();
  void RefreshModelInfo();  // 设置改动后刷新「配置 · 模型」显示

  // 记事本互通：每次发请求前回调取「当前打开文档」的上下文片段（空=没打开文件）
  void SetLiveContextProvider(std::function<std::string()> fn) { liveCtx_ = std::move(fn); }

 signals:
  void RequestSettings();
  void Notice(const QString& text);
  void WriteToNotepad(const QString& text);

 protected:
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  void BuildUi();
  void Send();
  void Regenerate();
  void AddBubble(const ChatMessage& msg, bool saveIt);
  void ClearList();
  void ScrollToBottom(bool force);
  void StepScroll();
  void PollStream();
  void PumpTypewriter(bool flush);
  void RefreshSessions(int keepIndex = -1);
  void SaveCurrent(bool force = false);
  void LoadSessionAt(int idx);
  void DeleteCurrentSession();
  void ExportCurrent();
  void ImportSession();
  void UpdatePendingChips();
  void SetBusyUi(bool busy);
  void EnsureConfigured();
  void RefreshProfileCombo();
  void OnProfilePicked(int idx);
  int LastUserIndex() const;
  // 点开消息里的附件/图片：优先用系统默认程序打开原文件，原文件丢失时回落到内存里的图片数据
  void OpenAttachment(const QString& path, const QString& b64, const QString& name);

  Config* cfg_ = nullptr;
  std::function<std::string()> liveCtx_;
  std::vector<ChatMessage> msgs_;
  std::vector<ChatImage> pendingImgs_;
  std::vector<ChatFile> pendingFiles_;
  std::vector<SessionInfo> sessions_;
  SessionInfo current_;
  bool hasSession_ = false;

  class ChatStream* stream_ = nullptr;
  QTimer* pollTimer_ = nullptr;
  QTimer* scrollTimer_ = nullptr;  // 平滑贴底：16ms 步进，不直接 setValue 跳到最大
  bool stickBottom_ = true;        // 是否还在跟随底部（用户上翻后置 false）
  int lastScrollMax_ = 0;          // 上次看到的内容上界：区分"内容长高"与"用户上翻"
  MessageBubble* streamingBubble_ = nullptr;
  int streamingIndex_ = -1;
  bool streamEnded_ = false;
  // 打字机：typeTarget_ 是"已经收到"的完整文本，typeShown_ 是已吐到气泡上的位置
  QString typeTarget_;
  int typeShown_ = 0;
  std::string streamError_;
  bool dirty_ = false;

  QScrollArea* scroll_ = nullptr;
  QWidget* listHost_ = nullptr;
  QVBoxLayout* listLay_ = nullptr;
  QComboBox* sessionBox_ = nullptr;
  QPlainTextEdit* input_ = nullptr;
  QWidget* chips_ = nullptr;
  QHBoxLayout* chipsLay_ = nullptr;
  QPushButton* sendBtn_ = nullptr;
  QPushButton* stopBtn_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* modelLab_ = nullptr;
  QComboBox* profileBox_ = nullptr;  // 底部工具条上的快速切换（多套 AI 配置）
};

}  // namespace wd
