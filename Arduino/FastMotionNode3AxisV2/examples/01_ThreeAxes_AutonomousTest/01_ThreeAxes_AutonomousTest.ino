#include <FastMotionNode3Axis.h>

using fastmotion3::FastMotionNode3Axis;

FastMotionNode3Axis node;

static uint8_t testPhase = 0;
static uint32_t phaseStartMs = 0;

void applyPhase(uint8_t phase) {
  node.stopAll(true);

  switch (phase) {
    case 0:  // Axis 1 only.
      node.setEnabled(0, true);
      node.setQuickStop(0, false);
      node.setVelocityStepsPerSecond(0, 200);
      break;

    case 1:  // Axis 2 only.
      node.setEnabled(1, true);
      node.setQuickStop(1, false);
      node.setVelocityStepsPerSecond(1, 200);
      break;

    case 2:  // Axis 3 only.
      node.setEnabled(2, true);
      node.setQuickStop(2, false);
      node.setVelocityStepsPerSecond(2, 200);
      break;

    case 3:  // All three at different signed velocities.
      node.setEnabled(0, true);
      node.setEnabled(1, true);
      node.setEnabled(2, true);
      node.setQuickStop(0, false);
      node.setQuickStop(1, false);
      node.setQuickStop(2, false);
      node.setVelocityStepsPerSecond(0, 200);
      node.setVelocityStepsPerSecond(1, -120);
      node.setVelocityStepsPerSecond(2, 60);
      break;

    case 4:  // Zero speed, coils energised for holding torque.
      node.setEnabled(0, true);
      node.setEnabled(1, true);
      node.setEnabled(2, true);
      node.setQuickStop(0, false);
      node.setQuickStop(1, false);
      node.setQuickStop(2, false);
      break;

    default:  // Disabled, coils released.
      node.stopAll(true);
      break;
  }
}

void setup() {
  FastMotionNode3Axis::Config config;
  config.maxAbsStepRate = 4000;
  config.releaseCoilsWhenDisabled = true;
  node.beginLocal(config);

  testPhase = 0;
  phaseStartMs = millis();
  applyPhase(testPhase);
}

void loop() {
  // No delay(): Timer1 generates all steps independently.
  const uint32_t phaseDurationMs = 3000UL;
  if (static_cast<uint32_t>(millis() - phaseStartMs) >= phaseDurationMs) {
    testPhase = static_cast<uint8_t>((testPhase + 1U) % 6U);
    phaseStartMs = millis();
    applyPhase(testPhase);
  }
}
