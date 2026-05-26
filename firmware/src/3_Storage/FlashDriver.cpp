#include "FlashDriver.h"
#include "0_Base/Logger.h"

#ifndef FLASH_CS_PIN
#define FLASH_CS_PIN PIN_SPI_FLASH_CS
#endif

// W25Q128 SPI 指令集
#define CMD_READ_DATA         0x03
#define CMD_PAGE_PROGRAM      0x02
#define CMD_SECTOR_ERASE      0x20
#define CMD_BLOCK_ERASE_64K   0xD8
#define CMD_READ_STATUS1      0x05
#define CMD_WRITE_ENABLE      0x06
#define CMD_RELEASE_PD        0xAB
#define CMD_JEDEC_ID          0x9F
#define CMD_READ_MANU_DEV     0x90  // 另一种读取制造商/设备ID的方式
#define CMD_GLOBAL_UNLOCK     0x98
#define CMD_WREN_VOLATILE_SR  0x50
#define CMD_WRITE_STATUS1     0x01

// SPI时钟：1MHz（提高速度可能改善M=0x0B问题）
#define FLASH_SPI_SETTINGS SPISettings(1000000, MSBFIRST, SPI_MODE3)  // 切回1MHz

FlashDriver::FlashDriver() : _initOk(false) {}  // 初始化 _initOk 为 false

FlashDriver& FlashDriver::instance() {
    static FlashDriver inst;
    return inst;
}

void FlashDriver::csLow()  { digitalWrite(FLASH_CS_PIN, LOW); }
void FlashDriver::csHigh() { digitalWrite(FLASH_CS_PIN, HIGH); }

void FlashDriver::writeEnable() {
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_WRITE_ENABLE);
    csHigh();
    SPI.endTransaction();
}

void FlashDriver::waitBusy() {
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_STATUS1);
    uint32_t startMs = millis();
    while (SPI.transfer(0) & 0x01) {
        if (millis() - startMs > 10000) {  // 10秒超时，覆盖sector erase (400ms) 和 block erase (1.5s)
            csHigh();
            SPI.endTransaction();
            LOG("[FLASH] waitBusy TIMEOUT!\n");
            return;
        }
        delay(1);  // 减少SPI总线占用
    }
    csHigh();
    SPI.endTransaction();
}

bool FlashDriver::isBusy() {
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_STATUS1);
    uint8_t s = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return (s & 0x01) != 0;
}

void FlashDriver::executeGoldenThreeSteps() {
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_GLOBAL_UNLOCK);
    csHigh();
    SPI.endTransaction();
    waitBusy();

    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_WREN_VOLATILE_SR);
    csHigh();
    SPI.endTransaction();

    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_STATUS1);
    uint8_t sr1 = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    sr1 &= ~0x1C;

    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_WRITE_STATUS1);
    SPI.transfer(sr1);
    csHigh();
    SPI.endTransaction();
    waitBusy();
}

