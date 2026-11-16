#include "util.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace wd {

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::string ExeDir() {
  wchar_t buf[MAX_PATH] = {0};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf);
  size_t pos = p.find_last_of(L"\\/");
  if (pos != std::wstring::npos) p = p.substr(0, pos);
  return WideToUtf8(p);
}

std::wstring AppDataDir() {
  wchar_t* base = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base))) {
    dir = std::wstring(base) + L"\\WorkDesk";
    CoTaskMemFree(base);
  } else {
    wchar_t buf[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    dir = std::wstring(buf) + L"\\WorkDesk";
  }
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart > 64ll * 1024 * 1024) {
    CloseHandle(h);
    return false;
  }
  out.resize((size_t)size.QuadPart);
  DWORD read = 0;
  BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr);
  CloseHandle(h);
  if (!ok) return false;
  out.resize(read);
  // 剥掉 UTF-8 BOM
  if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB &&
      (unsigned char)out[2] == 0xBF) {
    out.erase(0, 3);
  }
  return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& text) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = text.empty() || WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr);
  CloseHandle(h);
  return ok && written == text.size();
}

// ------------------------------------------------------------ 文字类文件
const char* TextEncodingName(TextEncoding e) {
  switch (e) {
    case TextEncoding::Utf8Bom: return "UTF-8 (BOM)";
    case TextEncoding::Utf16LE: return "UTF-16 LE";
    case TextEncoding::Utf16BE: return "UTF-16 BE";
    case TextEncoding::Ansi:    return "ANSI (GBK)";
    default:                    return "UTF-8";
  }
}

const char* TextEncodingShortName(TextEncoding e) {
  switch (e) {
    case TextEncoding::Utf8Bom: return "UTF-8 BOM";
    case TextEncoding::Utf16LE: return "UTF-16 LE";
    case TextEncoding::Utf16BE: return "UTF-16 BE";
    case TextEncoding::Ansi:    return "ANSI";
    default:                    return "UTF-8";
  }
}

namespace {

std::wstring LowerExtOf(const std::wstring& p) {
  size_t dot = p.find_last_of(L'.');
  size_t sep = p.find_last_of(L"\\/");
  if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return {};
  std::wstring e = p.substr(dot + 1);
  for (auto& c : e)
    if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
  return e;
}

std::wstring LowerNameOf(const std::wstring& p) {
  size_t sep = p.find_last_of(L"\\/");
  std::wstring n = (sep == std::wstring::npos) ? p : p.substr(sep + 1);
  for (auto& c : n)
    if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
  return n;
}

// 文档 / 配置 / 代码 / 脚本 / 字幕：凡是能用编辑器看的都算文字类
bool ExtLooksText(const std::wstring& e) {
  static const wchar_t* kExts[] = {
      L"txt",  L"text", L"md",     L"markdown", L"mdx",   L"rst",   L"adoc",  L"org",
      L"log",  L"json", L"jsonc",  L"json5",    L"csv",   L"tsv",   L"xml",   L"yml",
      L"yaml", L"toml", L"ini",    L"cfg",      L"conf",  L"config", L"properties",
      L"env",  L"reg",  L"inf",    L"c",        L"h",     L"cc",    L"cpp",   L"cxx",
      L"hpp",  L"hxx",  L"hh",     L"inl",      L"ipp",   L"inc",   L"tpl",   L"cs",
      L"java", L"kt",   L"kts",    L"scala",    L"go",    L"rs",    L"swift", L"rb",
      L"php",  L"pl",   L"lua",    L"py",       L"pyw",   L"pyi",   L"ipynb", L"js",
      L"jsx",  L"ts",   L"tsx",    L"mjs",      L"cjs",   L"vue",   L"svelte", L"css",
      L"scss", L"sass", L"less",   L"styl",     L"html",  L"htm",   L"xhtml", L"svg",
      L"sh",   L"bash", L"zsh",    L"fish",     L"bat",   L"cmd",   L"ps1",   L"psd1",
      L"psm1", L"vbs",  L"asm",    L"s",        L"ld",    L"lds",   L"nasm",  L"mk",
      L"cmake", L"gradle", L"pro", L"pri",      L"rc",    L"def",   L"sln",   L"vcxproj",
      L"csproj", L"props", L"filters", L"sql",  L"diff",  L"patch", L"tex",   L"bib",
      L"srt",  L"vtt",  L"ass",    L"lrc",      L"gitignore", L"gitattributes",
      L"gitmodules", L"editorconfig", L"clang-format", L"dockerignore", L"npmrc",
      L"lock", L"manifest", L"plist", L"xaml",  L"resx",  L"po",    L"pot",   L"m",
      L"mm",   L"r",    L"jl",     L"ex",       L"exs",   L"erl",   L"hs",    L"clj"};
  for (const wchar_t* k : kExts)
    if (e == k) return true;
  return false;
}

// 没扩展名的常见文本文件，按文件名认
bool NameLooksText(const std::wstring& name) {
  static const wchar_t* kNames[] = {L"makefile",  L"gnumakefile", L"cmakelists.txt",
                                    L"dockerfile", L"license",    L"licence",
                                    L"readme",     L"changelog",  L"authors",
                                    L"notice",     L"copying",    L"hosts",
                                    L".gitignore", L".gitattributes", L".editorconfig"};
  for (const wchar_t* k : kNames)
    if (name == k) return true;
  return false;
}

// 原始字节层面判断是否二进制：有 0 字节或异常控制字符就当二进制（UTF-16 已先排除）
bool LooksBinary(const std::string& raw) {
  const size_t n = raw.size() < 8192 ? raw.size() : 8192;
  int ctrl = 0;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = (unsigned char)raw[i];
    if (c == 0) return true;
    if (c < 0x09) return true;
    if (c > 0x0D && c < 0x20) ++ctrl;  // 0x0E-0x1F
  }
  return n > 0 && ctrl * 50 > (int)n;  // 控制字符超过 2% 认作二进制
}

