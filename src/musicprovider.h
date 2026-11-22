#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

#include "musicplayer.h"

namespace wd {

// 按扩展名判断是不是音频文件（曲库扫描、拖放、导入都用这一份判断）
bool IsAudioFile(const QString& path);

// 一条搜索结果。本地音源里 id 就是文件路径。
struct MusicHit {
  QString id;
  QString title;
  QString artist;
  QString album;
  QString coverUrl;
  QString lyricsUrl;
  QString sourceId;
  bool local = false;
  QString localPath;
};

using SearchCallback =
    std::function<void(bool ok, const QList<MusicHit>& hits, const QString& error)>;
using UrlCallback = std::function<void(bool ok, const QString& url, const QString& error)>;
using LyricCallback = std::function<void(bool ok, const QString& lrc, const QString& error)>;

// 音源适配器基类。当前只有本地文件音源，在线音源已经全部摘掉。
class MusicSource : public QObject {
  Q_OBJECT
 public:
  explicit MusicSource(QObject* parent = nullptr) : QObject(parent) {}
  ~MusicSource() override = default;

  virtual QString Id() const = 0;
  virtual QString Label() const = 0;

  virtual void Search(const QString& keyword, SearchCallback cb) {
    Q_UNUSED(keyword);
    Q_UNUSED(cb);
  }
  virtual void Resolve(const Track& track, UrlCallback cb) {
    Q_UNUSED(track);
    Q_UNUSED(cb);
  }
  virtual void FetchLyric(const Track& track, LyricCallback cb) {
    Q_UNUSED(track);
    Q_UNUSED(cb);
  }

  QString LastError() const { return lastError_; }
  void ClearError() { lastError_.clear(); }

 signals:
  void AvailabilityChanged();

 protected:
  void Fail(const QString& why) {
    lastError_ = why;
    emit AvailabilityChanged();
  }
  void Ok() {
    if (!lastError_.isEmpty()) {
      lastError_.clear();
      emit AvailabilityChanged();
    }
  }

 private:
  QString lastError_;
};

// 本地音源：搜索就是过滤当前播放列表 + 扫一遍音乐目录；直链是 file://。
class LocalMusicSource : public MusicSource {
  Q_OBJECT
 public:
  explicit LocalMusicSource(QObject* parent = nullptr) : MusicSource(parent) {}
  QString Id() const override { return QStringLiteral("local"); }
  QString Label() const override { return QStringLiteral("本地文件"); }

  void Search(const QString& keyword, SearchCallback cb) override;
  void Resolve(const Track& track, UrlCallback cb) override;
  void FetchLyric(const Track& track, LyricCallback cb) override;

  QStringList ScanDirs() const;
  void SetScanDirs(const QStringList& dirs) { dirs_ = dirs; }

 private:
  QStringList dirs_;
};

// 音源注册表：现在只挂本地文件音源，上层照旧只跟它打交道。
class MusicSourceHub : public QObject {
  Q_OBJECT
 public:
  static MusicSourceHub& Instance();

  void Init();
  QList<MusicSource*> Sources() const;
  MusicSource* Active() const;
  QString ActiveId() const { return QStringLiteral("local"); }

  void Search(const QString& keyword, SearchCallback cb);
  void Resolve(const Track& track, UrlCallback cb);
  void FetchLyric(const Track& track, LyricCallback cb);

  QString StatusLine() const;  // 一行状态，界面上直接显示

 signals:
  void ActiveChanged(const QString& id, const QString& label);
  void Degraded(const QString& reason);
  void StatusChanged();

 private:
  MusicSourceHub();
  ~MusicSourceHub() override;

  LocalMusicSource* local_ = nullptr;
  bool inited_ = false;
};

}  // namespace wd
