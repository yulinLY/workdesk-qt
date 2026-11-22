#include "musicpanel.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QInputDialog>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdlib>

#include "musiclibrary.h"
#include "musicplayer.h"
#include "musicprovider.h"
#include "overflowbar.h"
#include "wdtrace.h"

namespace wd {
namespace {

const char* const kAudioExts[] = {"mp3", "flac", "wav", "m4a", "aac", "ogg",
                                  "opus", "wma", "ape", "aiff", "aif", "mid", "midi"};

bool HasAudioExt(const QString& path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  for (const char* e : kAudioExts) {
    if (ext == QLatin1String(e)) return true;
  }
  return false;
}

QString FmtTime(int ms) {
  if (ms <= 0) return QStringLiteral("00:00");
  const int total = ms / 1000;
  const int m = total / 60;
  const int s = total % 60;
  return QStringLiteral("%1:%2")
      .arg(m, 2, 10, QLatin1Char('0'))
      .arg(s, 2, 10, QLatin1Char('0'));
}

// [mm:ss]、[mm:ss.xx]、[mm:ss.xxx]，一行里可能有多个标签
QList<QPair<int, QString>> ParseLrc(const QString& text) {
  QList<QPair<int, QString>> out;
  static const QRegularExpression tag(
      QStringLiteral("\\[(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]"));
  const QStringList lines = text.split(QLatin1Char('\n'));
  for (const QString& raw : lines) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) continue;
    auto it = tag.globalMatch(line);
    QList<int> stamps;
    int end = 0;
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      const int min = m.captured(1).toInt();
      const int sec = m.captured(2).toInt();
      const QString frac = m.captured(3);
      int ms = 0;
      if (!frac.isEmpty()) {
        if (frac.size() == 1) ms = frac.toInt() * 100;
        else if (frac.size() == 2) ms = frac.toInt() * 10;
        else ms = frac.left(3).toInt();
      }
      stamps << min * 60000 + sec * 1000 + ms;
      end = m.capturedEnd();
    }
    if (stamps.isEmpty()) continue;
    const QString body = line.mid(end).trimmed();
    for (int t : stamps) out << qMakePair(t, body);
  }
  std::sort(out.begin(), out.end(),
            [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
              return a.first < b.first;
            });
  return out;
}

}  // namespace

bool MusicPanel::IsAudioPath(const QString& path) { return HasAudioExt(path); }

MusicPanel::MusicPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("MusicPanel"));
  setAcceptDrops(true);
  BuildUi();

  // 曲库：读盘 → 同步给播放器；之后导入 / 移出 / 重扫都走 Changed 统一刷新
  {
    MusicLibrary& lib = MusicLibrary::Instance();
    lib.Load();
    ReloadScopeBox();
    SyncPlayerLibrary();
    connect(&lib, &MusicLibrary::Changed, this, [this]() {
      ReloadScopeBox();
      SyncPlayerLibrary();
      RefreshList();
      SyncEmptyHint();
      SyncSearchHint();
    });
  }

  

  // 自测：WD_MUSIC_SELFTEST=<关键词> 时启动 1.2 秒后跑一次本地搜索，结果打到 boot.log
  if (const char* kw = std::getenv("WD_MUSIC_SELFTEST")) {
    const QString key = QString::fromUtf8(kw);
    QTimer::singleShot(1200, this, [this, key]() {
      MusicSourceHub::Instance().Search(
          key, [this, key](bool ok, const QList<MusicHit>& hits, const QString& err) {
            char b[256];
            snprintf(b, sizeof(b), "selftest music: kw=%s ok=%d hits=%d err=%s",
                     key.toUtf8().constData(), int(ok), int(hits.size()),
                     err.toUtf8().constData());
            wd::Trace(b);
            if (!hits.isEmpty()) {
              char c[512];
              snprintf(c, sizeof(c), "selftest music: first=%s / %s local=%d",
                       hits.first().title.toUtf8().constData(),
                       hits.first().artist.toUtf8().constData(),
                       int(hits.first().local));
              wd::Trace(c);
            }
            if (srcHint_)
              srcHint_->setText(QStringLiteral("自测：%1 → %2 首").arg(key).arg(hits.size()));
          });
    });
  }

  MusicPlayer& mp = MusicPlayer::Instance();
  connect(&mp, &MusicPlayer::TrackChanged, this, &MusicPanel::RefreshCurrent);
  connect(&mp, &MusicPlayer::PlaylistChanged, this, [this]() {
    RefreshList();
    SyncEmptyHint();
  });
  connect(&mp, &MusicPlayer::LyricsReady, this, &MusicPanel::ShowLyrics);
  connect(&mp, &MusicPlayer::Error, this, [this](const QString& msg) {
    // 出错时把歌词区临时让给提示，三秒后自己还给歌词
    lyricPrev_->clear();
    lyricNext_->clear();
    lyricCur_->setText(msg);
    lyricIdx_ = -2;
    QTimer::singleShot(3000, this, [this]() {
      lyricIdx_ = -1;
      UpdateLyricLine(MusicPlayer::Instance().Position());
    });
  });

  tick_ = new QTimer(this);
  tick_->setInterval(200);
  connect(tick_, &QTimer::timeout, this, &MusicPanel::Tick);
  tick_->start();

  RefreshList();
  RefreshCurrent();
  SyncControls();
  SyncEmptyHint();
}

