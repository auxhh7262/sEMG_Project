#pragma once
// 防止版本失控与时序漂移
#define FIRMWARE_VERSION "1.0"

// 防除零保护，与前端 JS 绝对一致 (当前 mV 单位下对应 0.5f)
#define MIN_VALID_DIFF 0.5f
