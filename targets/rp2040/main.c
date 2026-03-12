#include "platform_config.h"
#include "jsinteractive.h"
#include "jshardware.h"
#include "jsvar.h"
#include "jswrapper.h"

#include "hardware/uart.h"
#include "pico/stdlib.h"

int main(void) {

  // basic hardware init
  jshInit();
  uart_puts(uart0, "RP2040 main after jshInit\r\n");
  jswHWInit();
  uart_puts(uart0, "RP2040 main after jswHWInit\r\n");
  jsvInit(JSVAR_CACHE_SIZE);
  uart_puts(uart0, "RP2040 main after jsvInit\r\n");

  // start Espruino
  jsiInit(false);
  uart_puts(uart0, "RP2040 main after jsiInit\r\n");

#ifdef PICO_DEFAULT_LED_PIN
  // A steady LED means startup got past jsiInit() and into the USB-init path.
  gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif

  // Start RP2040 USB after interpreter init so regular idle/task servicing is
  // immediately available during enumeration.
  rp2040UsbInitNow();
  uart_puts(uart0, "RP2040 main after rp2040UsbInitNow\r\n");

  // main interpreter loop
  while (1) {
    jsiLoop();
  }

  return 0;
}
