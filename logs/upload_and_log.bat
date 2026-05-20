@echo off
chcp 65001 > nul
echo ========================================
echo   sEMG 固件上传 + 监控
echo ========================================
echo.
echo [1/2] 编译上传固件...
cd /d E:\Personal\sEMG_Project\firmware
C:\Users\honghuang\.platformio\penv\Scripts\pio.exe run --target upload
if %ERRORLEVEL% NEQ 0 (
    echo 上传失败！
    pause
    exit /b 1
)
echo.
echo [2/2] 启动串口监控 (需手动物理复位设备)...
cd /d E:\Personal\sEMG_Project\logs
python serial_monitor.py
