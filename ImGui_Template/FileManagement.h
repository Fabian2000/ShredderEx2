#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <future>

using namespace std;

class FileManagement
{
private:

public:
	vector<wstring> GetAllNeededPaths(const wstring& path, atomic<bool>* cancellation);
	void Delete(const wstring& path);
	bool IsFile(const wstring& path);
	wstring LatestScanFile;
};