// ------------------------------------------------------------------ 界面
void MusicPanel::BuildUi() {
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(8);

  // ---- 曲目信息：封面 + 三行文字
  auto* head = new QWidget(this);
  auto* hl = new QHBoxLayout(head);
  hl->setContentsMargins(0, 0, 0, 0);
  hl->setSpacing(10);

  cover_ = new QLabel(head);
  cover_->setObjectName(QStringLiteral("MusicCover"));
  cover_->setFixedSize(64, 64);
  cover_->setAlignment(Qt::AlignCenter);
  cover_->setText(QStringLiteral("♪"));
  hl->addWidget(cover_, 0, Qt::AlignTop);

  auto* info = new QVBoxLayout();
  info->setContentsMargins(0, 0, 0, 0);
  info->setSpacing(2);
  title_ = new QLabel(QStringLiteral("还没有在放歌"), head);
  title_->setObjectName(QStringLiteral("MusicTitle"));
  title_->setWordWrap(false);
  title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  title_->setMinimumWidth(0);
  artist_ = new QLabel(QStringLiteral("拖音频文件进来，或点「导入」"), head);
  artist_->setObjectName(QStringLiteral("MusicArtist"));
  artist_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  artist_->setMinimumWidth(0);
  album_ = new QLabel(QString(), head);
  album_->setObjectName(QStringLiteral("MusicAlbum"));
  album_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  album_->setMinimumWidth(0);
  info->addWidget(title_);
  info->addWidget(artist_);
  info->addWidget(album_);
  info->addStretch(1);
  hl->addLayout(info, 1);
  lay->addWidget(head);
  BuildSearchBar(lay);

  // ---- 歌词：上一行 / 当前行 / 下一行
  auto* lyricBox = new QWidget(this);
  auto* ll = new QVBoxLayout(lyricBox);
  ll->setContentsMargins(0, 2, 0, 2);
  ll->setSpacing(1);
  lyricPrev_ = new QLabel(lyricBox);
  lyricPrev_->setObjectName(QStringLiteral("MusicLyricSide"));
  lyricPrev_->setAlignment(Qt::AlignHCenter);
  lyricCur_ = new QLabel(QStringLiteral("没有歌词的时候，就听伴奏"), lyricBox);
  lyricCur_->setObjectName(QStringLiteral("MusicLyricCur"));
  lyricCur_->setAlignment(Qt::AlignHCenter);
  lyricCur_->setWordWrap(true);
  lyricNext_ = new QLabel(lyricBox);
  lyricNext_->setObjectName(QStringLiteral("MusicLyricSide"));
  lyricNext_->setAlignment(Qt::AlignHCenter);
  ll->addWidget(lyricPrev_);
  ll->addWidget(lyricCur_);
  ll->addWidget(lyricNext_);
  lay->addWidget(lyricBox);

  // ---- 曲库列表：按文件夹分组，默认把库里的歌全摆出来，搜索只过滤显示
  empty_ = new QLabel(
      QStringLiteral("曲库是空的。点「导入」选文件夹或选歌曲，也可以把歌/文件夹直接拖进来。"), this);
  empty_->setObjectName(QStringLiteral("ModuleHint"));
  empty_->setAlignment(Qt::AlignHCenter);
  empty_->setWordWrap(true);
  lay->addWidget(empty_);

  tree_ = new QTreeWidget(this);
  tree_->setObjectName(QStringLiteral("MusicTree"));
  tree_->setHeaderHidden(true);
  tree_->setRootIsDecorated(true);
  tree_->setIndentation(12);
  tree_->setUniformRowHeights(true);
  tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setToolTip(QStringLiteral("双击歌曲开始播放；分组行可以折叠"));
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* it, int) { PlayTreeItem(it); });
  lay->addWidget(tree_, 1);

  // ---- 进度条 + 时间
  prog_ = new QSlider(Qt::Horizontal, this);
  prog_->setObjectName(QStringLiteral("MusicProgress"));
  prog_->setRange(0, 1);
  prog_->setCursor(Qt::PointingHandCursor);
  connect(prog_, &QSlider::sliderPressed, this, [this]() { userSeeking_ = true; });
  connect(prog_, &QSlider::sliderReleased, this, [this]() {
    userSeeking_ = false;
    MusicPlayer::Instance().Seek(prog_->value());
  });
  connect(prog_, &QSlider::sliderMoved, this, [this](int v) {
    time_->setText(FmtTime(v) + QStringLiteral(" / ") + FmtTime(lastDur_));
  });
  lay->addWidget(prog_);

  auto* timeRow = new QWidget(this);
  auto* trl = new QHBoxLayout(timeRow);
  trl->setContentsMargins(0, 0, 0, 0);
  trl->setSpacing(6);
  time_ = new QLabel(QStringLiteral("00:00 / 00:00"), timeRow);
  time_->setObjectName(QStringLiteral("MusicTime"));
  trl->addWidget(time_);
  trl->addStretch(1);

  // 音量滑块挪到时间行右端：控制条只留播放相关按钮，卡片变窄时才不会挤成一团
  vol_ = new QSlider(Qt::Horizontal, timeRow);
  vol_->setObjectName(QStringLiteral("MusicVolume"));
  vol_->setRange(0, 1000);
  vol_->setValue(MusicPlayer::Instance().Volume());
  vol_->setMinimumWidth(56);
  vol_->setToolTip(QStringLiteral("音量"));
  connect(vol_, &QSlider::valueChanged, this,
          [](int v) { MusicPlayer::Instance().SetVolume(v); });
  trl->addWidget(vol_);
  lay->addWidget(timeRow);

  // ---- 控制条（卡片变窄时「顺序」「导入」收进「⋯」，上一首/播放/下一首始终在手边）
  auto* ctrl = new OverflowBar(this);
  ctrl->SetSpacing(6);

  prevBtn_ = new QToolButton(ctrl);
  prevBtn_->setObjectName(QStringLiteral("MusicBtn"));
  prevBtn_->setText(QStringLiteral("上一首"));
  prevBtn_->setCursor(Qt::PointingHandCursor);
  connect(prevBtn_, &QToolButton::clicked, this, [this]() {
    RefreshList();
    MusicPlayer::Instance().Prev();
  });

  playBtn_ = new QToolButton(ctrl);
  playBtn_->setObjectName(QStringLiteral("MusicPlayBtn"));
  playBtn_->setText(QStringLiteral("播放"));
  playBtn_->setMinimumWidth(58);
  playBtn_->setCursor(Qt::PointingHandCursor);
  connect(playBtn_, &QToolButton::clicked, this, [this]() {
    MusicPlayer& mp = MusicPlayer::Instance();
    if (mp.Playlist().isEmpty()) {
      ImportFilesDialog();
      return;
    }
    RefreshList();
    mp.PlayPause();
  });

  nextBtn_ = new QToolButton(ctrl);
  nextBtn_->setObjectName(QStringLiteral("MusicBtn"));
  nextBtn_->setText(QStringLiteral("下一首"));
  nextBtn_->setCursor(Qt::PointingHandCursor);
  connect(nextBtn_, &QToolButton::clicked, this, [this]() {
    RefreshList();
    MusicPlayer::Instance().Next();
  });

  modeBtn_ = new QToolButton(ctrl);
  modeBtn_->setObjectName(QStringLiteral("MusicBtn"));
  modeBtn_->setText(QStringLiteral("顺序"));
  modeBtn_->setCursor(Qt::PointingHandCursor);
  modeBtn_->setToolTip(QStringLiteral("顺序播放 / 单曲循环 / 随机播放"));
  connect(modeBtn_, &QToolButton::clicked, this, &MusicPanel::CycleMode);

  importBtn_ = new QToolButton(ctrl);
  importBtn_->setObjectName(QStringLiteral("MusicBtn"));
  importBtn_->setText(QStringLiteral("导入"));
  importBtn_->setCursor(Qt::PointingHandCursor);
  importBtn_->setToolTip(QStringLiteral("导入歌曲（选存放文件夹）或导入整个文件夹"));
  importMenu_ = new QMenu(importBtn_);
  actImportFile_ = importMenu_->addAction(QStringLiteral("导入歌曲…（选存放文件夹）"));
  actImportDir_ = importMenu_->addAction(QStringLiteral("导入文件夹…（整目录收进曲库）"));
  importMenu_->addSeparator();
  actRescan_ = importMenu_->addAction(QStringLiteral("重新扫描当前文件夹"));
  actRemoveDir_ = importMenu_->addAction(QStringLiteral("把当前文件夹移出曲库"));
  // 不给按钮挂 QToolButton 自带菜单：那样会在按钮右侧画一个对不齐的箭头。
  // 改成点按钮时手动把菜单弹在按钮下方，点击行为不变、箭头彻底没有。
  connect(importBtn_, &QToolButton::clicked, this, [this]() {
    if (importBtn_ && importMenu_)
      importMenu_->exec(importBtn_->mapToGlobal(QPoint(0, importBtn_->height())));
  });
  connect(actImportFile_, &QAction::triggered, this, &MusicPanel::ImportFilesDialog);
  connect(actImportDir_, &QAction::triggered, this, &MusicPanel::ImportFolderDialog);
  connect(actRescan_, &QAction::triggered, this, &MusicPanel::RescanSelectedFolder);
  connect(actRemoveDir_, &QAction::triggered, this, &MusicPanel::RemoveSelectedFolder);

  ctrl->Add(prevBtn_, 0);
  ctrl->Add(playBtn_, 0);
  ctrl->Add(nextBtn_, 0);
  ctrl->Add(modeBtn_, 3);    // 顺序 / 单曲循环 / 随机
  ctrl->Add(importBtn_, 2);  // 导入
  ctrl->AddStretch(1);

  lay->addWidget(ctrl);
}

