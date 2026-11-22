#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QMediaPlayer;
class QAudioOutput;
class QNetworkAccessManager;

namespace wd {

// 一首歌的元信息。source 表示音频从哪来，决定了失败时怎么降级。
struct Track {
  QString title;
  QString artist;
  QString album;
  QString coverUrl;    // 封面（可能是本地路径也可能是 http 地址）
  QString lyricsUrl;   // 歌词地址（可空）
  QString streamUrl;   // 直链；为空表示需要向代理现取
  QString localPath;   // 本地文件的绝对路径（在线歌曲为空），降级时用它兜底
  bool local = false;  // 本地文件（QUrl::fromLocalFile），不走代理
  QString providerId;  // 代理侧的歌曲 id，用于补取直链
};

// 从本地文件构造 Track：文件名按「艺术家 - 标题」拆一次，拆不出来整串当标题。
// 曲库同步、拖放导入都走这一份解析，保证列表里和播放器里的名字一致。
Track TrackFromLocalFile(const QString& path);

enum class PlayMode { Sequential = 0, Single = 1, Shuffle = 2 };

// 全局唯一的播放器。所有界面（音乐模块、迷你播放器、dock 控制条）都只跟它打交道，
// 它自己不含任何 UI：这样模块隐藏、dock 收起、切到别的模块，播放都不中断。
class MusicPlayer : public QObject {
  Q_OBJECT
 public:
  static MusicPlayer& Instance();

  // ---- 播放控制
  void PlayAt(int index);
  void PlayPause();
  void Next();
  void Prev();
  void Stop();
  void Seek(int ms);
  void SetVolume(int v);  // 0-1000
  void SetMode(PlayMode m);
  void SetPlaylist(const QList<Track>& tracks, int startIndex = 0);
  // 曲库同步：整表替换播放列表但不打断正在放的歌（按路径找回位置；歌已不在曲库就停）
  void SetLibraryTracks(const QList<Track>& tracks, const QString& keepPath = QString());
  void AddLocalFiles(const QStringList& paths);
  // 搜索/歌单结果直接接管播放列表并开播
  void PlayTracks(const QList<Track>& tracks);
  void AppendTracks(const QList<Track>& tracks);
  void ClearPlaylist();

  // ---- 状态查询（界面每帧拉取即可，不依赖信号）
  bool Playing() const;
  bool Loading() const { return loading_; }
  int Position() const;
  int Duration() const;
  int Volume() const { return volume_; }
  PlayMode Mode() const { return mode_; }
  int CurrentIndex() const { return index_; }
  const QList<Track>& Playlist() const { return tracks_; }
  const Track* Current() const;

  // ---- 元信息补取：直链为空时向音源要，音源挂了就降级到本地
 signals:
  void TrackChanged();
  void StateChanged();     // 播放/暂停/缓冲/位置长度变化
  void PlaylistChanged();
  void Error(const QString& message);
  void LyricsReady(const QString& lrcText);
  void Degraded(const QString& reason);  // 在线音源不可用，已自动切本地

 private:
  MusicPlayer();
  ~MusicPlayer() override;

  void StartCurrent();
  void ResolveStream(const Track& t);
  void FetchLyrics(const Track& t);
  void ApplyModeOnEnded();
  void OnSourceDegraded(const QString& reason);
  bool PlayFirstLocal(QObject* avoid);  // 降级时把本地歌接上，返回是否真的接上了
  int NextIndex(bool forward) const;

  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audio_ = nullptr;
  QNetworkAccessManager* net_ = nullptr;
  QList<Track> tracks_;
  int index_ = -1;
  int volume_ = 700;
  PlayMode mode_ = PlayMode::Sequential;
  bool loading_ = false;
  int resolveGen_ = 0;  // 防止过期的直链回调覆盖新歌
  bool inFallback_ = false;  // 正在做一次本地兜底，防止连锁失败死循环
};

}  // namespace wd
