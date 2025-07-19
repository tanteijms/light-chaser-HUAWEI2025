@echo off
chcp 65001 >nul
echo 华为嵌入式软件大赛代码测试
echo ================================

echo 正在运行快速测试...
python quick_test.py

if %errorlevel% neq 0 (
    echo.
    echo 快速测试失败，正在运行详细测试...
    python test_runner.py
)

echo.
echo 测试完成，按任意键退出...
pause >nul
