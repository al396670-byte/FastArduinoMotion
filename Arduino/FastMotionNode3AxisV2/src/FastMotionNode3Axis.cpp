#include "FastMotionNode3Axis.h"

#if !defined(__AVR_ATmega328P__)
#error "FastMotionNode3Axis 2.0.1 supports Arduino UNO/Nano with ATmega328P only."
#endif

#include <avr/interrupt.h>
#include <util/atomic.h>

namespace fastmotion3 {

namespace {

static const uint8_t AXIS_PINS[FastMotionNode3Axis::AXIS_COUNT][4] = {
    {2, 3, 4, 5},
    {6, 7, 8, 9},
    {10, 11, 12, 13}
};

static const uint8_t HALF_STEP_PATTERNS[8] = {
    0b0001, 0b0011, 0b0010, 0b0110,
    0b0100, 0b1100, 0b1000, 0b1001
};

static const int32_t ABSOLUTE_FIRMWARE_MAX_STEP_RATE = 4000;
static const uint16_t MIN_WATCHDOG_MS = 50;
static const uint16_t MAX_WATCHDOG_MS = 5000;

}  // namespace

FastMotionNode3Axis* FastMotionNode3Axis::instance_ = 0;

FastMotionNode3Axis::Config::Config()
    : baudRate(115200UL),
      maxAbsStepRate(4000),
      watchdogMs(250),
      statusPeriodUs(10000UL),
      releaseCoilsWhenDisabled(true) {}

FastMotionNode3Axis::FastMotionNode3Axis()
    : binaryMode_(false),
      parserIndex_(0),
      lastSequence_(0),
      currentWatchdogMs_(250),
      lastValidCommandMs_(0),
      lastStatusUs_(0),
      everReceivedValidCommand_(false),
      watchdogOk_(false),
      statusPending_(false),
      nodeError_(protocol::ERROR_NONE),
      validCommandCount_(0),
      crcErrorCount_(0),
      formatErrorCount_(0) {
    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        axes_[axis].requestedVelocity = 0;
        axes_[axis].appliedVelocity = 0;
        axes_[axis].position = 0;
        axes_[axis].phaseAccumulator = 0;
        axes_[axis].patternIndex = 0;
        axes_[axis].enabled = false;
        axes_[axis].quickStop = true;
        axes_[axis].running = false;
        axes_[axis].coilsEnergized = false;
        axes_[axis].velocityClamped = false;
        axes_[axis].error = protocol::ERROR_NONE;
        for (uint8_t coil = 0; coil < 4; ++coil) {
            axes_[axis].pins[coil].outputRegister = 0;
            axes_[axis].pins[coil].mask = 0;
        }
    }
}

void FastMotionNode3Axis::beginLocal(const Config& config) {
    config_ = config;
    if (config_.maxAbsStepRate < 1) {
        config_.maxAbsStepRate = 1;
    }
    if (config_.maxAbsStepRate > ABSOLUTE_FIRMWARE_MAX_STEP_RATE) {
        config_.maxAbsStepRate = ABSOLUTE_FIRMWARE_MAX_STEP_RATE;
    }
    if (config_.watchdogMs < MIN_WATCHDOG_MS) {
        config_.watchdogMs = MIN_WATCHDOG_MS;
    }
    if (config_.watchdogMs > MAX_WATCHDOG_MS) {
        config_.watchdogMs = MAX_WATCHDOG_MS;
    }
    if (config_.statusPeriodUs < 1000UL) {
        config_.statusPeriodUs = 1000UL;
    }

    binaryMode_ = false;
    parserIndex_ = 0;
    lastSequence_ = 0;
    currentWatchdogMs_ = config_.watchdogMs;
    lastValidCommandMs_ = millis();
    lastStatusUs_ = micros();
    everReceivedValidCommand_ = false;
    watchdogOk_ = false;
    statusPending_ = false;
    nodeError_ = protocol::ERROR_NONE;

    stopAll(true);
    initializePins();
    instance_ = this;
    initializeTimer1();
}

void FastMotionNode3Axis::beginBinary(const Config& config) {
    beginLocal(config);
    binaryMode_ = true;
    Serial.begin(config_.baudRate);
}