void FlashDriver::init(bool fullUnlock) {
    pinMode(FLASH_CS_PIN, OUTPUT);
    csHigh();
    
    // 1. 初始化 SPI（先不beginTransaction）
    SPI.begin();
    delay(5);  // 【关键】上电稳定延迟（spec tVSL ≥ 20µs，保守给 5ms）
    
    pinMode(FLASH_CS_PIN, OUTPUT);
    csHigh();
    
    LOG("[FLASH] === SPI Flash Init ===\n");
    LOG("[FLASH] CS_PIN=%d, MOSI=%d, MISO=%d, SCK=%d\n", 
        FLASH_CS_PIN, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK);
    
    // 测试1: 检查 CS 电平控制
    csLow();
    delayMicroseconds(10);
    uint8_t cs_low_val = digitalRead(FLASH_CS_PIN);
    csHigh();
    uint8_t cs_high_val = digitalRead(FLASH_CS_PIN);
    LOG("[FLASH] CS test: LOW=%d HIGH=%d (expect 0,1)\n", cs_low_val, cs_high_val);
    
    // 测试2: 发送 Release Power-Down 命令 (0xAB)
    // 【关键修复】endTransaction 必须在 delay 之后，否则 SCK 被拉低破坏 Mode3 时序
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_RELEASE_PD);
    csHigh();
    delay(5);  // 【关键】保持 endTransaction 状态，SCK 维持 Mode3 高电平
    SPI.endTransaction();
    LOG("[FLASH] Release Power-Down sent, waited 5ms\n");
    
    // 测试3: 读状态寄存器（验证SPI通信）
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_STATUS1);
    uint8_t sr1 = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    LOG("[FLASH] Status Register 1: 0x%02X\n", sr1);
    
    // 测试4: 多次读取 JEDEC ID（取多数表决）
    LOG("[FLASH] Reading JEDEC ID 3 times...\n");
    uint8_t mids[3], tids[3], cids[3];
    for (int i = 0; i < 3; i++) {
        SPI.beginTransaction(FLASH_SPI_SETTINGS);
        csLow();
        uint8_t cmd_response = SPI.transfer(CMD_JEDEC_ID);  // 发送0x9F命令，同时接收Hi-Z状态（应为0xFF）
        mids[i] = SPI.transfer(0xFF);  // Dummy，同时Flash送出MID
        tids[i] = SPI.transfer(0xFF);  // Dummy，同时Flash送出Type ID
        cids[i] = SPI.transfer(0xFF);  // Dummy，同时Flash送出Capacity
        csHigh();
        SPI.endTransaction();
        LOG("[FLASH]   CMD response: 0x%02X (expect 0xFF - Hi-Z during command)\n", cmd_response);
        LOG("[FLASH]   Attempt %d: M=0x%02X T=0x%02X C=0x%02X\n", 
            i+1, mids[i], tids[i], cids[i]);
        delay(1);
    }
    
    // 取多数表决
    uint8_t mid = (mids[0] == mids[1]) ? mids[0] : mids[2];
    uint8_t tid = (tids[0] == tids[1]) ? tids[0] : tids[2];
    uint8_t cid = (cids[0] == cids[1]) ? cids[0] : cids[2];
    
    LOG("[FLASH] JEDEC ID (voted): M=0x%02X T=0x%02X C=0x%02X (expect EF 40 18)\n", 
        mid, tid, cid);
    
    uint32_t jedecId = ((uint32_t)mid << 16) | ((uint32_t)tid << 8) | cid;
    bool jedecOk = (mid == 0xEF) && (tid == 0x40) && (cid == 0x18);
    
    if (!jedecOk) {
        LOG("[FLASH] ERROR: JEDEC ID mismatch! Got 0x%06X, expected 0xEF4018\n", jedecId);
        
        // 额外诊断：尝试另一种命令读取ID
        LOG("[FLASH] Trying CMD_READ_MANU_DEV (0x90)...\n");
        SPI.beginTransaction(FLASH_SPI_SETTINGS);
        csLow();
        SPI.transfer(CMD_READ_MANU_DEV);
        SPI.transfer(0x00);  // Dummy 24-bit address
        SPI.transfer(0x00);
        SPI.transfer(0x00);
        uint8_t manu = SPI.transfer(0xFF);
        uint8_t dev = SPI.transfer(0xFF);
        csHigh();
        SPI.endTransaction();
        LOG("[FLASH] CMD_READ_MANU_DEV: Manufacturer=0x%02X Device=0x%02X\n", manu, dev);
    } else {
        LOG("[FLASH] JEDEC ID OK! Flash detected.\n");
    }
    
    if (fullUnlock && jedecOk) {
        executeGoldenThreeSteps();
        LOG("[FLASH] Driver initialized (full unlock)\n");
    } else if (jedecOk) {
        LOG("[FLASH] Driver initialized (read-only safe)\n");
    } else {
        LOG("[FLASH] Driver init FAILED - Flash not detected!\n");
    }
    
    _initOk = jedecOk;  // 记录初始化结果
}

uint32_t FlashDriver::getChipID() {
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_JEDEC_ID);
    uint8_t m = SPI.transfer(0xFF);  // 读取时发送 0xFF（空闲时钟线典型值）
    uint8_t t = SPI.transfer(0xFF);
    uint8_t c = SPI.transfer(0xFF);
    csHigh();
    SPI.endTransaction();
    return ((uint32_t)m << 16) | ((uint32_t)t << 8) | c;
}