// ------------------------------------------------------------------ 刷新
// 列表里显示的曲目文案：与播放器同一套拆分规则
static QString TrackLabel(const QString& path) {
  const Track t = TrackFromLocalFile(path);
  if (t.artist.isEmpty()) return t.title.isEmpty() ? QFileInfo(path).fileName() : t.title;
  return QStringLiteral("%1  ·  %2").arg(t.title, t.artist);
}

void MusicPanel::RefreshList() {
  if (!tree_) return;
  const QString kw = searchEdit_ ? searchEdit_->text().trimmed() : QString();
  const QString scope = SelectedScope();

  const QSignalBlocker block(tree_);
  tree_->clear();

  const QList<MusicFolder>& folders = MusicLibrary::Instance().Folders();
  for (const MusicFolder& mf : folders) {
    if (!scope.isEmpty() && mf.path.compare(scope, Qt::CaseInsensitive) != 0) continue;

    QStringList hits;
    for (const QString& path : mf.tracks) {
      if (!kw.isEmpty() && !TrackLabel(path).contains(kw, Qt::CaseInsensitive) &&
          !path.contains(kw, Qt::CaseInsensitive)) {
        continue;
      }
      hits << path;
    }
    if (!kw.isEmpty() && hits.isEmpty()) continue;  // 搜索时把没命中的分组一起藏掉

    auto* group = new QTreeWidgetItem(tree_);
    group->setText(0, QStringLiteral("%1  (%2)").arg(QDir(mf.path).dirName()).arg(hits.size()));
    group->setToolTip(0, mf.path);
    group->setData(0, Qt::UserRole, mf.path);
    group->setData(0, Qt::UserRole + 1, 1);  // 标记：这是分组行
    QFont gf = group->font(0);
    gf.setBold(true);
    group->setFont(0, gf);

    for (const QString& path : hits) {
      auto* item = new QTreeWidgetItem(group);
      item->setText(0, TrackLabel(path));
      item->setToolTip(0, path);
      item->setData(0, Qt::UserRole, path);
    }
  }
  tree_->expandAll();
  HighlightCurrent();
}

