#include "SystemStateMachine.h"
#include "0_Base/Logger.h"

void StateManager::init()
{
    _state = ST_BOOT;
    _prevState = ST_BOOT;
    _phaseActive = false;
    _phaseDurationMs = 0;
    _errorMsg[0] = '\0';
}

bool StateManager::_validTransition(SystemState_t from, SystemState_t to)
{
    if (from == ST_BOOT) return (to == ST_IDLE || to == ST_MONITORING);
    if (from == ST_IDLE) return (to == ST_CALIB_REST || to == ST_MONITORING || to == ST_DB_FEATURE);
    if (from == ST_CALIB_REST) return (to == ST_CALIB_WAIT || to == ST_IDLE);
    if (from == ST_CALIB_WAIT) return (to == ST_CALIB_MAX || to == ST_IDLE);
    if (from == ST_CALIB_MAX) return (to == ST_CALIB_DONE || to == ST_IDLE);
    if (from == ST_CALIB_DONE) return (to == ST_MONITORING || to == ST_IDLE);
    if (from == ST_MONITORING) return (to == ST_IDLE || to == ST_DB_FEATURE);
    if (from == ST_DB_FEATURE) return (to == ST_IDLE);
    if (from == ST_ERROR) return (to == ST_IDLE);
    return false;
}

bool StateManager::transitionTo(SystemState_t newState)
{
    if (!_validTransition(_state, newState)) {
        snprintf(_errorMsg, sizeof(_errorMsg), "Bad transition from %s", getStateName());
        LOG("[STATE] ERROR: %s\n", _errorMsg);
        _prevState = _state;
        _state = ST_ERROR;
        return false;
    }

    LOG("[STATE] %s -> %s\n", getStateName(),
        newState == ST_IDLE        ? "IDLE" :
        newState == ST_CALIB_REST  ? "CALIB_REST" :
        newState == ST_CALIB_WAIT  ? "CALIB_WAIT" :
        newState == ST_CALIB_MAX   ? "CALIB_MAX" :
        newState == ST_CALIB_DONE  ? "CALIB_DONE" :
        newState == ST_MONITORING  ? "MONITORING" :
        newState == ST_DB_FEATURE  ? "DB_FEATURE" : "???");

    _prevState = _state;
    _state = newState;
    _phaseActive = false;
    _errorMsg[0] = '\0';
    return true;
}

SystemState_t StateManager::getState() const
{
    return _state;
}

const char* StateManager::getStateName() const
{
    switch (_state) {
        case ST_BOOT:        return "BOOT";
        case ST_IDLE:        return "IDLE";
        case ST_CALIB_REST:  return "CALIB_REST";
        case ST_CALIB_WAIT:  return "CALIB_WAIT";
        case ST_CALIB_MAX:   return "CALIB_MAX";
        case ST_CALIB_DONE:  return "CALIB_DONE";
        case ST_MONITORING:  return "MONITORING";
        case ST_DB_FEATURE:  return "DB_FEATURE";
        case ST_ERROR:       return "ERROR";
        default:             return "?";
    }
}

void StateManager::startCalibPhase(uint16_t durationSec)
{
    _phaseStartMs = millis();
    _phaseDurationMs = (uint32_t)durationSec * 1000UL;
    _phaseActive = true;
}

bool StateManager::isCalibPhaseComplete() const
{
    if (!_phaseActive) {
        // [DEBUG] 只在前10次打印（避免刷屏）
        static uint8_t _dbgCount = 0;
        if (_dbgCount++ < 10) {
            LOG("[STATE] isCalibPhaseComplete: NOT active (state=%s)\n", getStateName());
        }
        return false;
    }
    uint32_t elapsed = millis() - _phaseStartMs;
    if (elapsed >= _phaseDurationMs) {
        LOG("[STATE] Phase COMPLETE! elapsed=%lu >= dur=%lu\n",
            (unsigned long)elapsed, (unsigned long)_phaseDurationMs);
        return true;
    }
    return false;
}

uint8_t StateManager::getCalibProgress() const
{
    if (!_phaseActive) return 0;
    uint32_t elapsed = millis() - _phaseStartMs;
    if (elapsed >= _phaseDurationMs) return 100;
    // �ȳ���˱��������������ʧ�ɽ��ܣ�У׼�׶�ͨ�� 3-10 �룩
    uint8_t pct = (uint8_t)((elapsed * 100UL) / _phaseDurationMs);
    return pct;
}

void StateManager::setError(const char* msg)
{
    strncpy(_errorMsg, msg, sizeof(_errorMsg) - 1);
    _errorMsg[sizeof(_errorMsg) - 1] = '\0';
    LOG("[STATE] ERROR: %s\n", _errorMsg);
    _prevState = _state;
    _state = ST_ERROR;
}

const char* StateManager::getErrorMsg() const
{
    return _errorMsg;
}

void StateManager::tick()
{
    // 预留：超时保护、心跳等
}
