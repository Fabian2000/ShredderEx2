#pragma once
#include <string>
#include <vector>
#include <Windows.h>

class FileLockFinder {
public:
    static std::vector<DWORD> FindLockingProcesses(const std::wstring& filename);
};