void MusicPanel::RefreshCurrent() {
  MusicPlayer& mp = MusicPlayer::Instance();
  const Track* t = mp.Current();
  if (!t) {
    trackKey_.clear();
    title_->setText(QStringLiteral("还没有在放歌"));
    artist_->setText(QStringLiteral("拖音频文件进来，或点「导入」"));
    album_->clear();
    lyrics_.clear();
    lyricIdx_ = -1;
    lyricPrev_->clear();
    lyricCur_->setText(QStringLiteral("没有歌词的时候，就听伴奏"));
    lyricNext_->clear();
    return;
  }
  const QString key = t->title + QLatin1Char('\x1f') + t->artist + QLatin1Char('\x1f') +
                      t->streamUrl;
  if (key != trackKey_) {
    trackKey_ = key;
    lyrics_.clear();  // 换歌先清歌词，等 LyricsReady 再填，避免串行
    lyricIdx_ = -1;
    lyricPrev_->clear();
    lyricCur_->setText(QStringLiteral("正在找歌词…"));
    lyricNext_->clear();
  }
  title_->setText(t->title.isEmpty() ? QStringLiteral("未命名") : t->title);
  artist_->setText(t->artist.isEmpty() ? QStringLiteral("未知艺术家") : t->artist);
  album_->setText(t->album);
  album_->setVisible(!t->album.isEmpty());
  SetCoverPlaceholder();

  const int dur = mp.Duration();
  if (dur > 0) prog_->setRange(0, dur);
  HighlightCurrent();
  SyncControls();
}