// 无 BOM 时猜 UTF-16：以 ASCII 为主的宽字符文本，一半字节是 0
int GuessWide(const std::string& raw) {  // 0=不是 1=UTF-16LE 2=UTF-16BE
  size_t n = raw.size() & ~(size_t)1;
  if (n < 8) return 0;
  if (n > 4096) n = 4096;
  int evenZero = 0, oddZero = 0;
  for (size_t i = 0; i + 1 < n; i += 2) {
    if (raw[i] == 0) ++evenZero;
    if (raw[i + 1] == 0) ++oddZero;
  }
  const int pairs = (int)(n / 2);
  if (oddZero > pairs * 3 / 10 && evenZero < pairs / 10) return 1;
  if (evenZero > pairs * 3 / 10 && oddZero < pairs / 10) return 2;
  return 0;
}

bool IsValidUtf8(const std::string& s) {
  if (s.empty()) return true;
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr,
                             0) > 0;
}

std::string AnsiToUtf8(const std::string& raw) {
  if (raw.empty()) return {};
  int n = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
  if (n <= 0) return raw;
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), w.data(), n);
  return WideToUtf8(w);
}

std::string Utf16ToUtf8(std::string raw, bool bigEndian) {
  if (bigEndian) {
    for (size_t i = 0; i + 1 < raw.size(); i += 2) std::swap(raw[i], raw[i + 1]);
  }
  if (raw.size() & 1) raw.push_back('\0');
  std::wstring w(raw.size() / 2, L'\0');
  if (!raw.empty()) memcpy(w.data(), raw.data(), raw.size());
  return WideToUtf8(w);
}

}  // namespace

bool IsTextFilePath(const std::wstring& path) {
  const std::wstring ext = LowerExtOf(path);
  if (!ext.empty()) return ExtLooksText(ext);
  return NameLooksText(LowerNameOf(path));
}