void FlashDriver::diagnoseJedec() {
    LOG("[FLASH] === Runtime JEDEC + RW Test ===\n");
    // Release Power-Down
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_RELEASE_PD);
    csHigh();
    delay(5);  // 【关键】保持 endTransaction 状态，SCK 维持 Mode3 高电平
    SPI.endTransaction();
    
    // 1. JEDEC 读取
    uint8_t mids[3], tids[3], cids[3];
    for (int i = 0; i < 3; i++) {
        SPI.beginTransaction(FLASH_SPI_SETTINGS);
        csLow();
        uint8_t cmd_resp = SPI.transfer(CMD_JEDEC_ID);
        mids[i] = SPI.transfer(0xFF);
        tids[i] = SPI.transfer(0xFF);
        cids[i] = SPI.transfer(0xFF);
        csHigh();
        delay(1);  // 【关键】delay 必须在 endTransaction 之前，保持 SCK 高电平
        SPI.endTransaction();
        LOG("[FLASH]   JEDEC %d: CMD=0x%02X M=0x%02X T=0x%02X C=0x%02X\n",
            i+1, cmd_resp, mids[i], tids[i], cids[i]);
    }
    uint8_t mid = (mids[0]==mids[1])?mids[0]:mids[2];
    uint8_t tid = (tids[0]==tids[1])?tids[0]:tids[2];
    uint8_t cid = (cids[0]==cids[1])?cids[0]:cids[2];
    LOG("[FLASH]   Voted: M=0x%02X T=0x%02X C=0x%02X\n", mid, tid, cid);

    // 2. 测试性读写（地址0，模式1扇区）
    uint32_t testAddr = 256;  // 使用第2页（避开bootloader区域）
    uint8_t writeBuf[16];
    uint8_t readBuf[16];
    for (int i = 0; i < 16; i++) writeBuf[i] = 0xA5 ^ i;  // 0xA5, 0xA4, 0xA3...
    
    LOG("[FLASH]   Write test @ 0x%06X: ", testAddr);
    writeData(testAddr, writeBuf, 16);
    
    // 读回来验证
    readData(testAddr, readBuf, 16);
    bool rwOk = true;
    for (int i = 0; i < 16; i++) {
        if (readBuf[i] != writeBuf[i]) rwOk = false;
        LOG("%02X ", readBuf[i]);
    }
    LOG("\n");
    if (rwOk) {
        LOG("[FLASH]   RW Test: PASS - Flash read/write functional!\n");
    } else {
        LOG("[FLASH]   RW Test: FAIL - Data mismatch!\n");
    }
}

void FlashDriver::readData(uint32_t addr, uint8_t* buf, uint16_t len) {
    waitBusy();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_DATA);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = SPI.transfer(0xFF);  // 读取时发送 0xFF
    }
    csHigh();
    SPI.endTransaction();
}

bool FlashDriver::writeData(uint32_t addr, const uint8_t* buf, uint16_t len) {
    if (len > 256) {
        LOG("[FLASH] writeData: len %d > 256, truncated!\n", len);
        return false;
    }
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_PAGE_PROGRAM);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(buf[i]);
    }
    csHigh();
    SPI.endTransaction();
    waitBusy();
    return true;
}

// ===== 同步擦除（阻塞，仅限初始化等非实时场景）=====
void FlashDriver::sectorErase(uint32_t addr) {
    sectorEraseAsync(addr);
    waitBusy();
}

void FlashDriver::blockErase64K(uint32_t addr) {
    blockErase64KAsync(addr);
    waitBusy();
}

// ===== 异步擦除（发起后立即返回，主循环轮询 isBusy）=====
void FlashDriver::sectorEraseAsync(uint32_t addr) {
    waitBusy();
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_SECTOR_ERASE);
    SPI.transfer((addr >> 16) & 0xFF);  // 统一为 & 0xFF
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    csHigh();
    SPI.endTransaction();
}

void FlashDriver::blockErase64KAsync(uint32_t addr) {
    waitBusy();
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_BLOCK_ERASE_64K);
    SPI.transfer((addr >> 16) & 0xFF);  // 统一为 & 0xFF
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    csHigh();
    SPI.endTransaction();
}