void FastMotionNode3Axis::poll() {
    if (!binaryMode_) {
        return;
    }

    parseAvailableSerialBytes();
    serviceWatchdog();
    sendStatusIfDue();
}

void FastMotionNode3Axis::setEnabled(uint8_t axis, bool enabled) {
    if (!validAxis(axis)) {
        return;
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        axes_[axis].enabled = enabled;
        if (!enabled) {
            axes_[axis].appliedVelocity = 0;
            axes_[axis].running = false;
            axes_[axis].phaseAccumulator = 0;
        }
    }
}

void FastMotionNode3Axis::setQuickStop(uint8_t axis, bool active) {
    if (!validAxis(axis)) {
        return;
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        axes_[axis].quickStop = active;
        if (active) {
            axes_[axis].appliedVelocity = 0;
            axes_[axis].running = false;
            axes_[axis].phaseAccumulator = 0;
        }
    }
}

void FastMotionNode3Axis::setVelocityStepsPerSecond(uint8_t axis,
                                                     int32_t velocity) {
    if (!validAxis(axis)) {
        return;
    }

    int32_t limited = velocity;
    bool clamped = false;
    if (limited > config_.maxAbsStepRate) {
        limited = config_.maxAbsStepRate;
        clamped = true;
    } else if (limited < -config_.maxAbsStepRate) {
        limited = -config_.maxAbsStepRate;
        clamped = true;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        axes_[axis].requestedVelocity = limited;
        axes_[axis].velocityClamped = clamped;
        axes_[axis].error = clamped
            ? protocol::ERROR_VELOCITY_CLAMP
            : protocol::ERROR_NONE;
    }
}

void FastMotionNode3Axis::resetPosition(uint8_t axis) {
    if (!validAxis(axis)) {
        return;
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        axes_[axis].position = 0;
        axes_[axis].phaseAccumulator = 0;
    }
}

void FastMotionNode3Axis::stopAll(bool disableOutputs) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
            axes_[axis].requestedVelocity = 0;
            axes_[axis].appliedVelocity = 0;
            axes_[axis].quickStop = true;
            axes_[axis].running = false;
            axes_[axis].phaseAccumulator = 0;
            if (disableOutputs) {
                axes_[axis].enabled = false;
            }
        }
    }
}

int32_t FastMotionNode3Axis::positionSteps(uint8_t axis) const {
    if (!validAxis(axis)) {
        return 0;
    }
    int32_t value = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = axes_[axis].position;
    }
    return value;
}

int32_t FastMotionNode3Axis::appliedVelocityStepsPerSecond(uint8_t axis) const {
    if (!validAxis(axis)) {
        return 0;
    }
    int32_t value = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = axes_[axis].appliedVelocity;
    }
    return value;
}

uint16_t FastMotionNode3Axis::axisStatus(uint8_t axis) const {
    if (!validAxis(axis)) {
        return 0;
    }

    bool enabled;
    bool quickStop;
    bool running;
    bool clamped;
    uint16_t error;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        enabled = axes_[axis].enabled;
        quickStop = axes_[axis].quickStop;
        running = axes_[axis].running;
        clamped = axes_[axis].velocityClamped;
        error = axes_[axis].error;
    }

    uint16_t status = protocol::AXIS_STATUS_POSITION_ESTIMATED;
    if (enabled) {
        status |= protocol::AXIS_STATUS_ENABLED;
    }
    if (running) {
        status |= protocol::AXIS_STATUS_RUNNING;
    }
    if (quickStop) {
        status |= protocol::AXIS_STATUS_QUICK_STOP;
    }
    if (clamped) {
        status |= protocol::AXIS_STATUS_VELOCITY_CLAMPED;
    }
    if ((error != protocol::ERROR_NONE) &&
        (error != protocol::ERROR_VELOCITY_CLAMP)) {
        status |= protocol::AXIS_STATUS_FAULT;
    }
    return status;
}

uint16_t FastMotionNode3Axis::axisError(uint8_t axis) const {
    if (!validAxis(axis)) {
        return protocol::ERROR_BAD_MASK;
    }
    uint16_t value;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = axes_[axis].error;
    }
    return value;
}

bool FastMotionNode3Axis::communicationActive() const {
    return binaryMode_ && everReceivedValidCommand_ && watchdogOk_;
}

bool FastMotionNode3Axis::watchdogOk() const {
    return watchdogOk_;
}

