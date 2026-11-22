#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QSlider;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace wd {

// 音乐模块：曲库（按文件夹分组）+ 封面/曲目信息 + 歌词 + 控制条。
// 曲库落在 %APPDATA%\WorkDesk\music.json，列表默认把库里的歌全摆出来，
// 搜索只在视图里过滤（可选全部曲库或某个文件夹），不改动播放列表顺序。
// 界面自己不持有播放状态，每 200ms 从 MusicPlayer 拉一次，所以模块隐藏、切模块，播放都不断。
class MusicPanel : public QWidget {
  Q_OBJECT
 public:
  explicit MusicPanel(QWidget* parent = nullptr);

  // 外部（拖放 / 打开方式 / 命令行）送进来的音频文件：收进曲库（父目录自动成组）
  void AddPaths(const QStringList& paths);
  // 有歌就直接放，没歌就什么也不做（给"拖进来立即播"用）
  void PlayFirstIfIdle();

  static bool IsAudioPath(const QString& path);

 protected:
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dragLeaveEvent(QDragLeaveEvent* e) override;
  void dropEvent(QDropEvent* e) override;

 private:
  void BuildUi();
  void Tick();               // 200ms 一帧：进度、时间、歌词
  void RefreshList();        // 按「关键词 + 范围」重建分组树
  void RefreshCurrent();     // 换歌后刷新标题/封面/时长/列表高亮
  void SyncControls();       // 播放按钮文案、模式文案、音量
  void CycleMode();
  void ShowLyrics(const QString& lrcText);
  void UpdateLyricLine(int posMs);
  void SetCoverPlaceholder();
  void SyncEmptyHint();

  // ---- 曲库
  void SyncPlayerLibrary();   // 把曲库整表同步给播放器（不打断正在放的歌）
  void ImportFolderDialog();  // 导入文件夹（递归收歌，位置记进曲库）
  void ImportFilesDialog();   // 导入歌曲：选文件 → 选存放文件夹
  QString AskTargetFolder(const QString& suggested) const;  // 选「存放文件夹」
  void RemoveSelectedFolder();  // 把当前文件夹移出曲库（不删文件）
  void RescanSelectedFolder();  // 重扫当前文件夹（外面增删过文件时用）

  // ---- 搜索 / 列表
  void BuildSearchBar(QVBoxLayout* lay);
  void ReloadScopeBox();            // 重建范围下拉：全部曲库 + 每个文件夹
  QString SelectedScope() const;    // 范围下拉对应的文件夹路径（空=全部曲库）
  QString SelectedFolderPath() const;  // 树里选中的文件夹（分组行或其子行）
  void PlayTreeItem(QTreeWidgetItem* it);
  void HighlightCurrent();  // 把正在播放的那行设为选中
  void SyncSearchHint();    // 刷新那行状态小字

  QTreeWidget* tree_ = nullptr;
  QLabel* empty_ = nullptr;
  QLabel* cover_ = nullptr;
  QLabel* title_ = nullptr;
  QLabel* artist_ = nullptr;
  QLabel* album_ = nullptr;
  QLabel* time_ = nullptr;
  QLabel* lyricPrev_ = nullptr;
  QLabel* lyricCur_ = nullptr;
  QLabel* lyricNext_ = nullptr;
  QSlider* prog_ = nullptr;
  QSlider* vol_ = nullptr;
  QToolButton* prevBtn_ = nullptr;
  QToolButton* playBtn_ = nullptr;
  QToolButton* nextBtn_ = nullptr;
  QToolButton* modeBtn_ = nullptr;
  QToolButton* importBtn_ = nullptr;
  QTimer* tick_ = nullptr;

  QLineEdit* searchEdit_ = nullptr;
  QComboBox* scopeBox_ = nullptr;
  QLabel* srcHint_ = nullptr;

  QMenu* importMenu_ = nullptr;
  QAction* actImportFile_ = nullptr;
  QAction* actImportDir_ = nullptr;
  QAction* actRescan_ = nullptr;
  QAction* actRemoveDir_ = nullptr;

  QList<QPair<int, QString>> lyrics_;  // 毫秒 + 该行文本，按时间升序
  QString trackKey_;                    // 当前曲目标识，变了才整体刷新
  int lyricIdx_ = -1;
  int lastDur_ = 0;
  bool userSeeking_ = false;
  bool dropHi_ = false;
};

}  // namespace wd