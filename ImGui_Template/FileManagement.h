#pragma once

#include <Windows.h>
#include <string>
#include <vector>

using namespace std;

class FileManagement
{
private:

public:
	vector<wstring> GetAllNeededPaths(const wstring& path);
	void Delete(const wstring& path);
	bool IsFile(const wstring& path);
};