uint16_t FastMotionNode3Axis::acknowledgedSequence() const {
    return lastSequence_;
}

uint32_t FastMotionNode3Axis::validCommandCount() const {
    return validCommandCount_;
}

uint32_t FastMotionNode3Axis::crcErrorCount() const {
    return crcErrorCount_;
}

uint32_t FastMotionNode3Axis::formatErrorCount() const {
    return formatErrorCount_;
}

void FastMotionNode3Axis::initializePins() {
    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        for (uint8_t coil = 0; coil < 4; ++coil) {
            const uint8_t pin = AXIS_PINS[axis][coil];
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
            axes_[axis].pins[coil].outputRegister =
                portOutputRegister(digitalPinToPort(pin));
            axes_[axis].pins[coil].mask = digitalPinToBitMask(pin);
        }
    }
}

void FastMotionNode3Axis::initializeTimer1() {
    const uint32_t compareValue = (F_CPU / 8UL / TIMER_TICK_HZ) - 1UL;

    const uint8_t savedSreg = SREG;
    cli();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = static_cast<uint16_t>(compareValue);
    TCCR1B |= _BV(WGM12);  // CTC mode.
    TCCR1B |= _BV(CS11);   // Prescaler 8.
    TIMSK1 |= _BV(OCIE1A);
    SREG = savedSreg;
}

void FastMotionNode3Axis::parseAvailableSerialBytes() {
    while (Serial.available() > 0) {
        const int value = Serial.read();
        if (value >= 0) {
            parseByte(static_cast<uint8_t>(value));
        }
    }
}

void FastMotionNode3Axis::parseByte(uint8_t value) {
    if (parserIndex_ == 0) {
        if (value == protocol::COMMAND_SYNC_0) {
            commandFrame_[0] = value;
            parserIndex_ = 1;
        }
        return;
    }

    if (parserIndex_ == 1) {
        if (value == protocol::COMMAND_SYNC_1) {
            commandFrame_[1] = value;
            parserIndex_ = 2;
        } else if (value == protocol::COMMAND_SYNC_0) {
            commandFrame_[0] = value;
            parserIndex_ = 1;
        } else {
            parserIndex_ = 0;
        }
        return;
    }

    commandFrame_[parserIndex_++] = value;
    if (parserIndex_ >= protocol::COMMAND_SIZE) {
        parserIndex_ = 0;
        acceptCommandFrame();
    }
}

void FastMotionNode3Axis::acceptCommandFrame() {
    const uint16_t receivedCrc =
        protocol::readU16LE(&commandFrame_[protocol::COMMAND_SIZE - 2]);
    const uint16_t calculatedCrc =
        protocol::crc16Modbus(commandFrame_, protocol::COMMAND_SIZE - 2);

    if (receivedCrc != calculatedCrc) {
        ++crcErrorCount_;
        nodeError_ = protocol::ERROR_BAD_CRC;
        return;
    }
    if (commandFrame_[2] != protocol::VERSION) {
        ++formatErrorCount_;
        nodeError_ = protocol::ERROR_BAD_VERSION;
        return;
    }
    if (commandFrame_[3] != protocol::COMMAND_TYPE) {
        ++formatErrorCount_;
        nodeError_ = protocol::ERROR_BAD_TYPE;
        return;
    }

    const uint16_t sequence = protocol::readU16LE(&commandFrame_[4]);
    uint16_t requestedWatchdog = protocol::readU16LE(&commandFrame_[6]);
    const uint16_t enableMask = protocol::readU16LE(&commandFrame_[8]);
    const uint16_t quickStopMask = protocol::readU16LE(&commandFrame_[10]);
    const uint16_t resetMask = protocol::readU16LE(&commandFrame_[12]);

    if (requestedWatchdog == 0) {
        requestedWatchdog = config_.watchdogMs;
    }
    if (requestedWatchdog < MIN_WATCHDOG_MS) {
        requestedWatchdog = MIN_WATCHDOG_MS;
    }
    if (requestedWatchdog > MAX_WATCHDOG_MS) {
        requestedWatchdog = MAX_WATCHDOG_MS;
    }

    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        const uint16_t bit = static_cast<uint16_t>(1U << axis);
        const int32_t velocity =
            protocol::readI32LE(&commandFrame_[14 + 4 * axis]);
        applyAxisCommand(axis,
                         (enableMask & bit) != 0U,
                         (quickStopMask & bit) != 0U,
                         (resetMask & bit) != 0U,
                         velocity);
    }

    lastSequence_ = sequence;
    currentWatchdogMs_ = requestedWatchdog;
    lastValidCommandMs_ = millis();
    everReceivedValidCommand_ = true;
    watchdogOk_ = true;
    nodeError_ = protocol::ERROR_NONE;
    statusPending_ = true;
    ++validCommandCount_;
}

