# =========================
# 输出目录统一
# =========================

# BIN_OUTPUT_DIR：所有目标（exe/lib/dll）统一输出目录，避免分散到各子目录。
set(BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/Release")
# CMAKE_RUNTIME_OUTPUT_DIRECTORY：可执行文件（.exe）默认输出目录。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
# CMAKE_LIBRARY_OUTPUT_DIRECTORY：动态库（.dll/.so）默认输出目录。
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY：静态库（.lib/.a）默认输出目录。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")