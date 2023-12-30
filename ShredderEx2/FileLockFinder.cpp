#include "FileLockFinder.h"
#include <RestartManager.h>
#include <iostream>

#pragma comment(lib, "Rstrtmgr.lib")

std::vector<DWORD> FileLockFinder::FindLockingProcesses(const std::wstring& filename) {
    DWORD sessionHandle;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = { 0 };
    std::vector<DWORD> lockingPids;

    if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS) {
        return lockingPids;
    }

    // Local structure for cleanup
    struct Cleanup {
        DWORD session;
        ~Cleanup() { RmEndSession(session); }
    } cleanup{ sessionHandle };

    LPCWSTR files[] = { filename.c_str() };
    if (RmRegisterResources(sessionHandle, 1, files, 0, nullptr, 0, nullptr) != ERROR_SUCCESS) {
        return lockingPids;
    }

    UINT procInfoNeeded = 0;
    UINT procInfoCount = 0;
    DWORD rebootReasons = RmRebootReasonNone;
    auto result = RmGetList(sessionHandle, &procInfoNeeded, &procInfoCount, nullptr, &rebootReasons);

    if (result == ERROR_MORE_DATA) {
        std::vector<RM_PROCESS_INFO> procInfos(procInfoNeeded);
        procInfoCount = procInfoNeeded;

        result = RmGetList(sessionHandle, &procInfoNeeded, &procInfoCount, procInfos.data(), &rebootReasons);
        if (result == ERROR_SUCCESS) {
            for (UINT i = 0; i < procInfoCount; ++i) {
                lockingPids.push_back(procInfos[i].Process.dwProcessId);
            }
        }
    }

    return lockingPids;
}