void FastMotionNode3Axis::serviceWatchdog() {
    if (!everReceivedValidCommand_ || !watchdogOk_) {
        return;
    }

    const uint32_t age = static_cast<uint32_t>(millis() - lastValidCommandMs_);
    if (age > currentWatchdogMs_) {
        watchdogOk_ = false;
        nodeError_ = protocol::ERROR_WATCHDOG;
        safeStopFromWatchdog();

        // Deliberately do not queue unsolicited watchdog-error frames. This
        // prevents stale ERROR_WATCHDOG responses being consumed after a
        // CODESYS Stop -> Run. The next valid command clears the condition and
        // requests a fresh status frame.
        statusPending_ = false;
    }
}

void FastMotionNode3Axis::sendStatusIfDue() {
    if (!everReceivedValidCommand_ || !watchdogOk_) {
        return;
    }

    const uint32_t nowUs = micros();
    const bool periodicDue =
        static_cast<uint32_t>(nowUs - lastStatusUs_) >= config_.statusPeriodUs;
    if (!statusPending_ && !periodicDue) {
        return;
    }
    if (Serial.availableForWrite() < protocol::STATUS_SIZE) {
        return;
    }

    uint8_t frame[protocol::STATUS_SIZE];
    buildStatusFrame(frame);
    Serial.write(frame, protocol::STATUS_SIZE);
    lastStatusUs_ = nowUs;
    statusPending_ = false;
}

void FastMotionNode3Axis::buildStatusFrame(uint8_t* frame) const {
    int32_t positions[AXIS_COUNT];
    int32_t velocities[AXIS_COUNT];
    uint16_t statuses[AXIS_COUNT];

    uint16_t readyMask = 0;
    uint16_t errorMask = 0;
    bool anyRunning = false;

    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        positions[axis] = positionSteps(axis);
        velocities[axis] = appliedVelocityStepsPerSecond(axis);
        statuses[axis] = axisStatus(axis);
        const uint16_t bit = static_cast<uint16_t>(1U << axis);
        if ((statuses[axis] & protocol::AXIS_STATUS_ENABLED) != 0U) {
            readyMask |= bit;
        }
        if ((statuses[axis] & protocol::AXIS_STATUS_RUNNING) != 0U) {
            anyRunning = true;
        }
        if ((statuses[axis] & protocol::AXIS_STATUS_FAULT) != 0U) {
            errorMask |= bit;
        }
    }

    uint16_t nodeStatus =
        protocol::NODE_STATUS_PROTOCOL_READY |
        protocol::NODE_STATUS_POSITION_ESTIMATED;
    if (communicationActive()) {
        nodeStatus |= protocol::NODE_STATUS_LINK_ACTIVE |
                      protocol::NODE_STATUS_WATCHDOG_OK;
    }
    if (anyRunning) {
        nodeStatus |= protocol::NODE_STATUS_ANY_AXIS_RUNNING;
    }

    frame[0] = protocol::STATUS_SYNC_0;
    frame[1] = protocol::STATUS_SYNC_1;
    frame[2] = protocol::VERSION;
    frame[3] = protocol::STATUS_TYPE;
    protocol::writeU16LE(&frame[4], lastSequence_);
    protocol::writeU16LE(&frame[6], nodeStatus);
    protocol::writeU16LE(&frame[8], nodeError_);
    protocol::writeU16LE(&frame[10], readyMask);
    protocol::writeU16LE(&frame[12], errorMask);

    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        protocol::writeI32LE(&frame[14 + 4 * axis], positions[axis]);
        protocol::writeI32LE(&frame[26 + 4 * axis], velocities[axis]);
        protocol::writeU16LE(&frame[38 + 2 * axis], statuses[axis]);
    }

    const uint32_t age32 = static_cast<uint32_t>(millis() - lastValidCommandMs_);
    const uint16_t age16 = age32 > 65535UL
        ? 65535U
        : static_cast<uint16_t>(age32);
    protocol::writeU16LE(&frame[44], age16);

    const uint16_t crc =
        protocol::crc16Modbus(frame, protocol::STATUS_SIZE - 2);
    protocol::writeU16LE(&frame[46], crc);
}

