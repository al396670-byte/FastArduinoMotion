#include <FastMotionNode3Axis.h>

using fastmotion3::FastMotionNode3Axis;

FastMotionNode3Axis node;

void setup() {
  FastMotionNode3Axis::Config config;
  config.baudRate = 115200UL;
  config.maxAbsStepRate = 4000;
  config.watchdogMs = 250;
  config.statusPeriodUs = 10000UL;
  config.releaseCoilsWhenDisabled = true;

  node.beginBinary(config);
}

void loop() {
  // Parser, watchdog and status transmission are non-blocking.
  // Timer1 continues generating the three step streams independently.
  node.poll();
}
