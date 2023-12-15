#include "FileManagement.h"

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
            LatestScanFile = fullPath;
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push_back(fullPath);
                vector<wstring> subDirs = GetAllNeededPaths(fullPath, cancellation);
                connected.insert(connected.begin(), subDirs.begin(), subDirs.end());
            }
            else {
                files.push_back(fullPath);
            }
        } while (FindNextFile(hFind, &findFileData) != 0 && !cancellation);
        FindClose(hFind);
    }

    connected.insert(connected.end(), files.begin(), files.end());
    connected.insert(connected.end(), dirs.begin(), dirs.end());

    return connected;
}

void FileManagement::Delete(const wstring& path)
{
}

bool FileManagement::IsFile(const wstring& path)
{
    DWORD fileType = GetFileAttributes(path.c_str());

    if (fileType == INVALID_FILE_ATTRIBUTES || fileType & FILE_ATTRIBUTE_DIRECTORY) {
        return false;
    }

    return true;
}
