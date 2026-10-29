#!/bin/bash
# 发版脚本：编译 → 更新 dist → 打绿包 → 提交 → 推送 → GitHub Release
#
# 用法: bash release.sh <版本号> ["更新说明"]
# 例:   bash release.sh 1.0.1 "修正高帧率下的丢帧"
#
# 每次发版固定产出三样并推送到 GitHub：
#   1. 源码
#   2. 编译好的 dist/fpsmon.exe
#   3. 绿色版 dist/FpsMon-v<版本>.zip
set -e
cd "$(dirname "$0")"

VER="${1:?用法: bash release.sh <版本号> [说明]}"
NOTE="${2:-版本 $VER}"
SEVENZIP="/c/MingSoftware/7-Zip/7z.exe"
GH="/c/Program Files/GitHub CLI/gh.exe"

echo "[1/6] 编译"
taskkill //F //IM fpsmon.exe >/dev/null 2>&1 || true
sleep 1
bash build.sh -O2 -Fe:build/fpsmon.exe -Fo:build/ \
    src/main.cpp src/overlay.cpp src/frame_source.cpp src/sysinfo.cpp

echo "[2/6] 更新 dist"
mkdir -p dist
rm -f dist/FpsMon-v*.zip
cp build/fpsmon.exe dist/fpsmon.exe

echo "[3/6] 打包绿色版"
rm -rf pack && mkdir -p pack/FpsMon
cp build/fpsmon.exe pack/FpsMon/
cp README.txt pack/FpsMon/
(cd pack && "$SEVENZIP" a -tzip "../dist/FpsMon-v$VER.zip" FpsMon >/dev/null)

echo "[4/6] 提交"
git add -A
git commit -m "Release v$VER: $NOTE" || echo "（工作区无改动，跳过提交）"

echo "[5/6] 推送"
git push

echo "[6/6] 创建 Release"
"$GH" release create "v$VER" --title "v$VER" --notes "$NOTE" \
    dist/fpsmon.exe "dist/FpsMon-v$VER.zip"

echo
echo "完成: https://github.com/Gao95277/fpsmon/releases/tag/v$VER"
