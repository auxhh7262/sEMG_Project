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

// SPI时钟：电阻分压方式（5V→3.3V），保守用1MHz
#define FLASH_SPI_SETTINGS SPISettings(1000000, MSBFIRST, SPI_MODE0)
#define CMD_JEDEC_ID          0x9F
#define CMD_GLOBAL_UNLOCK     0x98
#define CMD_WREN_VOLATILE_SR  0x50
#define CMD_WRITE_STATUS1     0x01

FlashDriver::FlashDriver() {}

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
    while (SPI.transfer(0) & 0x01);
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
    // ===== 增强诊断 v2：逐项排查 SPI Flash 通信 =====
    LOG("[FLASH] === Enhanced SPI Diagnostics v2 ===\n");

    // Step 1: CS 引脚电平测试
    pinMode(FLASH_CS_PIN, OUTPUT);
    csHigh();
    delay(1);
    uint8_t cs_high = digitalRead(FLASH_CS_PIN);
    csLow();
    delay(1);
    uint8_t cs_low = digitalRead(FLASH_CS_PIN);
    csHigh();
    LOG("[FLASH] DIAG CS: HIGH=%d LOW=%d (expect 1,0)\n", cs_high, cs_low);

    // Step 2: SPI 引脚电平扫描（不初始化SPI，纯GPIO读取）
    pinMode(PIN_SPI_MOSI, INPUT);
    pinMode(PIN_SPI_MISO, INPUT);
    pinMode(PIN_SPI_SCK, INPUT);
    LOG("[FLASH] DIAG GPIO idle: MOSI=%d MISO=%d SCK=%d CS=%d\n",
         digitalRead(PIN_SPI_MOSI), digitalRead(PIN_SPI_MISO),
         digitalRead(PIN_SPI_SCK), digitalRead(FLASH_CS_PIN));

    // Step 3: 初始化 SPI
    SPI.begin();
    delay(5);

    // Step 4: MISO 空闲电平（SPI已初始化，CS未拉低）
    pinMode(PIN_SPI_MISO, INPUT);
    uint8_t miso_before = digitalRead(PIN_SPI_MISO);
    LOG("[FLASH] DIAG MISO before CS low=%d (expect Hi-Z→0 or 1)\n", miso_before);

    // Step 5: Release from power-down
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_RELEASE_PD);
    csHigh();
    SPI.endTransaction();
    delay(1);

    // Step 6: MISO after release
    pinMode(PIN_SPI_MISO, INPUT);
    uint8_t miso_after_release = digitalRead(PIN_SPI_MISO);
    LOG("[FLASH] DIAG MISO after release=%d\n", miso_after_release);

    // Step 7: JEDEC ID — 用 0xFF 填充（区分 Flash 返回0 vs TXB0108 堵0）
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_JEDEC_ID);
    uint8_t mid = SPI.transfer(0xFF);
    uint8_t tid = SPI.transfer(0xFF);
    uint8_t cid = SPI.transfer(0xFF);
    csHigh();
    SPI.endTransaction();
    LOG("[FLASH] DIAG JEDEC(0xFF fill): M=0x%02X T=0x%02X C=0x%02X\n", mid, tid, cid);
    // 如果全是0xFF：MISO被上拉→TXB0108方向未建立或OE悬空
    // 如果全是0x00：MISO被下拉或接地→接线错误
    // 如果正常：M=0xEF T=0x40 C=0x18

    // Step 8: Status Register
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_READ_STATUS1);
    uint8_t sr1 = SPI.transfer(0xFF);
    csHigh();
    SPI.endTransaction();
    LOG("[FLASH] DIAG SR1(0xFF fill)=0x%02X\n", sr1);

    // Step 9: CS 拉低时 MISO 电平
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    delayMicroseconds(10);
    uint8_t miso_cs_low = digitalRead(PIN_SPI_MISO);
    csHigh();
    SPI.endTransaction();
    LOG("[FLASH] DIAG MISO with CS low=%d\n", miso_cs_low);

    // Step 10: 原始 getChipID（用0x00填充，与之前一致）
    uint32_t jedecId = getChipID();
    LOG("[FLASH] JEDEC ID: 0x%06X (expect 0xEF4018)\n", jedecId);

    if (fullUnlock) {
        executeGoldenThreeSteps();
        LOG("[FLASH] Driver initialized (full unlock)\n");
    } else {
        LOG("[FLASH] Driver initialized (read-only safe)\n");
    }
}

uint32_t FlashDriver::getChipID() {
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_JEDEC_ID);
    uint8_t m = SPI.transfer(0);
    uint8_t t = SPI.transfer(0);
    uint8_t c = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return (m << 16) | (t << 8) | c;
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
        buf[i] = SPI.transfer(0);
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
    waitBusy();  // 确保前序操作完成
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_SECTOR_ERASE);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    csHigh();
    SPI.endTransaction();
    // 不等 waitBusy()，立即返回
}

void FlashDriver::blockErase64KAsync(uint32_t addr) {
    waitBusy();  // 确保前序操作完成
    writeEnable();
    SPI.beginTransaction(FLASH_SPI_SETTINGS);
    csLow();
    SPI.transfer(CMD_BLOCK_ERASE_64K);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    csHigh();
    SPI.endTransaction();
    // 不等 waitBusy()，立即返回
}