bool ReadTextFileAuto(const std::wstring& path, std::string& outUtf8, TextEncoding& enc,
                      bool& crlf, std::string& error, unsigned long long maxBytes) {
  outUtf8.clear();
  error.clear();
  enc = TextEncoding::Utf8;
  crlf = false;

  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    error = "打不开文件（不存在，或正被别的程序占用）";
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size)) {
    CloseHandle(h);
    error = "读不到文件大小";
    return false;
  }
  // maxBytes > 0：只要文件开头，不受编辑器 32MB 上限约束（超上限的文件也允许"只看开头"）
  unsigned long long want = (unsigned long long)size.QuadPart;
  bool truncated = false;
  if (maxBytes > 0) {
    if (want > maxBytes) {
      want = maxBytes;
      truncated = true;
    }
  } else {
    const unsigned long long kMax = 32ull * 1024 * 1024;
    if (want > kMax) {
      CloseHandle(h);
      error = "文件超过 32 MB，编辑器打开会卡死，建议用专业编辑器";
      return false;
    }
  }
  std::string raw((size_t)want, '\0');
  DWORD got = 0;
  bool ok = true;
  while (got < (DWORD)raw.size()) {
    DWORD n = 0;
    if (!ReadFile(h, raw.data() + got, (DWORD)raw.size() - got, &n, nullptr)) {
      ok = false;
      break;
    }
    if (n == 0) break;
    got += n;
  }
  CloseHandle(h);
  if (!ok) {
    error = "读取失败，可能是磁盘错误或文件被独占";
    return false;
  }
  raw.resize(got);

  // 1) 编码判定：BOM 优先，其次无 BOM 的 UTF-16 启发式
  bool wide = false;
  if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
      (unsigned char)raw[2] == 0xBF) {
    enc = TextEncoding::Utf8Bom;
    raw.erase(0, 3);
  } else if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
    enc = TextEncoding::Utf16LE;
    raw.erase(0, 2);
    wide = true;
  } else if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) {
    enc = TextEncoding::Utf16BE;
    raw.erase(0, 2);
    wide = true;
  } else {
    const int g = GuessWide(raw);
    if (g == 1) enc = TextEncoding::Utf16LE, wide = true;
    if (g == 2) enc = TextEncoding::Utf16BE, wide = true;
  }

  if (!wide && LooksBinary(raw)) {
    error = "这不是文本文件（二进制内容），强行打开只会是一屏乱码";
    return false;
  }

  // 只读开头时，末尾可能刚好切在多字节字符中间，往回退到合法边界再转码
  if (truncated && !wide) {
    for (int cut = 0; cut < 4 && !raw.empty() && !IsValidUtf8(raw); ++cut) raw.pop_back();
  }

  // 2) 转成 UTF-8
  std::string utf8;
  if (enc == TextEncoding::Utf16LE) {
    utf8 = Utf16ToUtf8(raw, false);
  } else if (enc == TextEncoding::Utf16BE) {
    utf8 = Utf16ToUtf8(raw, true);
  } else if (IsValidUtf8(raw)) {
    utf8 = raw;  // 纯 ASCII 或合法 UTF-8
  } else {
    utf8 = AnsiToUtf8(raw);  // 本地代码页，简中系统即 GBK
    enc = TextEncoding::Ansi;
  }

  // 3) 换行风格：记下原文件是不是 CRLF，编辑器里统一用 LF，保存时再还原
  crlf = utf8.find("\r\n") != std::string::npos;
  std::string body;
  body.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size(); ++i) {
    if (utf8[i] == '\r') {
      if (i + 1 < utf8.size() && utf8[i + 1] == '\n') continue;  // CRLF 只留 LF
      body.push_back('\n');                                     // 孤立 CR
    } else {
      body.push_back(utf8[i]);
    }
  }
  outUtf8.swap(body);
  return true;
}

bool WriteTextFileAuto(const std::wstring& path, const std::string& utf8, TextEncoding enc,
                       bool crlf) {
  // 编辑器里是 LF，按原文件的换行风格写回
  std::string body;
  body.reserve(utf8.size() + utf8.size() / 16 + 4);
  for (char c : utf8) {
    if (c == '\r') continue;
    if (c == '\n') {
      if (crlf) body.push_back('\r');
      body.push_back('\n');
    } else {
      body.push_back(c);
    }
  }

  std::string bytes;
  switch (enc) {
    case TextEncoding::Utf16LE:
    case TextEncoding::Utf16BE: {
      const std::wstring w = Utf8ToWide(body);
      const bool le = (enc == TextEncoding::Utf16LE);
      bytes.reserve(w.size() * 2 + 2);
      auto push16 = [&](unsigned int v) {
        const char lo = (char)(v & 0xFF), hi = (char)((v >> 8) & 0xFF);
        bytes.push_back(le ? lo : hi);
        bytes.push_back(le ? hi : lo);
      };
      push16(0xFEFF);  // BOM
      for (wchar_t wc : w) push16((unsigned int)(unsigned short)wc);
      break;
    }
    case TextEncoding::Utf8Bom:
      bytes = "\xEF\xBB\xBF" + body;
      break;
    case TextEncoding::Ansi: {
      const std::wstring w = Utf8ToWide(body);
      if (w.empty()) break;
      int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
      if (n > 0) {
        bytes.resize((size_t)n);
        WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), bytes.data(), n, nullptr, nullptr);
      }
      break;
    }
    default:
      bytes = body;
      break;
  }

  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = bytes.empty() || WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
  CloseHandle(h);
  return ok && written == bytes.size();
}

