#pragma once

#include <Arduino.h>
#include "FastMotionProtocol3Axis.h"

namespace fastmotion3 {

class FastMotionNode3Axis {
public:
    static const uint8_t AXIS_COUNT = 3;
    static const uint32_t TIMER_TICK_HZ = 20000UL;

    struct Config {
        uint32_t baudRate;
        int32_t maxAbsStepRate;
        uint16_t watchdogMs;
        uint32_t statusPeriodUs;
        bool releaseCoilsWhenDisabled;

        Config();
    };

    FastMotionNode3Axis();

    // Local mode: Timer1 and the three axes are active, but Serial is untouched.
    void beginLocal(const Config& config = Config());

    // Binary mode: also opens Serial and enables the 28/48-byte protocol.
    void beginBinary(const Config& config = Config());

    // Call frequently from loop(). Step timing does not depend on this call;
    // Timer1 generates steps autonomously.
    void poll();

    // Local/manual control API. Axis index is 0, 1 or 2.
    void setEnabled(uint8_t axis, bool enabled);
    void setQuickStop(uint8_t axis, bool active);
    void setVelocityStepsPerSecond(uint8_t axis, int32_t velocity);
    void resetPosition(uint8_t axis);
    void stopAll(bool disableOutputs = true);

    int32_t positionSteps(uint8_t axis) const;
    int32_t appliedVelocityStepsPerSecond(uint8_t axis) const;
    uint16_t axisStatus(uint8_t axis) const;
    uint16_t axisError(uint8_t axis) const;

    bool communicationActive() const;
    bool watchdogOk() const;
    uint16_t acknowledgedSequence() const;
    uint32_t validCommandCount() const;
    uint32_t crcErrorCount() const;
    uint32_t formatErrorCount() const;

    static void onTimerCompareISR();

private:
    struct FastPin {
        volatile uint8_t* outputRegister;
        uint8_t mask;
    };

    struct AxisRuntime {
        FastPin pins[4];
        volatile int32_t requestedVelocity;
        volatile int32_t appliedVelocity;
        volatile int32_t position;
        volatile uint32_t phaseAccumulator;
        volatile uint8_t patternIndex;
        volatile bool enabled;
        volatile bool quickStop;
        volatile bool running;
        volatile bool coilsEnergized;
        volatile bool velocityClamped;
        volatile uint16_t error;
    };

    Config config_;
    AxisRuntime axes_[AXIS_COUNT];
    bool binaryMode_;

    uint8_t commandFrame_[protocol::COMMAND_SIZE];
    uint8_t parserIndex_;
    uint16_t lastSequence_;
    uint16_t currentWatchdogMs_;
    uint32_t lastValidCommandMs_;
    uint32_t lastStatusUs_;
    bool everReceivedValidCommand_;
    bool watchdogOk_;
    bool statusPending_;
    uint16_t nodeError_;

    uint32_t validCommandCount_;
    uint32_t crcErrorCount_;
    uint32_t formatErrorCount_;

    static FastMotionNode3Axis* instance_;

    void initializePins();
    void initializeTimer1();
    void parseAvailableSerialBytes();
    void parseByte(uint8_t value);
    void acceptCommandFrame();
    void serviceWatchdog();
    void sendStatusIfDue();
    void buildStatusFrame(uint8_t* frame) const;

    void applyAxisCommand(uint8_t axis, bool enabled, bool quickStop,
                          bool reset, int32_t velocity);
    void safeStopFromWatchdog();

    static void writeFastPin(const FastPin& pin, bool high);
    void writeCoilPatternFromISR(uint8_t axis, uint8_t patternIndex);
    void releaseCoilsFromISR(uint8_t axis);
    void serviceAxisFromISR(uint8_t axis);

    static bool validAxis(uint8_t axis);
};

}  // namespace fastmotion3
