#include "FileManagement.h"
#include <fstream>
#include <iostream>
#include "FileLockFinder.h"

vector<wstring> FileManagement::GetAllNeededPaths(const wstring& path, atomic<bool>* cancellation) {
    WIN32_FIND_DATA findFileData;
    wstring searchPath = path + (path.ends_with(L"\\") ? L"*" : L"\\*");
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findFileData);
    vector<wstring> files;
    vector<wstring> dirs;
    vector<wstring> connected;

    if (hFind == INVALID_HANDLE_VALUE) {
        return connected;  // Leeren Vektor zurückgeben
    }
    else {
        do {
            wstring fileName = findFileData.cFileName;
            if (fileName == L"." || fileName == L"..") {
                continue; // Aktuelles oder übergeordnetes Verzeichnis überspringen
            }
            wstring fullPath = path + L"\\" + fileName;
            SetLatestScanFile(fullPath);
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push_back(fullPath);
                vector<wstring> subDirs = GetAllNeededPaths(fullPath, cancellation);
                connected.insert(connected.begin(), subDirs.begin(), subDirs.end());
            }
            else {
                files.push_back(fullPath);
            }
        } while (FindNextFile(hFind, &findFileData) != 0 && !(cancellation && *cancellation));
        FindClose(hFind);
    }

    connected.insert(connected.end(), files.begin(), files.end());
    connected.insert(connected.end(), dirs.begin(), dirs.end());

    return connected;
}

// Needed to make the file unrecoverable
void FileManagement::OverwriteFileWithZeros(const wstring& filePath) {
    for (int retry = 0; retry < 3; retry++) {
        try {
            fstream file(filePath, ios::binary | ios::out | ios::in);

            if (!file.is_open()) {
                SetBreakpoint(true);
                while (GetBreakpoint()) {
                    if (GetAction() == FileAction::None) {
                        Sleep(1'000);
                        this_thread::yield();
                        continue;
                    }

                    switch (GetAction()) {
                    case FileAction::None:
                        Sleep(1'000);
                        this_thread::yield();
                        continue;
                    case FileAction::Skip:
                        if (!GetRemember()) {
                            SetAction(FileAction::None);
                        }
                        SetBreakpoint(false);
                        break;
                    case FileAction::Kill:
                        if (!GetRemember()) {
                            SetAction(FileAction::None);
                        }
                        KillProcessesOfFile(filePath);
                        SetBreakpoint(false);
                        break;
                    }
                }
                file.open(filePath, ios::binary | ios::out | ios::in);

                if (!file.is_open()) {
                    return;
                }
            }

            file.seekg(0, ios::end);
            streampos fileSize = file.tellg();
            file.seekg(0, ios::beg);

            constexpr size_t bufferSize = 1024 * 1024; // 1 MB Buffer
            vector<char>* buffer = new vector<char>(bufferSize, 0);

            while (fileSize > 0) {
                size_t currentBlockSize = static_cast<size_t>(min(fileSize, static_cast<streampos>(bufferSize)));
                file.write(buffer->data(), currentBlockSize);
                fileSize -= currentBlockSize;
            }
            delete buffer;

            file.close();
        }
        catch (...) {
        	Sleep(1'000);
			this_thread::yield();
			continue;
        }
        break;
	}
}

bool FileManagement::RemoveWriteProtection(const wstring& filePath) {
    DWORD attributes = GetFileAttributes(filePath.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (attributes & FILE_ATTRIBUTE_READONLY) {
        // Schreibschutzattribut entfernen
        if (!SetFileAttributes(filePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
            return false;
        }
    }

    return true;
}

void FileManagement::Delete(const wstring& path, bool allowFolder)
{
    for (int retry = 0; retry < 3; retry++) {
        try {
            if (!FileExists(path) && !DirectoryExists(path)) {
                return;
            }

            if (IsFile(path)) {
                RemoveWriteProtection(path);
                OverwriteFileWithZeros(path);

                if (DeleteFile(path.c_str()) == 0)
                {
                    SetBreakpoint(true);
                    while (GetBreakpoint()) {
                        if (GetAction() == FileAction::None) {
                            Sleep(1'000);
                            this_thread::yield();
                            continue;
                        }

                        switch (GetAction()) {
                        case FileAction::None:
                            Sleep(1'000);
                            this_thread::yield();
                            continue;
                        case FileAction::Skip:
                            if (!GetRemember()) {
                                SetAction(FileAction::None);
                            }
                            SetBreakpoint(false);
                            break;
                        case FileAction::Kill:
                            if (!GetRemember()) {
                                SetAction(FileAction::None);
                            }
                            KillProcessesOfFile(path);
                            DeleteFile(path.c_str());
                            SetBreakpoint(false);
                            break;
                        }
                    }
                }
                SetProgress(GetProgress() + 1);
            }
            else if (allowFolder) {
                RemoveDirectory(path.c_str());
                SetProgress(GetProgress() + 1);
            }
        }
        catch (...) {
			Sleep(1'000);
			this_thread::yield();
			continue;
		}
        break;
    }
}

void FileManagement::Delete(const vector<wstring>& paths)
{
    pathsToDelete = paths;
    activeFutures.push_back(async(launch::async, [&]() {
        for (const wstring& path : pathsToDelete) {
            this_thread::yield();
            Delete(path);
            SetLatestDeleteFile(path);

            if (GetDeleteFutureCancellation()) {
				return;
			}
        }

        for (const wstring& path : pathsToDelete) {
            this_thread::yield();
            Delete(path, true);
            SetLatestDeleteFile(path);
            if (GetDeleteFutureCancellation()) {
                return;
            }
        }

        SetDone(true);
    }));
}

bool FileManagement::IsFile(const wstring& path)
{
    DWORD fileType = GetFileAttributes(path.c_str());

    if (fileType == INVALID_FILE_ATTRIBUTES || fileType & FILE_ATTRIBUTE_DIRECTORY) {
        return false;
    }

    return true;
}

bool FileManagement::FileExists(const wstring& path)
{
    DWORD fileType = GetFileAttributes(path.c_str());

    if (fileType == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    return true;
}

bool FileManagement::DirectoryExists(const wstring& path)
{
	DWORD fileType = GetFileAttributes(path.c_str());

	if (fileType == INVALID_FILE_ATTRIBUTES || !(fileType & FILE_ATTRIBUTE_DIRECTORY)) {
		return false;
	}

	return true;
}

void FileManagement::KillProcessesOfFile(const wstring& path)
{
    auto pids = FileLockFinder::FindLockingProcesses(path);
    for (auto pid : pids) {
        KillProcess(pid);
    }
}

void FileManagement::KillProcess(DWORD pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (hProcess == NULL) {
		return;
	}
	TerminateProcess(hProcess, 0);
	CloseHandle(hProcess);
}
