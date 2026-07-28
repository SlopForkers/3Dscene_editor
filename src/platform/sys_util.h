#pragma once
#include <string>
#include <vector>

// Platform helpers. On Windows the C runtime narrow-file APIs interpret
// char* paths as ANSI, so all file IO in the app goes through these
// functions, which accept UTF-8 and use the wide Win32 APIs internally.
// On other platforms they fall back to plain fopen (paths are already
// UTF-8 there).

// Read an entire file. Returns false if it cannot be opened/read.
bool readFileBytes(const std::string& utf8Path, std::vector<char>& out);

// Write (overwrite) an entire file. Returns false on any failure.
bool writeFileBytes(const std::string& utf8Path, const void* data, size_t size);

// UTF-8 <-> UTF-16 conversion (Windows); identity helpers elsewhere.
#ifdef _WIN32
std::wstring utf8ToWide(const std::string& s);
std::string  wideToUtf8(const wchar_t* w);
#endif
