#!/bin/bash
# 将 origin 的 HML1.0 分支同步到本地
set -e
cd "$(dirname "$0")"

echo "正在从 origin 拉取..."
git fetch origin

if git show-ref --verify --quiet refs/remotes/origin/HML1.0; then
  if git show-ref --verify --quiet refs/heads/HML1.0; then
    echo "本地已有 HML1.0，正在更新..."
    git checkout HML1.0
    git pull origin HML1.0
  else
    echo "创建本地 HML1.0 并跟踪 origin/HML1.0..."
    git checkout -b HML1.0 origin/HML1.0
  fi
  echo "完成。当前分支: $(git branch --show-current)"
else
  echo "错误: origin 上未找到 HML1.0 分支，请检查远程与网络。"
  exit 1
fi
