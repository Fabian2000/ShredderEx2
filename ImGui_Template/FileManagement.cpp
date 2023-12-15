#include "FileManagement.h"

vector<wstring> FileManagement::GetAllNeededPaths(const wstring& path)
{
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(path.c_str(), &findFileData);
    vector<wstring> files;
    vector<wstring> dirs;
    if (hFind == INVALID_HANDLE_VALUE) {
        return vector<wstring>();
    }
    else {
        do {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push_back(findFileData.cFileName);
            }
            else {
                files.push_back(findFileData.cFileName);
            }
        } while (FindNextFile(hFind, &findFileData) != 0);
        FindClose(hFind);
    }

    vector<wstring> connected(files.begin(), files.end());
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
