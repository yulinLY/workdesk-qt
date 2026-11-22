#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStringList>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "frost.h"
#include "glass.h"
#include "mainwindow.h"
#include "markdowntoolbar.h"
#include "musicplayer.h"
#include "shellint.h"
#include "smoothscroll.h"
#include "theme.h"
#include "tooltip.h"

namespace {

// 启动日志：写到 %APPDATA%\WorkDesk\WorkDeskQt\boot.log，便于排查启动崩溃。
// 设置环境变量 WORKDESK_NO_BOOTLOG=1 可关闭。
void Boot(const char* s) {
  if (std::getenv("WORKDESK_NO_BOOTLOG")) return;
  const char* appdata = std::getenv("APPDATA");
  if (!appdata) return;
  char path[1024];
  snprintf(path, sizeof(path), "%s\\WorkDesk\\WorkDeskQt", appdata);
  CreateDirectoryA(path, nullptr);
  snprintf(path, sizeof(path), "%s\\WorkDesk\\WorkDeskQt\\boot.log", appdata);
  FILE* f = nullptr;
  if (fopen_s(&f, path, "a") == 0 && f) {
    fprintf(f, "%s\n", s);
    fclose(f);
  }
}

// Qt 自带消息统一落盘：qFatal / qCritical 会直接 abort，GUI 进程没有控制台，
// 不写日志就只剩一个 BEX64 崩溃事件，看不到真正死因。
void QtLogToFile(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  if (type < QtWarningMsg) return;
  char buf[2048];
  snprintf(buf, sizeof(buf), "qt[%d] %s (%s:%d)", (int)type, msg.toUtf8().constData(),
           ctx.file ? ctx.file : "?", ctx.line);
  Boot(buf);
}

// 未处理异常：记下异常码、出错模块与模块内偏移
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
  const void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
  char modName[MAX_PATH] = "?";
  HMODULE mod = nullptr;
  if (addr) {
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)addr, &mod);
    if (mod) GetModuleFileNameA(mod, modName, MAX_PATH);
  }
  char buf[1024];
  snprintf(buf, sizeof(buf), "!! crash code=%08lX addr=%p module=%s off=0x%llX",
           (unsigned long)(ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0), addr,
           modName, (unsigned long long)(mod ? (const char*)addr - (const char*)mod : 0));
  Boot(buf);
  return EXCEPTION_EXECUTE_HANDLER;
}

void OnTerminate() {
  Boot("!! terminate (std::terminate: 未捕获异常 / 线程析构仍 joinable)");
  std::abort();
}

// 字体全部取自系统已安装字体库：按优先级挑第一个系统里真实存在的族，
// 不打包任何字体文件，跟随系统字体设置走。
QFont PickSystemUiFont() {
  const QStringList prefer = {
      QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"),
      QStringLiteral("Segoe UI"),          QStringLiteral("SimSun"),
  };
  const QStringList installed = QFontDatabase::families();
  QFont f = QApplication::font();
  for (const QString& name : prefer) {
    if (installed.contains(name, Qt::CaseInsensitive)) {
      f.setFamily(name);
      break;
    }
  }
  f.setPointSizeF(9.5);
  return f;
}

}  // namespace

int main(int argc, char* argv[]) {
  Boot("-- start");
  // 崩溃定位三件套：Qt 消息落盘 + 未处理异常 + terminate 兜底
  qInstallMessageHandler(QtLogToFile);
  SetUnhandledExceptionFilter(CrashFilter);
  std::set_terminate(OnTerminate);
  QApplication app(argc, argv);
  Boot("qt app ok");
  app.setApplicationName(QStringLiteral("WorkDesk"));
  app.setApplicationDisplayName(QStringLiteral("WorkDesk"));
  app.setOrganizationName(QStringLiteral("WorkDesk"));

  // 窗口图标 / 任务栏图标：D:\画图\ico\WorkDesk.ico 已编进 qrc，exe 资源段另有一份
  app.setWindowIcon(QIcon(QStringLiteral(":/assets/WorkDesk.ico")));

  app.setFont(PickSystemUiFont());
  Boot("font ok");
  // 主题（深色/亮色/跟随系统/跟随时间）由 ThemeManager 统一判定并注入调色板与 QSS，
  // 切换入口在右侧 dock 栏；偏好写 %APPDATA%\WorkDesk\WorkDeskQt\ui.ini
  wd::theme::ThemeManager::Instance().Init();
  Boot("theme ok");

  // 全局平滑滚动：拦滚轮事件，滚动条按指数逼近推进，所有可滚动控件统一生效
  wd::SmoothScroll::Install();
  Boot("smoothscroll ok");

  // Markdown 工具条：任意输入框里 Ctrl+Alt 弹出，1~9 直接套格式（设置对话框内不弹）
  wd::InstallMarkdownTools();
  Boot("markdowntools ok");

  // 提示气泡：原生 QToolTip 在亮色主题下按系统深色画底、却取亮色的深色文字（黑底黑字），
  // 这里接管全局 ToolTip 事件，换成跟调色板走的自绘气泡
  wd::InstallTooltips();
  Boot("tooltips ok");

  // 登记到「控制面板 / 程序和功能」：绿色程序本来不进系统列表，用户会找不到卸载入口。
  // 指向同目录的 Uninstall.exe；不想被登记就在 exe 旁边放个 portable.txt。
  Boot(wd::RegisterUninstallEntry() ? "uninstall entry ok" : "uninstall entry skipped");

  // 实时毛玻璃：窗口底交给 DWM 系统材质（亚克力），背后内容每帧由 DWM 重算，
  // 不用抓屏、不用逐帧截图，CPU 零开销，录屏和截图都能正常看到窗口。
  // 快照磨砂 frost 只在系统材质挂不上时兜底（老系统 / 关了透明效果）。
  wd::MainWindow win;
  Boot("mainwindow ok");
  win.show();
  const bool glassOk = wd::glass::Enable(&win, wd::theme::ThemeManager::Instance().light());
  Boot(glassOk ? "glass ok" : "glass none");
  if (!glassOk) {
    wd::frost::Capture();
    Boot(wd::frost::Ready() ? "frost ok" : "frost none");
    win.update();
  }
  // 主题切换后：材质重设一次（DWM 需要重画），窗口重绘换遮罩色
  QObject::connect(&wd::theme::ThemeManager::Instance(), &wd::theme::ThemeManager::Applied, &win,
                   [&win](bool light) {
                     wd::glass::Retint(light);
                     win.update();
                   });

  // 命令行 / 资源管理器「打开方式」传入的文件
  if (argc > 1) {
    QStringList files;
    for (int i = 1; i < argc; ++i) {
      const QString a = QString::fromLocal8Bit(argv[i]);
      if (!a.startsWith('-')) files << a;
    }
    win.OpenFiles(files);
  }
  Boot("show ok");
  const int r = app.exec();
  Boot("exit");
  return r;
}
