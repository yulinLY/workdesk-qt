#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace wd {

// ---- 编码 ----
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// ---- 路径 ----
std::string ExeDir();                 // exe 所在目录（UTF-8，无结尾反斜杠）
std::wstring AppDataDir();            // %APPDATA%\WorkDesk，不存在会创建

// ---- 文件（统一 UTF-8，读时剥 BOM）----
bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8(const std::wstring& path, const std::string& text);

// ---- 系统 ----
std::string GetEnvUtf8(const char* name);
std::string Trim(const std::string& s);
std::string Lower(std::string s);

// ---- 剪贴板 / 资源管理器 ----
void SetClipboardUtf8(const std::string& text);
void RevealPath(const std::wstring& path);  // 文件则选中，目录则打开

// ---- base64 ----
std::string Base64Encode(const uint8_t* data, size_t len);
std::string Base64Encode(const std::vector<uint8_t>& bytes);

// ---- 文字类文件：多编码自动识别（记事本用）----
enum class TextEncoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Ansi };
const char* TextEncodingName(TextEncoding e);      // 状态栏展示用："UTF-8" / "ANSI(GBK)" ...
const char* TextEncodingShortName(TextEncoding e); // 同上，更短
bool IsTextFilePath(const std::wstring& path);     // 是否文字类文件（按扩展名/文件名判断）

// 自动识别编码读取：outUtf8 统一为 UTF-8 且换行统一成 LF；
// enc / crlf 记录源文件真实编码与换行风格，保存时原样写回；失败时 error 给出人话原因。
// maxBytes > 0 时只读文件开头这么多个字节（给"把大文件喂给 AI"用，不受编辑器 32MB 上限限制）
bool ReadTextFileAuto(const std::wstring& path, std::string& outUtf8, TextEncoding& enc,
                      bool& crlf, std::string& error,
                      unsigned long long maxBytes = 0);
// 按 enc / crlf 写回（打开时是什么编码、什么换行，保存就还是什么）
bool WriteTextFileAuto(const std::wstring& path, const std::string& utf8, TextEncoding enc,
                       bool crlf);

const wchar_t* TextFileOpenFilter();  // 记事本「打开」对话框过滤器（文字类 + 所有文件）
const wchar_t* TextFileSaveFilter();  // 记事本「保存」对话框过滤器

// ---- 文件对话框（Win32 通用对话框）----
// filter 形如 L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0"
bool OpenFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    std::wstring& outPath);
bool SaveFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    const std::wstring& suggestedName, std::wstring& outPath);

// ---- 时间 ----
std::string NowTimeString();  // HH:MM

}  // namespace wd
