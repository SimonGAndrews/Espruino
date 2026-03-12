#include "jsinteractive.h"
#include "jshardware.h"

int main(void) {

  // basic hardware init
  jshInit();

  // start Espruino
  jsiInit(false);

  // main interpreter loop
  while (1) {
    jsiLoop();
  }

  return 0;
}