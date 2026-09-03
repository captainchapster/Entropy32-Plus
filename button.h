#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

struct Button {
  uint8_t pin;
  bool lastState;
  unsigned long lastChange;
};

#endif