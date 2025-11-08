# Git仓库设置说明

由于路径中包含中文字符，PowerShell在处理时可能出现编码问题。请按照以下步骤手动设置：

## 方法1：使用Git Bash（推荐）

1. 打开Git Bash
2. 切换到项目目录：
   ```bash
   cd "/e/MY WORK/随时存取/7文件传递/GP2040/GP2040-CE-leonxis"
   ```

3. 初始化Git仓库：
   ```bash
   git init
   ```

4. 添加远程仓库：
   ```bash
   git remote add origin https://YOUR_TOKEN@github.com/leonxis/GP2040-CE.git
   ```

5. 验证远程仓库：
   ```bash
   git remote -v
   ```

## 方法2：使用文件资源管理器

1. 在文件资源管理器中导航到项目目录：
   `E:\MY WORK\随时存取\7文件传递\GP2040\GP2040-CE-leonxis`

2. 在地址栏中输入 `cmd` 并按回车，这将在此目录打开命令提示符

3. 执行以下命令：
   ```cmd
   git init
   git remote add origin https://YOUR_TOKEN@github.com/leonxis/GP2040-CE.git
   git remote -v
   ```

## 远程仓库信息

- 远程仓库地址：`https://github.com/leonxis/GP2040-CE.git`
- 认证Token已包含在URL中

## 下一步操作

设置完成后，可以执行：
```bash
git add .
git commit -m "Initial commit"
git push -u origin main
```

注意：如果远程仓库已有内容，可能需要先执行 `git pull` 或 `git fetch`。

