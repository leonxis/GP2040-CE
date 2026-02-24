#!/bin/bash
# ============================================================
# Cursor 与 Git SOCKS 代理设置脚本
# 用法：在下方修改 SOCKS_PROXY 后执行 ./set-socks-proxy.sh
# ============================================================

# ---------- 在此修改为你的 SOCKS 代理（格式：socks5://IP:端口）----------
SOCKS_PROXY="socks5://192.168.2.50:10808"
# -------------------------------------------------------------------------

set -e

echo "使用代理: $SOCKS_PROXY"
echo ""

# --- 1. 设置 Cursor 代理 ---
CURSOR_SETTINGS="${HOME}/.config/Cursor/User/settings.json"
CURSOR_DIR="$(dirname "$CURSOR_SETTINGS")"

if [[ ! -d "$CURSOR_DIR" ]]; then
  echo "[Cursor] 配置目录不存在，正在创建: $CURSOR_DIR"
  mkdir -p "$CURSOR_DIR"
fi

if [[ ! -f "$CURSOR_SETTINGS" ]]; then
  echo "[Cursor] settings.json 不存在，正在创建并写入代理。"
  echo "{\"http.proxy\": \"$SOCKS_PROXY\", \"http.proxySupport\": \"override\", \"http.proxyStrictSSL\": false}" > "$CURSOR_SETTINGS"
else
  echo "[Cursor] 正在更新 settings.json 中的代理..."
  python3 - "$CURSOR_SETTINGS" "$SOCKS_PROXY" << 'PY'
import json, sys
path, proxy = sys.argv[1], sys.argv[2]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)
data["http.proxy"] = proxy
data["http.proxySupport"] = data.get("http.proxySupport", "override")
data["http.proxyStrictSSL"] = data.get("http.proxyStrictSSL", False)
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
PY
fi
echo "[Cursor] 已设置 http.proxy = $SOCKS_PROXY"
echo ""

# --- 2. 设置 Git 代理 ---
echo "[Git] 正在设置全局 http/https 及 GitHub 代理..."
git config --global http.proxy "$SOCKS_PROXY"
git config --global https.proxy "$SOCKS_PROXY"
git config --global http.https://github.com.proxy "$SOCKS_PROXY"
git config --global https.https://github.com.proxy "$SOCKS_PROXY"
echo "[Git] 已设置 http.proxy / https.proxy / http.https://github.com.proxy / https.https://github.com.proxy"
echo ""

echo "全部完成。Cursor 若已打开，请重新加载窗口或重启 Cursor 使代理生效。"
