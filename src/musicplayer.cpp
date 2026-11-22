#include "musicplayer.h"

#include "musicprovider.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>

namespace wd {
namespace {

// 代理返回的 JSON 各家写法不一样，这里把常见的几种都认一遍：
//   {"url": "..."}、{"data": {"url": "..."}}、{"data": "..."}
//   {"song": {"play_url": "..."}}
QString ParseStreamUrl(const QByteArray& body) {
  const QJsonDocument doc = QJsonDocument::fromJson(body);
  if (!doc.isObject()) return QString();
  const QJsonObject root = doc.object();
  const char* keys[] = {"url", "src", "play_url", "streamUrl", "stream_url"};
  for (const char* k : keys) {
    const QJsonValue v = root.value(QLatin1String(k));
    if (v.isString() && !v.toString().isEmpty()) return v.toString();
  }
  const QJsonValue data = root.value(QStringLiteral("data"));
  if (data.isString() && !data.toString().isEmpty()) return data.toString();
  if (data.isObject()) {
    const QJsonObject d = data.toObject();
    for (const char* k : keys) {
      const QJsonValue v = d.value(QLatin1String(k));
      if (v.isString() && !v.toString().isEmpty()) return v.toString();
    }
  }
  const QJsonObject song = root.value(QStringLiteral("song")).toObject();
  for (const char* k : keys) {
    const QJsonValue v = song.value(QLatin1String(k));
    if (v.isString() && !v.toString().isEmpty()) return v.toString();
  }
  return QString();
}

// 本地路径统一转成 QUrl：已经是 file:// 的不再二次包一层
QUrl ToPlayableUrl(const QString& s, bool local) {
  if (s.isEmpty()) return QUrl();
  const QUrl u(s);
  if (u.isValid() && !u.scheme().isEmpty()) return u;
  return local ? QUrl::fromLocalFile(s) : u;
}

}  // namespace

MusicPlayer& MusicPlayer::Instance() {
  static MusicPlayer inst;
  return inst;
}

// 对象全部自己养（不给 this 当父），因为单例是静态析构，
// 万一比 QApplication 活得久，Qt 多媒体内部对象就不能再安全释放了。
MusicPlayer::MusicPlayer()
    : player_(new QMediaPlayer(nullptr)),
      audio_(new QAudioOutput(nullptr)),
      net_(new QNetworkAccessManager(nullptr)) {
  player_->setAudioOutput(audio_);
  audio_->setVolume(volume_ / 1000.0);

  connect(player_, &QMediaPlayer::playbackStateChanged, this,
          [this](QMediaPlayer::PlaybackState) { emit StateChanged(); });
  connect(player_, &QMediaPlayer::positionChanged, this,
          [this](qint64) { emit StateChanged(); });
  connect(player_, &QMediaPlayer::durationChanged, this,
          [this](qint64) { emit StateChanged(); });
  connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
    switch (s) {
      case QMediaPlayer::LoadedMedia:
      case QMediaPlayer::BufferedMedia:
        if (loading_) {
          loading_ = false;
          emit StateChanged();
        }
        break;
      case QMediaPlayer::EndOfMedia:
        ApplyModeOnEnded();
        break;
      case QMediaPlayer::InvalidMedia:
        loading_ = false;
        emit StateChanged();
        break;
      default:
        break;
    }
  });
  connect(player_, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString& msg) {
            loading_ = false;
            emit Error(msg.isEmpty() ? QStringLiteral("播放失败") : msg);
            emit StateChanged();
          });

  // 音源注册表在这里初始化：界面只跟播放器打交道，换音源对它透明。
  MusicSourceHub::Instance().Init();
  connect(&MusicSourceHub::Instance(), &MusicSourceHub::Degraded, this,
          &MusicPlayer::OnSourceDegraded);
}

MusicPlayer::~MusicPlayer() {
  ++resolveGen_;  // 让在途的网络回调全部作废
  if (player_) {
    player_->stop();
    player_->setSource(QUrl());
    player_->setAudioOutput(nullptr);
  }
  // 只有事件循环还在的时候才真正释放，否则交给进程退出回收
  if (qApp) {
    delete player_;
    delete audio_;
    delete net_;
  }
  player_ = nullptr;
  audio_ = nullptr;
  net_ = nullptr;
}

// ------------------------------------------------------------------ 播放控制
void MusicPlayer::PlayAt(int index) {
  if (index < 0 || index >= tracks_.size()) return;
  index_ = index;
  StartCurrent();
}

void MusicPlayer::PlayPause() {
  if (tracks_.isEmpty()) return;
  if (index_ < 0 || index_ >= tracks_.size()) index_ = 0;
  const QMediaPlayer::PlaybackState st = player_ ? player_->playbackState()
                                                : QMediaPlayer::StoppedState;
  if (st == QMediaPlayer::PlayingState) {
    player_->pause();
    return;
  }
  if (st == QMediaPlayer::PausedState && player_ && player_->source().isValid()) {
    player_->play();
    return;
  }
  StartCurrent();
}

