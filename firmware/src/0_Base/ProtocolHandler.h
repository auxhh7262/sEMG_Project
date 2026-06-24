#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <Arduino.h>
#include "0_Base/Globals.h"
#include "0_Base/Board.h"

class ProtocolHandler {
public:
    void init();
    void tick();

    // 下行指令解析 (本地调试)
    AppCommand_t tickLocalDebug();

    // JSON 命令解析 (WebSocket)
    void handleJsonCommand(uint8_t clientNum, const char* json);

    // 上行状态发送 (串口透传占位)
    void sendStatus(const char* stateName);
    void sendStatus(const char* stateName, uint8_t progress);
    void sendCalResult(bool success, float restRms, float refRms, const char* errMsg = nullptr);
    void sendData(float rms, float fatigue);
    void sendError(const char* msg);

private:
    // 【新增】串口指令解析缓冲区，解决内存紧张时单字符丢失问题
    char _debugBuf[100];  // 增大缓冲支持 "w SSID password" 命令（最多 31+1+63=95 字节）
    uint8_t _debugBufIdx;
};

#endif // PROTOCOL_HANDLER_H