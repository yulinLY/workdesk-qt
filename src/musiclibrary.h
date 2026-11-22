#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace wd {

// 曲库里的一段音乐文件夹：path 是磁盘目录，tracks 是该目录收进来的音频绝对路径。
struct MusicFolder {
  QString path;
  QStringList tracks;
};

// 本地曲库：一组音乐文件夹 + 每个文件夹里的歌。
// 结构存 %APPDATA%\WorkDesk\music.json，下次打开照旧（WORKDESK_MUSIC_JSON 可改路径，便于自测/便携）。
class MusicLibrary : public QObject {
  Q_OBJECT
 public:
  static MusicLibrary& Instance();
  static QString Path();

  void Load();  // 读盘；磁盘上已经没了的文件顺手剔掉
  bool Save() const;

  const QList<MusicFolder>& Folders() const { return folders_; }
  QStringList AllTracks() const;              // 按分组顺序展平
  int FolderIndex(const QString& dir) const;  // 没找到返回 -1
  bool HasFolder(const QString& dir) const { return FolderIndex(dir) >= 0; }
  QString FolderOf(const QString& trackPath) const;  // 空=不在曲库里

  // 导入文件夹：递归扫描（子目录里的歌也收）；已在库里就原地重扫。返回该文件夹曲目数
  int ImportFolder(const QString& dir);
  // 单曲入库到 dir：文件不在该目录时先复制过去，返回最终路径（失败返回空）
  QString ImportFileInto(const QString& file, const QString& dir);
  // 拖入/命令行送来的单曲：只收这几首，父目录不在库里就以父目录为名新建一组（不递归扫别人）
  QStringList AddTrackFiles(const QStringList& files);
  void RemoveFolder(const QString& dir);   // 只移出曲库，不删磁盘文件
  void RescanFolder(const QString& dir);   // 外面增删过文件后重扫

 signals:
  void Changed();

 private:
  MusicLibrary() = default;

  bool ScanInto(MusicFolder& folder) const;  // 重扫 folder.tracks，返回是否有变化
  static QStringList ScanDir(const QString& dir);  // 递归找音频并排序
  static void SortTracks(QStringList& tracks);
  void Touch();  // 存盘 + 通知

  QList<MusicFolder> folders_;
};

}  // namespace wd