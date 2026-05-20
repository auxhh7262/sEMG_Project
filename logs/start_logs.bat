@echo off
chcp 65001 > nul
echo ========================================
echo   sEMG 日志监控 - 固件串口 + 小程序
echo ========================================
echo.
start "串口日志" cmd /k "cd /d E:\Personal\sEMG_Project\logs && python serial_monitor.py"
echo  [串口] COM4 115200 → serial_log.txt
echo.
start "小程序日志" cmd /k "cd /d E:\Personal\sEMG_Project\logs && python mini_log_server.py"
echo  [小程序] HTTP :9876 → mini_log.txt
echo.
echo 两个窗口已打开，Ctrl+C 关闭
echo ========================================
