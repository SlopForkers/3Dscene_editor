#include "sys_util.h"
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

std::string wideToUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};   // 0 = error, 1 = empty string (just the NUL)
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}
#endif

namespace {
FILE* openFile(const std::string& utf8Path, bool write) {
#ifdef _WIN32
    std::wstring w = utf8ToWide(utf8Path);
    if (w.empty()) return nullptr;
    FILE* f = nullptr;
    _wfopen_s(&f, w.c_str(), write ? L"wb" : L"rb");
    return f;
#else
    return std::fopen(utf8Path.c_str(), write ? "wb" : "rb");
#endif
}
} // namespace

bool readFileBytes(const std::string& utf8Path, std::vector<char>& out) {
    FILE* f = openFile(utf8Path, false);
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) { std::fclose(f); return false; }
    out.resize((size_t)size);
    size_t read = size > 0 ? std::fread(out.data(), 1, (size_t)size, f) : 0;
    bool ok = std::ferror(f) == 0 && read == (size_t)size;
    std::fclose(f);
    return ok;
}

bool writeFileBytes(const std::string& utf8Path, const void* data, size_t size) {
    FILE* f = openFile(utf8Path, true);
    if (!f) return false;
    size_t written = size > 0 ? std::fwrite(data, 1, size, f) : 0;
    bool ok = std::ferror(f) == 0 && written == size;
    std::fclose(f);
    return ok;
}
