// WorkDesk 卸载程序（Uninstall.exe）。
//
// 绿色软件没有安装器，所以卸载这事得自己带一个：
//   - 和 WorkDeskQt.exe 放在同一个目录，双击就能跑；
//   - 删掉安装目录里的全部文件（最后连自己一起删）；
//   - 按用户勾选删掉 %APPDATA%\WorkDesk 里的配置、聊天记录、曲库、日志；
//   - 顺手清理 HKCU\Software\WorkDesk 和桌面/开始菜单里带 WorkDesk 的快捷方式；
//   - 支持静默：Uninstall.exe --silent [--purge-data]，给批处理/自动化用。
//
// 关键设计：卸载程序自己也用 Qt，而 Qt 的 DLL 就躺在安装目录里——直接原地删，
// 这些 DLL 正被自己加载着，一个都删不掉（共享冲突）。所以先把自己连同必需的
// 运行时复制到 %TEMP% 下另起一个进程（--workdir 指向真正的安装目录），
// 由那个副本去删；副本删完后由 cmd 收尾，把临时目录和自身一并抹掉。
//
// 安全边界：装到 C:\Windows、盘符根目录这类位置不认，宁可拒绝也不乱删。

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "shellint.h"
#include "theme.h"

namespace {

// ------------------------------------------------------------------ 通用小工具
QString UserDataDir() {
  const QString appdata = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPDATA"));
  if (appdata.isEmpty()) return QString();
  return appdata + QStringLiteral("/WorkDesk");
}

qint64 PathSize(const QString& path) {
  QFileInfo fi(path);
  if (!fi.exists()) return 0;
  if (fi.isFile()) return fi.size();
  qint64 total = 0;
  QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}

QString Human(qint64 bytes) {
  if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
  if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
  return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

// 只读文件也能删：先补写权限再删，删不掉就如实返回 false
bool RemovePath(const QString& path) {
  QFileInfo fi(path);
  if (!fi.exists()) return true;
  if (fi.isDir()) return QDir(path).removeRecursively();
  if (!fi.isWritable()) {
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
  }
  return QFile::remove(path);
}

// 安装目录合理性检查：拒绝盘符根目录、Windows 目录这类「删了要出事」的位置
bool SafeInstallDir(const QString& dir, const QString& selfExe) {
  if (dir.isEmpty()) return false;
  const QString d = QDir::fromNativeSeparators(dir).toLower();
  if (d.length() < 8) return false;
  if (d.contains(QStringLiteral("/windows/"))) return false;
  if (d.contains(QStringLiteral("/program files"))) return false;
  const QString clean = QDir::cleanPath(dir);
  for (const QFileInfo& dr : QDir::drives()) {
    if (clean.compare(QDir::cleanPath(dr.absoluteFilePath()), Qt::CaseInsensitive) == 0) return false;
  }
  // 自己得真在，才说明这是个能自删的安装目录
  if (!QFileInfo::exists(selfExe)) return false;
  return true;
}

using Logger = std::function<void(const QString&)>;

// ------------------------------------------------------------------ 卸载主流程
// 返回删不掉的文件数量（0 表示干净）
int DoUninstall(const QString& installDir, const QString& selfExe, bool purgeData, const Logger& log) {
  int failed = 0;

  log(QStringLiteral("结束正在运行的 WorkDeskQt…"));
  QProcess::execute(QStringLiteral("taskkill"),
                    {QStringLiteral("/IM"), QStringLiteral("WorkDeskQt.exe"), QStringLiteral("/F")});

  log(QStringLiteral("清理安装目录：%1").arg(QDir::toNativeSeparators(installDir)));
  const QFileInfoList entries = QDir(installDir).entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
  for (const QFileInfo& fi : entries) {
    // 自己还占着句柄，最后交给外部的 cmd 删
    if (fi.absoluteFilePath().compare(selfExe, Qt::CaseInsensitive) == 0) continue;
    if (RemovePath(fi.absoluteFilePath())) {
      log(QStringLiteral("  已删除 %1").arg(fi.fileName()));
    } else {
      ++failed;
      log(QStringLiteral("  × 删不掉 %1（可能正被占用）").arg(fi.fileName()));
    }
  }

  if (purgeData) {
    const QString data = UserDataDir();
    if (!data.isEmpty() && QFileInfo::exists(data)) {
      log(QStringLiteral("删除设置、聊天记录与曲库：%1").arg(QDir::toNativeSeparators(data)));
      if (!RemovePath(data)) {
        ++failed;
        log(QStringLiteral("  × 用户数据删不干净，可能是某个文件正被占用"));
      }
    }
    // 清掉「程序和功能」里那条登记，以及程序自己的 HKCU\Software\WorkDesk
    wd::UnregisterUninstallEntry();
    log(QStringLiteral("已从「程序和功能」里移除 WorkDesk，并清理注册表项"));
  } else {
    log(QStringLiteral("按你的选择，保留 %APPDATA%\\WorkDesk 下的设置与数据"));
  }

  // 桌面和开始菜单里带 WorkDesk 的快捷方式
  QStringList places;
  const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
  if (!desktop.isEmpty()) places << desktop;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString appdata = env.value(QStringLiteral("APPDATA"));
  const QString profile = env.value(QStringLiteral("USERPROFILE"));
  if (!profile.isEmpty()) places << profile + QStringLiteral("/Desktop");
  if (!appdata.isEmpty())
    places << appdata + QStringLiteral("/Microsoft/Windows/Start Menu/Programs");
  places.removeDuplicates();

  // 按已知名字精确删，不遍历目录：列目录在个别环境下会拿到空结果（实测过），
  // 明确路径反而稳；没命中就什么都不做，不会误删别人的快捷方式。
  const QStringList names = {QStringLiteral("WorkDesk.lnk"), QStringLiteral("WorkDeskQt.lnk"),
                             QStringLiteral("Work Desk.lnk"), QStringLiteral("写字板.lnk")};
  for (const QString& place : places) {
    for (const QString& name : names) {
      const QString path = place + QLatin1Char('/') + name;
      if (!QFileInfo::exists(path)) continue;
      if (RemovePath(path))
        log(QStringLiteral("  已删除快捷方式 %1").arg(QDir::toNativeSeparators(path)));
      else
        log(QStringLiteral("  × 快捷方式删不掉：%1").arg(QDir::toNativeSeparators(path)));
    }
  }

  return failed;
}

// 让自己这个进程退出后，由 cmd 把 exe、安装目录（和临时副本目录）全部抹掉
void ScheduleSelfRemoval(const QString& installDir, const QString& selfExe,
                         const QStringList& extraDirs) {
  // 直接用 cmd 的整条命令行：先等本进程退出，再删 exe 与各个目录。
  // 走 CreateProcessW 而不是 QProcess，是为了让 cmd 原样收到这条命令行——
  // 交给 QProcess 拼接时 /c 和引号会被重新组合，实测 cmd 会把 /c 当成命令名。
  QString line = QStringLiteral("cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"%1\"")
                     .arg(QDir::toNativeSeparators(selfExe));
  // 进程刚退出时 DLL 句柄还可能没完全释放，等一拍、各目录连删两遍，别留个空壳目录
  for (const QString& d : extraDirs) {
    const QString nd = QDir::toNativeSeparators(d);
    line += QStringLiteral(" & ping -n 2 127.0.0.1 >nul & rmdir /s /q \"%1\" 2>nul").arg(nd);
    line += QStringLiteral(" & rmdir /s /q \"%1\" 2>nul").arg(nd);
  }
  const QString ni = QDir::toNativeSeparators(installDir);
  line += QStringLiteral(" & ping -n 2 127.0.0.1 >nul & rmdir /s /q \"%1\" 2>nul").arg(ni);
  line += QStringLiteral(" & rmdir /s /q \"%1\" 2>nul").arg(ni);

  std::wstring cmd = line.toStdWString();
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const BOOL ok =
      CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                     nullptr, &si, &pi);
  if (ok) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// 把「自己和跑起来需要的 Qt 运行时」复制到临时目录，返回副本路径（失败返回空）
// 这样原始安装目录里没有一个文件被本进程占用，删得干净。
QString RelocateToTemp() {
  const QString srcDir = QCoreApplication::applicationDirPath();
  const QString srcExe = QCoreApplication::applicationFilePath();
  const QString tmp =
      QDir::tempPath() + QStringLiteral("/WorkDesk-uninstall-%1").arg(QCoreApplication::applicationPid());
  QDir().mkpath(tmp + QStringLiteral("/platforms"));

  const QStringList needed = {QStringLiteral("Qt6Core.dll"), QStringLiteral("Qt6Gui.dll"),
                              QStringLiteral("Qt6Widgets.dll")};
  for (const QString& dll : needed) {
    const QString from = srcDir + QLatin1Char('/') + dll;
    if (!QFileInfo::exists(from)) return QString();
    if (!QFile::copy(from, tmp + QLatin1Char('/') + dll)) return QString();
  }
  // 平台插件：缺了它副本起不来（真缺就退回原地删，至少把能删的删掉并如实报告）
  const QString platFrom = srcDir + QStringLiteral("/platforms/qwindows.dll");
  if (QFileInfo::exists(platFrom)) QFile::copy(platFrom, tmp + QStringLiteral("/platforms/qwindows.dll"));
  else return QString();

  const QString tmpExe = tmp + QStringLiteral("/Uninstall.exe");
  if (!QFile::copy(srcExe, tmpExe)) return QString();
  return tmpExe;
}

// ------------------------------------------------------------------ 卸载窗口
class UninstallWindow : public QWidget {
 public:
  UninstallWindow(const QString& installDir, const QString& selfExe, const QStringList& cleanupDirs)
      : installDir_(installDir), selfExe_(selfExe), cleanupDirs_(cleanupDirs) {
    BuildUi();
    Append(QStringLiteral("准备卸载 WorkDesk。"));
    Append(QStringLiteral("安装目录：%1（%2）")
               .arg(QDir::toNativeSeparators(installDir_), Human(PathSize(installDir_))));
    const QString data = UserDataDir();
    if (!data.isEmpty() && QFileInfo::exists(data))
      Append(QStringLiteral("用户数据：%1（%2）")
                 .arg(QDir::toNativeSeparators(data), Human(PathSize(data))));
  }

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (obj == titleBar_ && ev->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(ev);
      if (me->button() == Qt::LeftButton && windowHandle()) {
        windowHandle()->startSystemMove();
        return true;
      }
    }
    return QWidget::eventFilter(obj, ev);
  }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    QColor card(wd::theme::Hex(QStringLiteral("card")));
    if (!card.isValid()) card = palette().window().color();
    p.fillPath(path, card);
    QColor line(wd::theme::Hex(QStringLiteral("line")));
    if (line.isValid()) {
      p.setPen(QPen(line, 1));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
    }
  }

