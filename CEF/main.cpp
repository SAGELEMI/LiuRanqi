#include "CEF调用.h"

int main(int argc, char* argv[]) {
    void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
    CefScopedSandboxInfo scoped_sandbox;
    sandbox_info = scoped_sandbox.sandbox_info();
#endif
    return CEFjihe::运行(argc, argv, sandbox_info);
}
