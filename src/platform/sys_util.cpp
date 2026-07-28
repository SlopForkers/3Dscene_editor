#include "sys_util.h"
#include <filesystem>
#include <fstream>

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
    if (len <= 1) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}
#endif

bool readFileBytes(const std::string& utf8Path, std::vector<char>& out) {
    std::filesystem::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    f.read(out.data(), size);
    return f.good() && (size_t)f.gcount() == (size_t)size;
}

bool writeFileBytes(const std::string& utf8Path, const void* data, size_t size) {
    std::filesystem::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size == 0) return true;
    f.write((const char*)data, (std::streamsize)size);
    return f.good();
}
