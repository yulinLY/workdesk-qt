#include "musicprovider.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <cstdarg>
#include <cstdio>

#include "wdtrace.h"

namespace wd {
namespace {

const char* const kAudioExts[] = {"mp3", "flac", "wav", "m4a", "aac", "ogg",
                                  "opus", "wma", "ape", "aiff", "aif"};

bool HasAudioExt(const QString& path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  for (const char* e : kAudioExts) {
    if (ext == QLatin1String(e)) return true;
  }
  return false;
}

void Tracef(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Trace(buf);
}

}  // namespace

bool IsAudioFile(const QString& path) { return HasAudioExt(path); }

// ================================================================ 本地音源
QStringList LocalMusicSource::ScanDirs() const {
  if (!dirs_.isEmpty()) return dirs_;
  QStringList dirs;
  const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  if (!music.isEmpty() && QFileInfo(music).isDir()) dirs << music;
  const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  if (!home.isEmpty()) {
    const QStringList extra{QStringLiteral("/Desktop"), QStringLiteral("/Downloads"),
                            QStringLiteral("/Music"), QStringLiteral("/桌面"),
                            QStringLiteral("/下载"), QStringLiteral("/音乐")};
    for (const QString& sub : extra) {
      const QString p = home + sub;
      if (QFileInfo(p).isDir() && !dirs.contains(p)) dirs << p;
    }
  }
  return dirs;
}

void LocalMusicSource::Search(const QString& keyword, SearchCallback cb) {
  const QString kw = keyword.trimmed();
  QList<MusicHit> hits;
  QSet<QString> seen;
  auto push = [&](const QString& path) {
    if (path.isEmpty() || seen.contains(path)) return;
    if (!HasAudioExt(path)) return;
    if (!kw.isEmpty() && !path.contains(kw, Qt::CaseInsensitive)) return;
    seen.insert(path);
    MusicHit h;
    h.id = path;
    h.localPath = path;
    h.local = true;
    h.title = QFileInfo(path).completeBaseName();
    h.sourceId = QStringLiteral("local");
    hits << h;
  };
  // 先加入当前播放列表
  for (const Track& t : MusicPlayer::Instance().Playlist()) {
    if (t.local || !t.localPath.isEmpty()) push(t.localPath);
  }
  for (const QString& dir : ScanDirs()) {
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    int guard = 0;
    while (it.hasNext() && guard < 1200) {
      ++guard;
      push(it.next());
    }
  }
  Ok();
  if (cb) cb(true, hits, QString());
}

void LocalMusicSource::Resolve(const Track& track, UrlCallback cb) {
  const QString path = !track.localPath.isEmpty() ? track.localPath : track.streamUrl;
  if (path.isEmpty()) {
    if (cb) cb(false, QString(), QStringLiteral("本地音源没有这首歌的地址"));
    return;
  }
  if (QFileInfo::exists(path)) {
    Ok();
    if (cb) cb(true, QUrl::fromLocalFile(path).toString(), QString());
    return;
  }
  if (path.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
    if (cb) cb(true, path, QString());
    return;
  }
  if (cb) cb(false, QString(), QStringLiteral("本地音源没有这首歌的地址"));
}

void LocalMusicSource::FetchLyric(const Track& track, LyricCallback cb) {
  const QString path = !track.localPath.isEmpty() ? track.localPath : track.streamUrl;
  if (path.isEmpty()) {
    if (cb) cb(false, QString(), QStringLiteral("本地文件没有歌词"));
    return;
  }
  QFileInfo fi(path);
  QStringList cand;
  cand << fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".lrc")
       << fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".LRC");
  for (const QString& lrc : cand) {
    QFile f(lrc);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) continue;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    if (!text.trimmed().isEmpty()) {
      Ok();
      if (cb) cb(true, text, QString());
      return;
    }
  }
  if (cb) cb(false, QString(), QStringLiteral("本地文件没有同名歌词"));
}

// ================================================================ 注册表
MusicSourceHub& MusicSourceHub::Instance() {
  static MusicSourceHub hub;
  return hub;
}

MusicSourceHub::MusicSourceHub() = default;
MusicSourceHub::~MusicSourceHub() = default;

void MusicSourceHub::Init() {
  if (inited_) return;
  inited_ = true;
  local_ = new LocalMusicSource(this);
  Tracef("music: 音源只有本地文件（音乐文件夹 / 桌面 / 下载）");
}

QList<MusicSource*> MusicSourceHub::Sources() const {
  QList<MusicSource*> out;
  if (local_) out << local_;
  return out;
}

MusicSource* MusicSourceHub::Active() const { return local_; }

QString MusicSourceHub::StatusLine() const {
  return QStringLiteral("本地文件 · 音乐文件夹 / 桌面 / 下载");
}

void MusicSourceHub::Search(const QString& keyword, SearchCallback cb) {
  if (!local_) {
    if (cb) cb(false, {}, QStringLiteral("本地音源还没初始化"));
    return;
  }
  local_->Search(keyword, cb);
}

void MusicSourceHub::Resolve(const Track& track, UrlCallback cb) {
  if (!local_) {
    if (cb) cb(false, QString(), QStringLiteral("本地音源还没初始化"));
    return;
  }
  local_->Resolve(track, cb);
}

void MusicSourceHub::FetchLyric(const Track& track, LyricCallback cb) {
  if (!local_) {
    if (cb) cb(false, QString(), QStringLiteral("本地音源还没初始化"));
    return;
  }
  local_->FetchLyric(track, cb);
}

}  // namespace wd
