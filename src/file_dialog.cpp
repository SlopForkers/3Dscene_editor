#include "file_dialog.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <vector>

std::string openFileDialog(const char* filterDescription, const char* filterPatterns) {
    // Build the filter string: "Description\0*.ext;*.ext\0\0"
    std::vector<char> filter;
    for (const char* p = filterDescription; *p; ++p) filter.push_back(*p);
    filter.push_back('\0');
    for (const char* p = filterPatterns; *p; ++p) filter.push_back(*p);
    filter.push_back('\0');
    filter.push_back('\0');

    wchar_t wFile[MAX_PATH] = {0};

    // Convert the filter to wide chars.
    int wFilterLen = MultiByteToWideChar(CP_UTF8, 0, filter.data(), (int)filter.size(),
                                          nullptr, 0);
    std::vector<wchar_t> wFilter(wFilterLen);
    MultiByteToWideChar(CP_UTF8, 0, filter.data(), (int)filter.size(),
                        wFilter.data(), wFilterLen);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = wFilter.data();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = wFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return std::string();

    // Convert the chosen path back to UTF-8.
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wFile, -1, nullptr, 0, nullptr, nullptr);
    std::string result(utf8Len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wFile, -1, &result[0], utf8Len, nullptr, nullptr);
    return result;
}
