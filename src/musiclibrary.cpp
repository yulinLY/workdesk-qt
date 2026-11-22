#include "musiclibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "musicprovider.h"
#include "util.h"

using json = nlohmann::json;

namespace wd {
namespace {

// 统一成「正斜杠 + 无尾斜杠」的绝对路径，方便比较
QString NormDir(const QString& dir) {
  QString d = QDir::fromNativeSeparators(dir.trimmed());
  while (d.size() > 1 && d.endsWith(QLatin1Char('/'))) d.chop(1);
  return QDir::cleanPath(d);
}

bool SameDir(const QString& a, const QString& b) {
  return NormDir(a).compare(NormDir(b), Qt::CaseInsensitive) == 0;
}

}  // namespace

MusicLibrary& MusicLibrary::Instance() {
  static MusicLibrary lib;
  return lib;
}

QString MusicLibrary::Path() {
  // 自测/便携：显式给了路径就用它，否则跟 config.json 一样落在 %APPDATA%\WorkDesk 下
  const QString env = qEnvironmentVariable("WORKDESK_MUSIC_JSON").trimmed();
  if (!env.isEmpty()) return env;
  return QString::fromStdWString(AppDataDir()) + QStringLiteral("/music.json");
}

void MusicLibrary::SortTracks(QStringList& tracks) {
  QStringList uniq;
  for (const QString& t : tracks) {
    bool dup = false;
    for (const QString& u : uniq) {
      if (u.compare(t, Qt::CaseInsensitive) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup) uniq << t;
  }
  std::sort(uniq.begin(), uniq.end(), [](const QString& a, const QString& b) {
    const QString an = QFileInfo(a).completeBaseName();
    const QString bn = QFileInfo(b).completeBaseName();
    const int c = an.compare(bn, Qt::CaseInsensitive);
    if (c != 0) return c < 0;
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  tracks = uniq;
}

QStringList MusicLibrary::ScanDir(const QString& dir) {
  QStringList out;
  if (dir.isEmpty() || !QFileInfo(dir).isDir()) return out;
  QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString p = it.next();
    if (IsAudioFile(p)) out << QDir::fromNativeSeparators(QFileInfo(p).absoluteFilePath());
  }
  SortTracks(out);
  return out;
}

void MusicLibrary::Load() {
  folders_.clear();
  QFile f(Path());
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
  const QByteArray raw = f.readAll();
  f.close();

  json j;
  try {
    j = json::parse(raw.toStdString());
  } catch (...) {
    return;  // 坏文件当空库，不覆盖盘上的原文件
  }
  if (!j.contains("folders") || !j["folders"].is_array()) return;

  for (const auto& o : j["folders"]) {
    if (!o.is_object()) continue;
    MusicFolder mf;
    if (o.contains("path") && o["path"].is_string()) {
      mf.path = NormDir(QString::fromStdString(o["path"].get<std::string>()));
    }
    if (mf.path.isEmpty()) continue;
    if (o.contains("tracks") && o["tracks"].is_array()) {
      for (const auto& t : o["tracks"]) {
        if (!t.is_string()) continue;
        const QString p = QString::fromStdString(t.get<std::string>());
        // 盘上已经删掉的歌不再列出来，避免列表里全是打不开的死链
        if (!p.isEmpty() && QFileInfo::exists(p)) mf.tracks << QDir::fromNativeSeparators(p);
      }
    }
    SortTracks(mf.tracks);
    folders_ << mf;
  }
}

bool MusicLibrary::Save() const {
  const QString p = Path();
  const QString parent = QFileInfo(p).absolutePath();
  if (!parent.isEmpty()) QDir().mkpath(parent);

  json arr = json::array();
  for (const MusicFolder& mf : folders_) {
    json o;
    o["path"] = mf.path.toStdString();
    json ts = json::array();
    for (const QString& t : mf.tracks) ts.push_back(t.toStdString());
    o["tracks"] = ts;
    arr.push_back(o);
  }
  json root;
  root["version"] = 1;
  root["folders"] = arr;

  QFile f(p);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  const std::string text = root.dump(2);
  f.write(text.data(), (qint64)text.size());
  f.close();
  return true;
}

void MusicLibrary::Touch() {
  Save();
  emit Changed();
}

QStringList MusicLibrary::AllTracks() const {
  QStringList out;
  for (const MusicFolder& mf : folders_) out << mf.tracks;
  return out;
}

int MusicLibrary::FolderIndex(const QString& dir) const {
  const QString d = NormDir(dir);
  for (int i = 0; i < folders_.size(); ++i) {
    if (folders_.at(i).path.compare(d, Qt::CaseInsensitive) == 0) return i;
  }
  return -1;
}

QString MusicLibrary::FolderOf(const QString& trackPath) const {
  const QString dir = QDir::fromNativeSeparators(QFileInfo(trackPath).absolutePath());
  for (const MusicFolder& mf : folders_) {
    // 递归扫描进来的子目录歌曲也算在本分组里
    if (SameDir(dir, mf.path)) return mf.path;
    if (dir.startsWith(mf.path + QLatin1Char('/'), Qt::CaseInsensitive)) return mf.path;
  }
  return QString();
}

int MusicLibrary::ImportFolder(const QString& dir) {
  const QString d = NormDir(dir);
  if (d.isEmpty() || !QFileInfo(d).isDir()) return 0;
  int idx = FolderIndex(d);
  if (idx < 0) {
    MusicFolder mf;
    mf.path = d;
    folders_ << mf;
    idx = folders_.size() - 1;
  }
  folders_[idx].tracks = ScanDir(d);
  Touch();
  return folders_.at(idx).tracks.size();
}

QString MusicLibrary::ImportFileInto(const QString& file, const QString& dir) {
  const QFileInfo fi(file);
  const QString d = NormDir(dir);
  if (!fi.isFile() || d.isEmpty()) return QString();
  if (!QDir().mkpath(d)) return QString();

  QString target = QDir::fromNativeSeparators(fi.absoluteFilePath());
  if (!SameDir(fi.absolutePath(), d)) {
    target = d + QLatin1Char('/') + fi.fileName();
    if (!QFileInfo::exists(target)) {
      if (!QFile::copy(fi.absoluteFilePath(), target)) return QString();
    }
  }

  int idx = FolderIndex(d);
  if (idx < 0) {
    MusicFolder mf;
    mf.path = d;
    folders_ << mf;
    idx = folders_.size() - 1;
  }
  QStringList& list = folders_[idx].tracks;
  bool found = false;
  for (const QString& t : list) {
    if (t.compare(target, Qt::CaseInsensitive) == 0) {
      found = true;
      break;
    }
  }
  if (!found) list << target;
  SortTracks(list);
  Touch();
  return target;
}

QStringList MusicLibrary::AddTrackFiles(const QStringList& files) {
  QStringList added;
  bool touched = false;
  for (const QString& raw : files) {
    const QFileInfo fi(raw);
    if (!fi.isFile() || !IsAudioFile(fi.absoluteFilePath())) continue;
    const QString d = NormDir(fi.absolutePath());
    int idx = FolderIndex(d);
    if (idx < 0) {
      MusicFolder mf;
      mf.path = d;
      folders_ << mf;
      idx = folders_.size() - 1;
      touched = true;
    }
    const QString track = QDir::fromNativeSeparators(fi.absoluteFilePath());
    QStringList& list = folders_[idx].tracks;
    bool found = false;
    for (const QString& t : list) {
      if (t.compare(track, Qt::CaseInsensitive) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      list << track;
      SortTracks(list);
      added << track;
      touched = true;
    }
  }
  if (touched) Touch();
  return added;
}

void MusicLibrary::RemoveFolder(const QString& dir) {
  const int idx = FolderIndex(dir);
  if (idx < 0) return;
  folders_.removeAt(idx);
  Touch();
}

void MusicLibrary::RescanFolder(const QString& dir) {
  const int idx = FolderIndex(dir);
  if (idx < 0) return;
  if (ScanInto(folders_[idx])) Touch();
}

bool MusicLibrary::ScanInto(MusicFolder& folder) const {
  const QStringList now = ScanDir(folder.path);
  if (now.size() == folder.tracks.size()) {
    bool same = true;
    for (int i = 0; i < now.size(); ++i) {
      if (now.at(i).compare(folder.tracks.at(i), Qt::CaseInsensitive) != 0) {
        same = false;
        break;
      }
    }
    if (same) return false;
  }
  folder.tracks = now;
  return true;
}

}  // namespace wd