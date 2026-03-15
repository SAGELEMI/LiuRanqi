#include "CEF/CEFCall.h"
#include "SDL/SdlRuntime.h"
#include <string_view>

// 判断命令行里是否存在指定前缀的参数。
static bool HasArgumentPrefix(int argc, char* argv[], std::string_view prefix) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string_view(argv[i]).rfind(prefix, 0) == 0) return true;
    }
    return false;
}

// 判断命令行里是否存在和指定值完全相等的参数。
static bool HasArgument(int argc, char* argv[], std::string_view exact_value) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string_view(argv[i]) == exact_value) return true;
    }
    return false;
}

// 程序入口，CEF 子进程走 CEFMain，主进程走 SDL 主流程。
int main(int argc, char* argv[]) {
    // CEF 内部子进程都会带 --type=xxx，你手动拉起的 CEF 浏览器进程带 --lrq-cef-host。
    if (HasArgumentPrefix(argc, argv, "--type=") || HasArgument(argc, argv, "--lrq-cef-host")) {
        return CEFjihe::CEFMain(argc, argv);
    }
    return RunSdlApp(argc, argv);
}