const wchar_t* TextFileOpenFilter() {
  return L"文字类文件\0"
         L"*.txt;*.text;*.md;*.markdown;*.mdx;*.log;*.json;*.jsonc;*.csv;*.tsv;*.xml;*.yml;"
         L"*.yaml;*.toml;*.ini;*.cfg;*.conf;*.properties;*.env;*.reg;"
         L"*.c;*.h;*.cc;*.cpp;*.cxx;*.hpp;*.hxx;*.hh;*.inl;*.ipp;*.cs;*.java;*.kt;*.go;*.rs;"
         L"*.py;*.pyw;*.js;*.mjs;*.ts;*.jsx;*.tsx;*.vue;*.svelte;*.css;*.scss;*.sass;*.less;"
         L"*.html;*.htm;*.svg;*.php;*.rb;*.lua;*.pl;*.swift;*.m;*.mm;"
         L"*.sh;*.bash;*.bat;*.cmd;*.ps1;*.vbs;*.asm;*.s;*.sql;*.cmake;*.mk;*.gradle;"
         L"*.sln;*.vcxproj;*.csproj;*.diff;*.patch;*.tex;*.srt;*.vtt;*.ass;*.lrc;"
         L"*.gitignore;*.gitattributes;*.editorconfig;*.npmrc;*.manifest;*.rc;*.def\0"
         L"所有文件 (*.*)\0*.*\0\0";
}

const wchar_t* TextFileSaveFilter() {
  return L"纯文本 (*.txt)\0*.txt\0Markdown (*.md)\0*.md\0"
         L"代码 (*.c;*.cpp;*.h;*.hpp;*.py;*.js;*.ts;*.json)\0*.c;*.cpp;*.h;*.hpp;*.py;*.js;*.ts;*.json\0"
         L"日志 (*.log)\0*.log\0所有文件 (*.*)\0*.*\0\0";
}

std::string GetEnvUtf8(const char* name) {
  wchar_t buf[4096] = {0};
  DWORD n = GetEnvironmentVariableW(Utf8ToWide(name).c_str(), buf, 4096);
  if (n == 0) return {};
  return WideToUtf8(std::wstring(buf, n));
}

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string Lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

void SetClipboardUtf8(const std::string& text) {
  std::wstring w = Utf8ToWide(text);
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  size_t bytes = (w.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      memcpy(p, w.c_str(), bytes);
      GlobalUnlock(h);
      if (SetClipboardData(CF_UNICODETEXT, h)) h = nullptr;  // 所有权交给系统
    }
    if (h) GlobalFree(h);
  }
  CloseClipboard();
}

void RevealPath(const std::wstring& path) {
  if (path.empty()) return;
  DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    return;
  }
  std::wstring dir = path;
  if (attr == INVALID_FILE_ATTRIBUTES) {
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
  }
  ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string Base64Encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]);
    out.push_back(kB64[v & 63]);
  }
  if (i < len) {
    uint32_t v = data[i] << 16;
    bool two = (i + 1 < len);
    if (two) v |= data[i + 1] << 8;
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(two ? kB64[(v >> 6) & 63] : '=');
    out.push_back('=');
  }
  return out;
}

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
  return Base64Encode(bytes.data(), bytes.size());
}

bool OpenFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    std::wstring& outPath) {
  wchar_t buf[MAX_PATH * 4] = {0};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = (HWND)ownerHwnd;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH * 4;
  ofn.lpstrDefExt = defaultExt;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn)) return false;
  outPath = buf;
  return true;
}

bool SaveFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    const std::wstring& suggestedName, std::wstring& outPath) {
  wchar_t buf[MAX_PATH * 4] = {0};
  if (!suggestedName.empty()) wcsncpy_s(buf, suggestedName.c_str(), _TRUNCATE);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = (HWND)ownerHwnd;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH * 4;
  ofn.lpstrDefExt = defaultExt;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetSaveFileNameW(&ofn)) return false;
  outPath = buf;
  return true;
}

std::string NowTimeString() {
  time_t t = time(nullptr);
  tm local{};
  localtime_s(&local, &t);
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", &local);
  return buf;
}

}  // namespace wd
