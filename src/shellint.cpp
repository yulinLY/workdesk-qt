#include "shellint.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

namespace wd {
namespace {

const char* kUninstallKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WorkDesk";
const char* kAppKey = "HKEY_CURRENT_USER\\Software\\WorkDesk";

// 纯绿模式：exe 旁边有 portable.txt，或显式设了环境变量，就完全不碰注册表
bool PortableMode() {
  if (!qEnvironmentVariableIsEmpty("WORKDESK_NO_REGISTRY")) return true;
  const QString flag =
      QCoreApplication::applicationDirPath() + QStringLiteral("/portable.txt");
  return QFileInfo::exists(flag);
}

qint64 DirSizeKb(const QString& dir) {
  qint64 bytes = 0;
  QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    bytes += it.fileInfo().size();
  }
  return bytes / 1024;
}

void DeleteKey(const QString& key) {
  QProcess::execute(QStringLiteral("reg"),
                    {QStringLiteral("delete"), key, QStringLiteral("/f")});
}

}  // namespace

bool RegisterUninstallEntry() {
  if (PortableMode()) return false;

  const QString exe = QCoreApplication::applicationFilePath();
  const QString dir = QCoreApplication::applicationDirPath();
  const QString uninst = dir + QStringLiteral("/Uninstall.exe");
  // 没有卸载程序就登记，等于给自己留个删不掉的条目，宁可不写
  if (!QFileInfo::exists(uninst)) return false;

  const QString nativeExe = QDir::toNativeSeparators(exe);
  const QString nativeDir = QDir::toNativeSeparators(dir);
  const QString nativeUninst = QDir::toNativeSeparators(uninst);

  QSettings st(QString::fromLatin1(kUninstallKey), QSettings::NativeFormat);
  st.setValue(QStringLiteral("DisplayName"), QStringLiteral("WorkDesk"));
  st.setValue(QStringLiteral("DisplayVersion"), QStringLiteral("1.1.2"));
  st.setValue(QStringLiteral("Publisher"), QStringLiteral("WorkDesk"));
  st.setValue(QStringLiteral("DisplayIcon"), nativeExe);
  st.setValue(QStringLiteral("InstallLocation"), nativeDir);
  // 卸载命令带引号，路径里有空格也能跑；静默版给批处理和第三方卸载器用
  st.setValue(QStringLiteral("UninstallString"),
              QStringLiteral("\"%1\"").arg(nativeUninst));
  st.setValue(QStringLiteral("QuietUninstallString"),
              QStringLiteral("\"%1\" --silent --purge-data").arg(nativeUninst));
  st.setValue(QStringLiteral("EstimatedSize"), int(DirSizeKb(dir)));
  st.setValue(QStringLiteral("InstallDate"),
              QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd")));
  st.setValue(QStringLiteral("NoModify"), 1);
  st.setValue(QStringLiteral("NoRepair"), 1);
  st.sync();
  return st.status() == QSettings::NoError;
}

void UnregisterUninstallEntry() {
  DeleteKey(QString::fromLatin1(kUninstallKey));
  DeleteKey(QString::fromLatin1(kAppKey));
}

}  // namespace wd