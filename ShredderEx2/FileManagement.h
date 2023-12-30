#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <future>

using namespace std;

class FileManagement {
public: 
    enum class FileAction {
        None = 0,
        Skip = 1,
        Kill = 2
    };

private:
    wstring latestScanFile;
    wstring latestDeleteFile;
    mutex mutexString;

    atomic<int> progress{ 0 };
    atomic<bool> breakpoint{ false };
    atomic<bool> remember{ false };
    atomic<FileAction> action{ FileAction::None };
    atomic<bool> done{ false };
    atomic<bool> deleteFutureCancellation{ false };

    vector<future<void>> activeFutures;
    vector<wstring> pathsToDelete;

	void OverwriteFileWithZeros(const wstring& filePath);
	void Delete(const wstring& path, bool allowFolder = false);
    void KillProcessesOfFile(const wstring& path);
    void KillProcess(DWORD pid);
    bool FileExists(const wstring& path);
    bool DirectoryExists(const wstring& path);
    bool RemoveWriteProtection(const wstring& filePath);

public:
	vector<wstring> GetAllNeededPaths(const wstring& path, atomic<bool>* cancellation);
	void Delete(const vector<wstring>& paths);
	bool IsFile(const wstring& path);
    
    void SetLatestScanFile(const wstring& filePath) {
        lock_guard<mutex> lock(mutexString);
        latestScanFile = filePath;
    }

    wstring GetLatestScanFile() {
        lock_guard<mutex> lock(mutexString);
        return latestScanFile;
    }

    void SetLatestDeleteFile(const wstring& filePath) {
        lock_guard<mutex> lock(mutexString);
        latestDeleteFile = filePath;
    }

    wstring GetLatestDeleteFile() {
        lock_guard<mutex> lock(mutexString);
        return latestDeleteFile;
    }

    void SetProgress(int value) {
        progress = value;
    }

    int GetProgress() const {
        return progress;
    }

    void SetBreakpoint(bool value) {
        breakpoint = value;
    }

    bool GetBreakpoint() const {
        return breakpoint;
    }

    void SetRemember(bool value) {
        remember = value;
    }

    bool GetRemember() const {
        return remember;
    }

    void SetAction(FileAction value) {
		action = value;
	}

    FileAction GetAction() const {
		return action;
	}

	void SetDone(bool value) {
		done = value;
	}

	bool GetDone() const {
		return done;
	}

    void SetDeleteFutureCancellation(bool value) {
		deleteFutureCancellation = value;
	}

    bool GetDeleteFutureCancellation() const {
		return deleteFutureCancellation;
	}
};