 private:
  void BuildUi() {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(620, 440);
    setWindowTitle(QStringLiteral("卸载 WorkDesk"));

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 12, 16, 14);
    v->setSpacing(10);

    titleBar_ = new QWidget(this);
    titleBar_->setFixedHeight(28);
    titleBar_->installEventFilter(this);
    auto* th = new QHBoxLayout(titleBar_);
    th->setContentsMargins(0, 0, 0, 0);
    auto* t = new QLabel(QStringLiteral("卸载 WorkDesk"), titleBar_);
    t->setObjectName(QStringLiteral("UninstallTitle"));
    t->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* closeBtn = new QToolButton(titleBar_);
    closeBtn->setObjectName(QStringLiteral("UninstallClose"));
    closeBtn->setText(QString(QChar(0x00D7)));
    closeBtn->setFixedSize(26, 26);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(QStringLiteral("关闭"));
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);
    th->addWidget(t);
    th->addStretch(1);
    th->addWidget(closeBtn);

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("UninstallHint"));
    hint_->setWordWrap(true);
    hint_->setText(QStringLiteral("卸载会删除 WorkDesk 的程序文件。下面勾选的个人数据也会一起删掉，"
                                  "删完找不回来。"));

    keepData_ = new QCheckBox(QStringLiteral("同时删除我的设置、聊天记录、曲库和日志"), this);
    keepData_->setChecked(true);

    log_ = new QTextEdit(this);
    log_->setObjectName(QStringLiteral("UninstallLog"));
    log_->setReadOnly(true);
    log_->setMinimumHeight(160);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    cancelBtn_ = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn_->setObjectName(QStringLiteral("Ghost"));
    goBtn_ = new QPushButton(QStringLiteral("卸载"), this);
    goBtn_->setObjectName(QStringLiteral("Primary"));
    goBtn_->setDefault(true);
    btnRow->addStretch(1);
    btnRow->addWidget(cancelBtn_);
    btnRow->addWidget(goBtn_);

    v->addWidget(titleBar_);
    v->addWidget(hint_);
    v->addWidget(keepData_);
    v->addWidget(log_, 1);
    v->addLayout(btnRow);

    connect(cancelBtn_, &QPushButton::clicked, this, [this]() { close(); });
    connect(goBtn_, &QPushButton::clicked, this, &UninstallWindow::StartUninstall);
  }

  void Append(const QString& line) { log_->append(line); }

  void StartUninstall() {
    if (started_) return;
    const auto ret = QMessageBox::question(
        this, QStringLiteral("确认卸载"),
        QStringLiteral("确实要从这台电脑上删除 WorkDesk 吗？\n这一步不可撤销。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (!SafeInstallDir(installDir_, selfExe_)) {
      Append(QStringLiteral("× 当前目录看起来不是 WorkDesk 的安装目录，为安全起见停止卸载。"));
      goBtn_->setEnabled(false);
      return;
    }

    started_ = true;
    goBtn_->setEnabled(false);
    keepData_->setEnabled(false);
    setCursor(Qt::WaitCursor);

    const int failed = DoUninstall(installDir_, selfExe_, keepData_->isChecked(),
                                   [this](const QString& s) { Append(s); });

    if (failed == 0) {
      Append(QStringLiteral("收拾干净了，正在移除卸载程序自身…"));
      Append(QStringLiteral("窗口关掉之后就删完了，可以放心。"));
      cancelBtn_->setText(QStringLiteral("关闭"));
      ScheduleSelfRemoval(installDir_, selfExe_, cleanupDirs_);
    } else {
      Append(QStringLiteral("有 %1 个文件没删掉，多半是还被占用：关掉相关程序后再运行一次本卸载程序。")
                .arg(failed));
      cancelBtn_->setText(QStringLiteral("关闭"));
    }
    setCursor(Qt::ArrowCursor);
  }

  QString installDir_;
  QString selfExe_;
  QStringList cleanupDirs_;
  QWidget* titleBar_ = nullptr;
  QLabel* hint_ = nullptr;
  QCheckBox* keepData_ = nullptr;
  QTextEdit* log_ = nullptr;
  QPushButton* cancelBtn_ = nullptr;
  QPushButton* goBtn_ = nullptr;
  bool started_ = false;
};

