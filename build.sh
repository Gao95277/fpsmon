#!/bin/bash
# 手动搭建 MSVC 编译环境（不调用 vcvars64.bat，避免其内部的 reg.exe 查询）
set -e
MSVC_WIN="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207"
SDK_WIN="C:/Program Files (x86)/Windows Kits/10"
SDKVER="10.0.26100.0"

export PATH="/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64:$PATH"
export INCLUDE="$MSVC_WIN/include;$SDK_WIN/Include/$SDKVER/ucrt;$SDK_WIN/Include/$SDKVER/um;$SDK_WIN/Include/$SDKVER/shared;$SDK_WIN/Include/$SDKVER/winrt"
export LIB="$MSVC_WIN/lib/x64;$SDK_WIN/Lib/$SDKVER/ucrt/x64;$SDK_WIN/Lib/$SDKVER/um/x64"

exec cl -nologo -EHsc -std:c++20 -W3 -DUNICODE -D_UNICODE -utf-8 "$@"