void MusicPanel::SyncControls() {
  MusicPlayer& mp = MusicPlayer::Instance();
  playBtn_->setText(mp.Playing() ? QStringLiteral("暂停") : QStringLiteral("播放"));
  switch (mp.Mode()) {
    case PlayMode::Sequential:
      modeBtn_->setText(QStringLiteral("顺序"));
      break;
    case PlayMode::Single:
      modeBtn_->setText(QStringLiteral("单曲"));
      break;
    case PlayMode::Shuffle:
      modeBtn_->setText(QStringLiteral("随机"));
      break;
  }
  if (vol_->value() != mp.Volume()) {
    const QSignalBlocker block(vol_);
    vol_->setValue(mp.Volume());
  }
  prevBtn_->setEnabled(!mp.Playlist().isEmpty());
  nextBtn_->setEnabled(!mp.Playlist().isEmpty());
}

void MusicPanel::SyncEmptyHint() {
  const bool none = MusicLibrary::Instance().AllTracks().isEmpty();
  empty_->setVisible(none);
  if (tree_) tree_->setVisible(!none);
}

void MusicPanel::SetCoverPlaceholder() {
  // 没有真正的封面图时，用曲名首字当封面，比一个死音符有辨识度
  const Track* t = MusicPlayer::Instance().Current();
  QString mark = QStringLiteral("♪");
  if (t && !t->title.isEmpty()) mark = t->title.left(1).toUpper();
  cover_->setText(mark);
}

void MusicPanel::Tick() {
  MusicPlayer& mp = MusicPlayer::Instance();
  const int dur = mp.Duration();
  const int pos = mp.Position();
  if (dur != lastDur_) {
    lastDur_ = dur;
    prog_->setRange(0, qMax(1, dur));
  }
  if (!userSeeking_) {
    prog_->setValue(qBound(0, pos, qMax(0, prog_->maximum())));
    time_->setText(FmtTime(pos) + QStringLiteral(" / ") + FmtTime(dur));
  }
  SyncControls();
  UpdateLyricLine(pos);
}

// ------------------------------------------------------------------ 歌词
void MusicPanel::ShowLyrics(const QString& lrcText) {
  lyrics_ = ParseLrc(lrcText);
  lyricIdx_ = -1;
  if (lyrics_.isEmpty()) {
    lyricPrev_->clear();
    lyricCur_->setText(QStringLiteral("这首歌没有歌词"));
    lyricNext_->clear();
    return;
  }
  UpdateLyricLine(MusicPlayer::Instance().Position());
}