// 从临时副本里跑：自己所在目录就是要删的临时目录，安装目录由 --workdir 给
QString ValueOfArg(const QStringList& args, const QString& key) {
  const int i = args.indexOf(key);
  if (i >= 0 && i + 1 < args.size()) return args.at(i + 1);
  return QString();
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("WorkDesk Uninstall"));
  app.setOrganizationName(QStringLiteral("WorkDesk"));

  QStringList args = app.arguments();
  args.removeFirst();  // 去掉 exe 路径

  const bool silent = args.contains(QStringLiteral("--silent"));
  const bool purge = args.contains(QStringLiteral("--purge-data"));
  const QString givenDir = ValueOfArg(args, QStringLiteral("--workdir"));
  const bool relocated = !givenDir.isEmpty();
  const QString selfExe = QCoreApplication::applicationFilePath();

  // 第一趟（在安装目录里跑）：把自删改到临时副本去做，否则自己加载的 Qt DLL 谁也删不掉
  if (!relocated && SafeInstallDir(QCoreApplication::applicationDirPath(), selfExe)) {
    const QString tmpExe = RelocateToTemp();
    if (!tmpExe.isEmpty()) {
      QStringList child{QStringLiteral("--workdir"), QCoreApplication::applicationDirPath()};
      if (silent) child << QStringLiteral("--silent");
      if (purge) child << QStringLiteral("--purge-data");
      const bool ok = QProcess::startDetached(tmpExe, child);
      if (ok) return 0;
    }
    // 复制失败就退回原地删：能删多少删多少，删不掉的会如实列出来
  }

  const QString installDir = relocated ? givenDir : QCoreApplication::applicationDirPath();
  const QStringList cleanupDirs = relocated ? QStringList{QCoreApplication::applicationDirPath()}
                                           : QStringList{};

  if (silent) {
    const QString out = QDir::tempPath() + QStringLiteral("/WorkDesk-uninstall.log");
    QStringList lines;
    if (!SafeInstallDir(installDir, selfExe)) {
      lines << QStringLiteral("× 不是可识别的 WorkDesk 安装目录：%1")
                   .arg(QDir::toNativeSeparators(installDir));
      QFile f(out);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(lines.join('\n').toUtf8());
      return 2;
    }
    const int failed = DoUninstall(installDir, selfExe, purge,
                                   [&lines](const QString& s) { lines << s; });
    lines << (failed == 0 ? QStringLiteral("完成") : QStringLiteral("未完成：%1 项失败").arg(failed));
    QFile f(out);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(lines.join('\n').toUtf8());
    if (failed == 0) ScheduleSelfRemoval(installDir, selfExe, cleanupDirs);
    return failed == 0 ? 0 : 1;
  }

  wd::theme::ThemeManager::Instance().Init();
  UninstallWindow win(installDir, selfExe, cleanupDirs);
  win.show();
  return app.exec();
}