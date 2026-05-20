#pragma once
#include <Arduino.h>
// 仅用于极简的变量赋值保护，严禁包裹复杂逻辑！
#define ENTER_CRITICAL() noInterrupts()
#define EXIT_CRITICAL() interrupts()