void MusicPanel::UpdateLyricLine(int posMs) {
  if (lyrics_.isEmpty() || lyricIdx_ == -2) return;  // -2 = 正在显示错误提示
  int idx = -1;
  for (int i = 0; i < lyrics_.size(); ++i) {
    if (lyrics_.at(i).first <= posMs) idx = i;
    else break;
  }
  if (idx == lyricIdx_) return;
  lyricIdx_ = idx;
  lyricPrev_->setText(idx > 0 ? lyrics_.at(idx - 1).second : QString());
  lyricCur_->setText(idx >= 0 ? lyrics_.at(idx).second : QStringLiteral("前奏…"));
  lyricNext_->setText(idx >= 0 && idx + 1 < lyrics_.size() ? lyrics_.at(idx + 1).second
                                                          : QString());
}

// ------------------------------------------------------------------ 交互
void MusicPanel::CycleMode() {
  MusicPlayer& mp = MusicPlayer::Instance();
  PlayMode next = PlayMode::Sequential;
  switch (mp.Mode()) {
    case PlayMode::Sequential:
      next = PlayMode::Single;
      break;
    case PlayMode::Single:
      next = PlayMode::Shuffle;
      break;
    case PlayMode::Shuffle:
      next = PlayMode::Sequential;
      break;
  }
  mp.SetMode(next);
  SyncControls();
}

// 把曲库整表推给播放器：不打断正在放的歌，只把列表换成库里的顺序
void MusicPanel::SyncPlayerLibrary() {
  const QStringList paths = MusicLibrary::Instance().AllTracks();
  QList<Track> tracks;
  tracks.reserve(paths.size());
  for (const QString& p : paths) tracks << TrackFromLocalFile(p);
  MusicPlayer::Instance().SetLibraryTracks(tracks, QString());
}

void MusicPanel::ImportFolderDialog() {
  QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  if (start.isEmpty() || !QFileInfo(start).isDir())
    start = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择音乐文件夹（会递归收子目录里的歌）"), start);
  if (dir.isEmpty()) return;
  const int n = MusicLibrary::Instance().ImportFolder(dir);
  if (srcHint_) {
    srcHint_->setText(
        QStringLiteral("已收进曲库：%1，共 %2 首（位置已记住）").arg(QDir(dir).dirName()).arg(n));
  }
}

// 选「存放文件夹」：曲库里的文件夹任选，也可以挑一个新目录（会新建一组）
QString MusicPanel::AskTargetFolder(const QString& suggested) const {
  auto* self = const_cast<MusicPanel*>(this);
  QStringList labels;
  QStringList paths;
  for (const MusicFolder& mf : MusicLibrary::Instance().Folders()) {
    labels << QStringLiteral("%1  ·  %2").arg(QDir(mf.path).dirName(), mf.path);
    paths << mf.path;
  }
  const QString other = QStringLiteral("选择其他目录…");
  labels << other;

  bool ok = false;
  const QString pick = QInputDialog::getItem(self, QStringLiteral("选择存放文件夹"),
                                             QStringLiteral("这首歌放进哪个文件夹？"), labels, 0,
                                             false, &ok);
  if (!ok || pick.isEmpty()) return QString();
  if (pick == other) {
    return QFileDialog::getExistingDirectory(self, QStringLiteral("选择存放文件夹"), suggested);
  }
  const int idx = labels.indexOf(pick);
  return (idx >= 0 && idx < paths.size()) ? paths.at(idx) : QString();
}

void MusicPanel::ImportFilesDialog() {
  QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  if (start.isEmpty() || !QFileInfo(start).isDir())
    start = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  const QString filter = QStringLiteral(
      "音频文件 (*.mp3 *.flac *.wav *.m4a *.aac *.ogg *.opus *.wma *.ape *.aiff *.mid);;"
      "所有文件 (*.*)");
  const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择歌曲"), start, filter);
  if (file.isEmpty()) return;

  const QString target = AskTargetFolder(QFileInfo(file).absolutePath());
  if (target.isEmpty()) return;

  const QString placed = MusicLibrary::Instance().ImportFileInto(file, target);
  if (placed.isEmpty()) {
    if (srcHint_)
      srcHint_->setText(QStringLiteral("没能放进「%1」，检查一下有没有写入权限").arg(target));
    return;
  }
  if (srcHint_) {
    srcHint_->setText(QStringLiteral("已放进 %1：%2")
                          .arg(QDir(target).dirName(), QFileInfo(placed).fileName()));
  }
}

