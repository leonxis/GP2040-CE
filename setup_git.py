#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import subprocess
import sys

# 项目目录路径
project_dir = r"E:\MY WORK\随时存取\7文件传递\GP2040\GP2040-CE-leonxis"

# 检查目录是否存在
if not os.path.exists(project_dir):
    print(f"错误: 目录不存在: {project_dir}")
    sys.exit(1)

# 切换到项目目录
os.chdir(project_dir)
print(f"当前工作目录: {os.getcwd()}")

# 如果存在.git目录，先删除
if os.path.exists(".git"):
    import shutil
    shutil.rmtree(".git")
    print("已删除现有的.git目录")

# 初始化git仓库
try:
    subprocess.run(["git", "init"], check=True)
    print("Git仓库初始化成功")
except subprocess.CalledProcessError as e:
    print(f"Git初始化失败: {e}")
    sys.exit(1)

# 添加远程仓库
remote_url = "https://YOUR_TOKEN@github.com/leonxis/GP2040-CE.git"
try:
    # 检查是否已存在origin
    result = subprocess.run(["git", "remote", "-v"], capture_output=True, text=True)
    if "origin" in result.stdout:
        subprocess.run(["git", "remote", "set-url", "origin", remote_url], check=True)
        print("已更新远程仓库地址")
    else:
        subprocess.run(["git", "remote", "add", "origin", remote_url], check=True)
        print("已添加远程仓库")
    
    # 显示远程仓库信息
    subprocess.run(["git", "remote", "-v"], check=True)
except subprocess.CalledProcessError as e:
    print(f"添加远程仓库失败: {e}")
    sys.exit(1)

print("\nGit仓库设置完成！")