void MusicPlayer::Next() {
  const int i = NextIndex(true);
  if (i < 0) return;
  index_ = i;
  StartCurrent();
}

void MusicPlayer::Prev() {
  // 播了 3 秒以上按「回到开头」，否则才切上一首，跟常见播放器一致
  if (player_ && player_->position() > 3000 && player_->source().isValid()) {
    player_->setPosition(0);
    return;
  }
  const int i = NextIndex(false);
  if (i < 0) return;
  index_ = i;
  StartCurrent();
}

void MusicPlayer::Stop() {
  ++resolveGen_;
  loading_ = false;
  if (player_) {
    player_->stop();
    player_->setSource(QUrl());
  }
  emit StateChanged();
}

void MusicPlayer::Seek(int ms) {
  if (!player_ || !player_->source().isValid()) return;
  player_->setPosition(qMax(0, ms));
}

void MusicPlayer::SetVolume(int v) {
  volume_ = qBound(0, v, 1000);
  if (audio_) audio_->setVolume(volume_ / 1000.0);
  emit StateChanged();
}

void MusicPlayer::SetMode(PlayMode m) {
  mode_ = m;
  emit StateChanged();
}

void MusicPlayer::SetPlaylist(const QList<Track>& tracks, int startIndex) {
  tracks_ = tracks;
  if (tracks_.isEmpty()) {
    index_ = -1;
    Stop();
    emit PlaylistChanged();
    return;
  }
  index_ = qBound(0, startIndex, int(tracks_.size()) - 1);
  emit PlaylistChanged();
  StartCurrent();
}

Track TrackFromLocalFile(const QString& path) {
  const QFileInfo fi(path);
  Track t;
  t.local = true;
  t.streamUrl = fi.absoluteFilePath();
  t.localPath = fi.absoluteFilePath();  // 降级 / 歌词找的是这个绝对路径
  const QString base = fi.completeBaseName().trimmed();
  const int sep = base.indexOf(QStringLiteral(" - "));
  if (sep > 0) {
    t.artist = base.left(sep).trimmed();
    t.title = base.mid(sep + 3).trimmed();
  } else {
    t.title = base;
  }
  t.album = fi.absoluteDir().dirName();
  return t;
}

void MusicPlayer::AddLocalFiles(const QStringList& paths) {
  if (paths.isEmpty()) return;
  const bool wasEmpty = tracks_.isEmpty();
  for (const QString& p : paths) {
    const QFileInfo fi(p);
    if (!fi.exists() || !fi.isFile()) continue;
    tracks_ << TrackFromLocalFile(fi.absoluteFilePath());
  }
  if (tracks_.size() == 0) return;
  emit PlaylistChanged();
  if (wasEmpty) {
    index_ = 0;
    StartCurrent();
  }
}

// ------------------------------------------------------------------ 播放列表
void MusicPlayer::SetLibraryTracks(const QList<Track>& tracks, const QString& keepPath) {
  QString keep = keepPath;
  if (keep.isEmpty()) {
    const Track* cur = Current();
    if (cur) keep = cur->localPath;
  }
  tracks_ = tracks;

  int idx = -1;
  if (!keep.isEmpty()) {
    for (int i = 0; i < tracks_.size(); ++i) {
      if (tracks_.at(i).localPath.compare(keep, Qt::CaseInsensitive) == 0) {
        idx = i;
        break;
      }
    }
  }

  if (tracks_.isEmpty()) {
    index_ = -1;
    Stop();
  } else if (idx >= 0) {
    index_ = idx;  // 同一首歌接着放，不从头开始
  } else if (!keep.isEmpty()) {
    index_ = -1;  // 正在放的这首已经不在曲库里了（如文件夹被移出），停掉
    Stop();
  } else if (index_ >= tracks_.size()) {
    index_ = -1;
  }
  emit PlaylistChanged();
}

void MusicPlayer::PlayTracks(const QList<Track>& tracks) { SetPlaylist(tracks, 0); }

void MusicPlayer::AppendTracks(const QList<Track>& tracks) {
  if (tracks.isEmpty()) return;
  const bool wasEmpty = tracks_.isEmpty();
  tracks_ += tracks;
  emit PlaylistChanged();
  if (wasEmpty) {
    index_ = 0;
    StartCurrent();
  }
}

void MusicPlayer::ClearPlaylist() {
  tracks_.clear();
  index_ = -1;
  Stop();
  emit PlaylistChanged();
}

// ------------------------------------------------------------------ 状态查询
bool MusicPlayer::Playing() const {
  return player_ && player_->playbackState() == QMediaPlayer::PlayingState;
}

int MusicPlayer::Position() const { return player_ ? int(player_->position()) : 0; }

int MusicPlayer::Duration() const { return player_ ? int(player_->duration()) : 0; }

const Track* MusicPlayer::Current() const {
  if (index_ < 0 || index_ >= tracks_.size()) return nullptr;
  return &tracks_.at(index_);
}

