#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
    std::filesystem::path self_dir;
    if (argc > 0 && argv[0] != nullptr) {
        self_dir = std::filesystem::path(argv[0]).parent_path();
    } else {
        self_dir = std::filesystem::current_path();
    }

    const std::filesystem::path browser_exe = self_dir / L"浏览器.exe";
    if (!std::filesystem::exists(browser_exe)) {
        std::wcerr << L"未找到: " << browser_exe.wstring() << std::endl;
        return 1;
    }

    std::wstring command_line = L"\"" + browser_exe.wstring() + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(
            nullptr,
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            self_dir.wstring().c_str(),
            &si,
            &pi)) {
        std::wcerr << L"启动失败，Win32错误码: " << GetLastError() << std::endl;
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
