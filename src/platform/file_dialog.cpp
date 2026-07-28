#include "file_dialog.h"
#include "sys_util.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <vector>

namespace {

// Build the double-NUL-terminated wide filter string
// "Description\0*.ext;*.ext\0\0" from UTF-8 pieces.
std::vector<wchar_t> buildFilter(const char* description, const char* patterns) {
    std::vector<char> narrow;
    for (const char* p = description; *p; ++p) narrow.push_back(*p);
    narrow.push_back('\0');
    for (const char* p = patterns; *p; ++p) narrow.push_back(*p);
    narrow.push_back('\0');
    narrow.push_back('\0');

    int wlen = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), (int)narrow.size(),
                                   nullptr, 0);
    if (wlen <= 0) return {};
    std::vector<wchar_t> w(wlen);
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), (int)narrow.size(),
                        w.data(), wlen);
    return w;
}

} // namespace

std::string openFileDialog(const char* filterDescription,
                           const char* filterPatterns, void* ownerWindow) {
    std::vector<wchar_t> wFilter = buildFilter(filterDescription, filterPatterns);
    if (wFilter.empty()) return std::string();

    wchar_t wFile[MAX_PATH] = {0};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)ownerWindow;
    ofn.lpstrFilter = wFilter.data();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = wFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    // FALSE covers both "cancelled" and "error" (e.g. FNERR_BUFFERTOOSMALL
    // for very long paths); either way the caller gets an empty string.
    if (!GetOpenFileNameW(&ofn)) return std::string();

    // Convert the chosen path back to UTF-8 (wideToUtf8 is checked).
    return wideToUtf8(wFile);
}

std::string saveFileDialog(const char* filterDescription,
                           const char* filterPatterns,
                           const char* defaultExtension, void* ownerWindow) {
    std::vector<wchar_t> wFilter = buildFilter(filterDescription, filterPatterns);
    if (wFilter.empty()) return std::string();

    wchar_t wFile[MAX_PATH] = {0};
    std::wstring wExt = utf8ToWide(defaultExtension ? defaultExtension : "");

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)ownerWindow;
    ofn.lpstrFilter = wFilter.data();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = wFile;
    ofn.nMaxFile = MAX_PATH;
    // Let the dialog append the extension itself for extension-less names.
    ofn.lpstrDefExt = wExt.empty() ? nullptr : wExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) return std::string();

    return wideToUtf8(wFile);
}