void MusicPanel::RemoveSelectedFolder() {
  const QString dir = SelectedFolderPath();
  if (dir.isEmpty()) {
    if (srcHint_) srcHint_->setText(QStringLiteral("先在列表里点一个文件夹（或它下面的歌）"));
    return;
  }
  const QString name = QDir(dir).dirName();
  if (QMessageBox::question(this, QStringLiteral("移出曲库"),
                            QStringLiteral("把「%1」移出曲库？\n只取消收藏，不会删磁盘上的文件。")
                                .arg(name),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  MusicLibrary::Instance().RemoveFolder(dir);
  if (srcHint_) srcHint_->setText(QStringLiteral("已移出：%1").arg(name));
}

void MusicPanel::RescanSelectedFolder() {
  const QString dir = SelectedFolderPath();
  if (dir.isEmpty()) {
    if (srcHint_) srcHint_->setText(QStringLiteral("先在列表里点一个文件夹"));
    return;
  }
  MusicLibrary::Instance().RescanFolder(dir);
  if (srcHint_) srcHint_->setText(QStringLiteral("已重扫：%1").arg(QDir(dir).dirName()));
}

void MusicPanel::AddPaths(const QStringList& paths) {
  QStringList audio;
  for (const QString& p : paths) {
    if (IsAudioPath(p)) audio << p;
  }
  if (audio.isEmpty()) return;
  const QStringList added = MusicLibrary::Instance().AddTrackFiles(audio);
  if (added.isEmpty()) {
    if (srcHint_) srcHint_->setText(QStringLiteral("这些歌已经在曲库里了"));
    return;
  }
  if (srcHint_) srcHint_->setText(QStringLiteral("已收进曲库 %1 首").arg(added.size()));
  RefreshList();
  SyncEmptyHint();
}

void MusicPanel::PlayFirstIfIdle() {
  MusicPlayer& mp = MusicPlayer::Instance();
  if (mp.Playlist().isEmpty()) return;
  if (mp.Playing()) return;
  if (mp.CurrentIndex() < 0) {
    mp.PlayAt(0);
  } else {
    mp.PlayPause();
  }
}

// ------------------------------------------------------------------ 拖放
void MusicPanel::dragEnterEvent(QDragEnterEvent* e) {
  if (!e->mimeData()->hasUrls()) return;
  for (const QUrl& u : e->mimeData()->urls()) {
    if (u.isLocalFile() && HasAudioExt(u.toLocalFile())) {
      dropHi_ = true;
      setProperty("dropHi", true);
      style()->unpolish(this);
      style()->polish(this);
      e->acceptProposedAction();
      return;
    }
  }
}

void MusicPanel::dragLeaveEvent(QDragLeaveEvent*) {
  if (!dropHi_) return;
  dropHi_ = false;
  setProperty("dropHi", false);
  style()->unpolish(this);
  style()->polish(this);
}

void MusicPanel::dropEvent(QDropEvent* e) {
  dragLeaveEvent(nullptr);
  if (!e->mimeData()->hasUrls()) return;
  QStringList paths;
  for (const QUrl& u : e->mimeData()->urls()) {
    if (u.isLocalFile()) paths << u.toLocalFile();
  }
  e->acceptProposedAction();
  AddPaths(paths);
}

// ------------------------------------------------------------ 搜索条（只有本地曲库）
void MusicPanel::BuildSearchBar(QVBoxLayout* lay) {
  // 搜索条：关键词 + 范围（全部曲库 / 某个文件夹）。只过滤列表显示，不改播放顺序。
  auto* bar = new OverflowBar(this);
  bar->SetSpacing(6);

  searchEdit_ = new QLineEdit(bar);
  searchEdit_->setPlaceholderText(QStringLiteral("搜歌名 / 艺术家 / 文件名"));
  searchEdit_->setClearButtonEnabled(true);
  searchEdit_->setMinimumWidth(80);
  bar->Add(searchEdit_, 0, 1);

  scopeBox_ = new QComboBox(bar);
  scopeBox_->setToolTip(QStringLiteral("搜索范围：全部曲库，或只在某个文件夹里找"));
  scopeBox_->setMinimumWidth(96);
  scopeBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  bar->Add(scopeBox_, 0);
  lay->addWidget(bar);

  // 一行小字：曲库统计 / 搜索结果条数。不换行、不撑宽
  srcHint_ = new QLabel(this);
  srcHint_->setObjectName(QStringLiteral("MusicSourceHint"));
  srcHint_->setWordWrap(false);
  srcHint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  lay->addWidget(srcHint_);

  connect(searchEdit_, &QLineEdit::textChanged, this, [this]() {
    RefreshList();
    SyncSearchHint();
  });
  connect(searchEdit_, &QLineEdit::returnPressed, this, [this]() {
    RefreshList();
    SyncSearchHint();
  });
  connect(scopeBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
    RefreshList();
    SyncSearchHint();
  });

  SyncSearchHint();
}

void MusicPanel::SyncSearchHint() {
  if (!srcHint_) return;
  const MusicLibrary& lib = MusicLibrary::Instance();
  const int folderCount = lib.Folders().size();
  const int total = lib.AllTracks().size();
  const QString kw = searchEdit_ ? searchEdit_->text().trimmed() : QString();
  const QString scope = SelectedScope();

  if (total == 0) {
    srcHint_->setText(QStringLiteral("曲库还是空的：点「导入」选文件夹，或把歌拖进来"));
    return;
  }
  if (kw.isEmpty()) {
    srcHint_->setText(QStringLiteral("%1 个文件夹 · %2 首（位置已记住）")
                          .arg(folderCount)
                          .arg(total));
    return;
  }
  int hits = 0;
  for (const MusicFolder& mf : lib.Folders()) {
    if (!scope.isEmpty() && mf.path.compare(scope, Qt::CaseInsensitive) != 0) continue;
    for (const QString& path : mf.tracks) {
      if (TrackLabel(path).contains(kw, Qt::CaseInsensitive) ||
          path.contains(kw, Qt::CaseInsensitive)) {
        ++hits;
      }
    }
  }
  const QString where = scope.isEmpty() ? QStringLiteral("全部曲库") : QDir(scope).dirName();
  srcHint_->setText(QStringLiteral("「%1」在%2里找到 %3 首").arg(kw, where).arg(hits));
}

// 重建「范围」下拉：全部曲库 + 每个文件夹；尽量保住当前选中的那个
void MusicPanel::ReloadScopeBox() {
  if (!scopeBox_) return;
  const QString keep = SelectedScope();
  const QSignalBlocker block(scopeBox_);
  scopeBox_->clear();
  scopeBox_->addItem(QStringLiteral("全部曲库"), QString());
  for (const MusicFolder& mf : MusicLibrary::Instance().Folders()) {
    scopeBox_->addItem(QStringLiteral("%1 (%2)").arg(QDir(mf.path).dirName()).arg(mf.tracks.size()),
                       mf.path);
  }
  const int idx = keep.isEmpty() ? 0 : scopeBox_->findData(keep);
  scopeBox_->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString MusicPanel::SelectedScope() const {
  if (!scopeBox_ || scopeBox_->count() == 0) return QString();
  return scopeBox_->currentData().toString();
}

// 树里选中的文件夹：分组行取自己，歌曲行取父分组；什么都没选返回空
QString MusicPanel::SelectedFolderPath() const {
  if (!tree_) return QString();
  QTreeWidgetItem* it = tree_->currentItem();
  if (!it) return QString();
  if (it->parent()) return it->parent()->data(0, Qt::UserRole).toString();
  return it->data(0, Qt::UserRole).toString();
}

void MusicPanel::PlayTreeItem(QTreeWidgetItem* it) {
  if (!it || !it->parent()) return;  // 分组行双击只做折叠/展开，不播
  const QString path = it->data(0, Qt::UserRole).toString();
  if (path.isEmpty()) return;
  const QList<Track>& pl = MusicPlayer::Instance().Playlist();
  for (int i = 0; i < pl.size(); ++i) {
    if (pl.at(i).localPath.compare(path, Qt::CaseInsensitive) == 0) {
      MusicPlayer::Instance().PlayAt(i);
      return;
    }
  }
}

// 把正在播放的那一行设为选中（换歌 / 曲库刷新后跟着走）
void MusicPanel::HighlightCurrent() {
  if (!tree_) return;
  const Track* cur = MusicPlayer::Instance().Current();
  if (!cur || cur->localPath.isEmpty()) return;
  QTreeWidgetItemIterator it(tree_);
  while (*it) {
    QTreeWidgetItem* item = *it;
    if (item->parent() &&
        item->data(0, Qt::UserRole).toString().compare(cur->localPath, Qt::CaseInsensitive) == 0) {
      tree_->setCurrentItem(item);
      tree_->scrollToItem(item);
      return;
    }
    ++it;
  }
}

}  // namespace wd