void FastMotionNode3Axis::applyAxisCommand(uint8_t axis, bool enabled,
                                            bool quickStop, bool reset,
                                            int32_t velocity) {
    if (reset) {
        resetPosition(axis);
    }
    setVelocityStepsPerSecond(axis, velocity);
    setEnabled(axis, enabled);
    setQuickStop(axis, quickStop);
}

void FastMotionNode3Axis::safeStopFromWatchdog() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
            axes_[axis].requestedVelocity = 0;
            axes_[axis].appliedVelocity = 0;
            axes_[axis].quickStop = true;
            axes_[axis].enabled = false;
            axes_[axis].running = false;
            axes_[axis].phaseAccumulator = 0;
        }
    }
}

void FastMotionNode3Axis::writeFastPin(const FastPin& pin, bool high) {
    if (high) {
        *pin.outputRegister |= pin.mask;
    } else {
        *pin.outputRegister &= static_cast<uint8_t>(~pin.mask);
    }
}

void FastMotionNode3Axis::writeCoilPatternFromISR(uint8_t axis,
                                                   uint8_t patternIndex) {
    const uint8_t pattern = HALF_STEP_PATTERNS[patternIndex & 0x07U];
    for (uint8_t coil = 0; coil < 4; ++coil) {
        writeFastPin(axes_[axis].pins[coil],
                     (pattern & static_cast<uint8_t>(1U << coil)) != 0U);
    }
    axes_[axis].coilsEnergized = true;
}

void FastMotionNode3Axis::releaseCoilsFromISR(uint8_t axis) {
    for (uint8_t coil = 0; coil < 4; ++coil) {
        writeFastPin(axes_[axis].pins[coil], false);
    }
    axes_[axis].coilsEnergized = false;
}

void FastMotionNode3Axis::serviceAxisFromISR(uint8_t axis) {
    AxisRuntime& state = axes_[axis];

    if (!state.enabled) {
        state.appliedVelocity = 0;
        state.running = false;
        state.phaseAccumulator = 0;
        if (config_.releaseCoilsWhenDisabled && state.coilsEnergized) {
            releaseCoilsFromISR(axis);
        }
        return;
    }

    if (!state.coilsEnergized) {
        writeCoilPatternFromISR(axis, state.patternIndex);
    }

    if (state.quickStop || state.requestedVelocity == 0) {
        state.appliedVelocity = 0;
        state.running = false;
        state.phaseAccumulator = 0;
        return;
    }

    const int32_t velocity = state.requestedVelocity;
    const uint32_t magnitude = velocity < 0
        ? static_cast<uint32_t>(-velocity)
        : static_cast<uint32_t>(velocity);

    state.appliedVelocity = velocity;
    state.running = true;
    state.phaseAccumulator += magnitude;

    if (state.phaseAccumulator < TIMER_TICK_HZ) {
        return;
    }
    state.phaseAccumulator -= TIMER_TICK_HZ;

    if (velocity > 0) {
        state.patternIndex = static_cast<uint8_t>((state.patternIndex + 1U) & 0x07U);
        ++state.position;
    } else {
        state.patternIndex = static_cast<uint8_t>((state.patternIndex + 7U) & 0x07U);
        --state.position;
    }
    writeCoilPatternFromISR(axis, state.patternIndex);
}

void FastMotionNode3Axis::onTimerCompareISR() {
    if (instance_ == 0) {
        return;
    }
    for (uint8_t axis = 0; axis < AXIS_COUNT; ++axis) {
        instance_->serviceAxisFromISR(axis);
    }
}

bool FastMotionNode3Axis::validAxis(uint8_t axis) {
    return axis < AXIS_COUNT;
}

}  // namespace fastmotion3

ISR(TIMER1_COMPA_vect) {
    fastmotion3::FastMotionNode3Axis::onTimerCompareISR();
}
