@echo off
chcp 65001 >nul
cd /d "E:\MY WORK\随时存取\7文件传递\GP2040\GP2040-CE-leonxis"
if exist .git rmdir /s /q .git
git init
git remote add origin https://YOUR_TOKEN@github.com/leonxis/GP2040-CE.git
git remote -v
echo.
echo Git repository initialized and remote added successfully!
pause