// ------------------------------------------------------------------ 内部实现
void MusicPlayer::StartCurrent() {
  if (index_ < 0 || index_ >= tracks_.size()) return;
  const Track& t = tracks_.at(index_);
  ++resolveGen_;  // 换歌即作废上一首的异步回调
  loading_ = false;
  emit TrackChanged();
  FetchLyrics(t);

  if (!t.streamUrl.isEmpty()) {
    inFallback_ = false;
    const QUrl url = ToPlayableUrl(t.streamUrl, t.local);
    if (!url.isValid() || !player_) {
      emit Error(QStringLiteral("这个地址播不了"));
      return;
    }
    loading_ = true;
    player_->setSource(url);
    player_->play();
    emit StateChanged();
    return;
  }
  // 直链为空：交给音源适配器现取（本地文件 / 社区 API / 官方），失败自动降级本地
  if (!t.providerId.isEmpty() || !t.localPath.isEmpty()) {
    loading_ = true;
    emit StateChanged();
    ResolveStream(t);
    return;
  }
  emit Error(QStringLiteral("这首歌没有可播放的地址"));
}

void MusicPlayer::ResolveStream(const Track& t) {
  const int gen = resolveGen_;
  const bool wantLocal = t.local || !t.localPath.isEmpty();
  MusicSourceHub::Instance().Resolve(
      t, [this, gen, wantLocal](bool ok, const QString& url, const QString& err) {
        if (gen != resolveGen_) return;  // 已经换歌，这次结果丢掉
        loading_ = false;
        if (!ok) {
          emit Error(err.isEmpty() ? QStringLiteral("这首歌暂时播不了") : err);
          emit StateChanged();
          // 在线取流失败：把列表里的本地歌接上，界面不能停在空白
          if (!wantLocal) PlayFirstLocal(this);
          return;
        }
        const bool isFile = url.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive);
        if (index_ >= 0 && index_ < tracks_.size() && !isFile)
          tracks_[index_].streamUrl = url;  // 直链缓存，重播不再问音源
        if (player_) {
          player_->setSource(ToPlayableUrl(url, isFile));
          player_->play();
        }
        emit StateChanged();
      });
}

bool MusicPlayer::PlayFirstLocal(QObject* avoid) {
  Q_UNUSED(avoid);
  if (inFallback_) return false;  // 兜底只走一层，避免连锁失败刷屏
  for (int i = 0; i < tracks_.size(); ++i) {
    if (i == index_) continue;
    const Track& c = tracks_.at(i);
    if (!c.local && c.localPath.isEmpty()) continue;
    inFallback_ = true;
    index_ = i;
    StartCurrent();
    emit StateChanged();
    return true;
  }
  return false;
}

void MusicPlayer::OnSourceDegraded(const QString& reason) { emit Degraded(reason); }


void MusicPlayer::FetchLyrics(const Track& t) {
  const int gen = resolveGen_;
  // 1) 显式给了歌词地址就直连抓（外部塞进来的 lrc 链接）
  if (!t.lyricsUrl.isEmpty() && net_) {
    const QUrl url(t.lyricsUrl);
    if (url.isValid()) {
      QNetworkRequest req(url);
      req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WorkDesk/1.0"));
      QNetworkReply* reply = net_->get(req);
      connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != resolveGen_) return;
        if (reply->error() != QNetworkReply::NoError) return;
        emit LyricsReady(QString::fromUtf8(reply->readAll()));
      });
      return;
    }
  }
  // 2) 否则让音源去找：本地同名 .lrc（新音源里只有本地有歌词，其余返回空）
  MusicSourceHub::Instance().FetchLyric(
      t, [this, gen](bool ok, const QString& lrc, const QString& err) {
        Q_UNUSED(err);
        if (gen != resolveGen_ || !ok) return;
        emit LyricsReady(lrc);
      });
}

void MusicPlayer::ApplyModeOnEnded() {
  loading_ = false;
  if (tracks_.isEmpty()) return;
  if (mode_ == PlayMode::Single || tracks_.size() == 1) {
    if (player_) {
      player_->setPosition(0);
      player_->play();
    }
    return;
  }
  if (mode_ == PlayMode::Shuffle) {
    int r = index_;
    while (r == index_ && tracks_.size() > 1)
      r = QRandomGenerator::global()->bounded(int(tracks_.size()));
    index_ = r;
    StartCurrent();
    return;
  }
  // 顺序模式：放到最后一首就停住，不再绕回去
  if (index_ + 1 >= tracks_.size()) {
    if (player_) player_->stop();
    emit StateChanged();
    return;
  }
  index_ += 1;
  StartCurrent();
}

int MusicPlayer::NextIndex(bool forward) const {
  const int n = tracks_.size();
  if (n == 0) return -1;
  if (n == 1) return 0;
  if (mode_ == PlayMode::Shuffle) {
    int r = index_;
    while (r == index_) r = QRandomGenerator::global()->bounded(int(n));
    return r;
  }
  int i = index_ + (forward ? 1 : -1);
  if (i < 0) i = n - 1;  // 手动切歌绕到最后 / 最前，循环手感更顺
  if (i >= n) i = 0;
  return i;
}

}  // namespace wd
