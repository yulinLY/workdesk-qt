#pragma once
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "config.h"

class QLabel;
class QSplitter;
class QVariantAnimation;

namespace wd {

class ChatPanel;
class DockBar;
class MusicPanel;
class NotepadPanel;

// 主窗口：模块卡片式布局，左右分栏可用分隔条自由拖动改变大小（尺寸记忆在 QSettings）。
// 拖入文件时按类型自动分模块：图片/二进制 -> 聊天附件；文本/代码 -> 记事本。
// 设置 / 关于 / 时钟 / 一键隐藏控件全部收在右侧 dock 栏里，界面上不再放提示文案。
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow();
  ~MainWindow() override;

  // 供应用级拖放路由器调用（子控件会自己吃拖放事件，必须在窗口层面统一接管）
  void HandleDropUrls(const QList<QUrl>& urls, QWidget* target = nullptr);

  // 启动时从命令行/「打开方式」传入的文件：文本进记事本，其余按拖放规则分流
  void OpenFiles(const QStringList& paths);
  void ShowDropHint(bool on);

 protected:
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  void closeEvent(QCloseEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void paintEvent(QPaintEvent* e) override;
  void showEvent(QShowEvent* e) override;
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private:
  QWidget* MakeCard(const QString& title, QWidget* content, QWidget* parent = nullptr);
  void BuildUi();
  void BuildDock();
  void OpenSettings();
  void ShowAbout();
  void RoutePaths(const QStringList& paths, bool preferChat = false);
  void RestoreLayout();
  void SaveLayout();
  // 首次启动的欢迎向导（走完或跳过都会落一个标记，之后不再弹）
  void MaybeShowWelcome();
  // 生日彩蛋：今天正好是生日、今年还没弹过，才弹
  void MaybeShowBirthday();
  void ToggleChrome();
  void SetChromeHidden(bool hide);
  void LayoutDock();
  // 拖动模块标题栏换位
  void MoveCard(QWidget* from, QWidget* to);
  void ApplyCardOrder(const QStringList& order);
  QString CardOrder() const;
  QWidget* CardAtGlobal(const QPoint& gp) const;
  void SetMoveHighlight(QWidget* card);
  // 模块级隐藏：卡片从布局里消失，dock 里留一条还原入口
  void HideModule(QWidget* card);
  void RestoreModule(const QString& id);
  void SyncDockModules();
  // 窗口最小宽度随「当前可见模块数」走：三块全开时不把最右侧卡片顶出边界，
  // 隐藏一块之后窗口就能继续往窄里缩。
  void UpdateMinWidth();
  QWidget* CardById(const QString& id) const;
  int VisibleCardCount() const;
  // 拖动时跟着鼠标走的那块半透明拖影
  void BeginGhost(QWidget* card, const QPoint& gp);
  void MoveGhost(const QPoint& gp);
  void EndGhost();
  // 换位时卡片宽度平滑滑动，不再瞬间跳位
  void AnimateSizes(const QList<int>& from, const QList<int>& to);

  Config cfg_;
  ChatPanel* chat_ = nullptr;
  NotepadPanel* notepad_ = nullptr;
  MusicPanel* music_ = nullptr;
  QWidget* musicCard_ = nullptr;
  DockBar* dock_ = nullptr;
  QSplitter* hsplit_ = nullptr;
  QWidget* root_ = nullptr;
  QWidget* top_ = nullptr;
  QList<QWidget*> cards_;
  QList<QWidget*> chromeHeads_;
  QWidget* dragCard_ = nullptr;   // 正在拖的模块卡片
  QWidget* dropCard_ = nullptr;   // 当前落点模块卡片
  QPoint dragStart_;
  QWidget* ghost_ = nullptr;              // 鼠标上跟随的拖影
  QVariantAnimation* sizeAnim_ = nullptr;  // 换位滑动动画
  QStringList hiddenIds_;                  // 被隐藏模块的 cardId
  bool dragging_ = false;
  bool chromeHidden_ = false;
};

}  // namespace wd
