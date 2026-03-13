#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "platform_config.h"
#include "jshardware.h"
#include "jsdevices.h"
#include "jsinteractive.h"

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "bsp/board_api.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#ifdef USB
#include "tusb.h"
#endif

#define RP2040_FLASH_PAGE_SIZE 4096u
#define RP2040_XIP_BASE 0x10000000u

#if !defined(RELEASE) && defined(RP2040_DEBUG_EARLY_BOOT)
#define RP2040_EARLY_LOG_ENABLED 1
#else
#define RP2040_EARLY_LOG_ENABLED 0
#endif

static bool rpFirstIdle = true;
static bool rpUsbInitialised = false;
static int64_t rpSystemTimeOffsetUs = 0;
static bool rpEarlyLogInitialised = false;

static JshPinState rpPinState[JSH_PIN_COUNT];
static Pin rpWatchPins[ESPR_EXTI_COUNT];

static uint32_t rpIrqStateStack[8];
static uint8_t rpIrqStateStackLen = 0;

void NVIC_SystemReset(void) {
  watchdog_reboot(0, 0, 0);
  while (1) {
    tight_loop_contents();
  }
}

static bool rpPinIsValid(Pin pin) {
  return pin != PIN_UNDEFINED && pin < JSH_PIN_COUNT;
}

static uint rpPinToGpio(Pin pin) {
  return (uint)pinInfo[pin].pin;
}

static void rpPinApplyState(Pin pin, JshPinState state) {
  if (!rpPinIsValid(pin)) return;
  uint gpio = rpPinToGpio(pin);

  gpio_init(gpio);
  gpio_disable_pulls(gpio);

  switch (state & JSHPINSTATE_MASK) {
    case JSHPINSTATE_GPIO_OUT:
    case JSHPINSTATE_GPIO_OUT_OPENDRAIN:
    case JSHPINSTATE_GPIO_OUT_OPENDRAIN_PULLUP:
    case JSHPINSTATE_AF_OUT:
    case JSHPINSTATE_AF_OUT_OPENDRAIN:
    case JSHPINSTATE_USART_OUT:
    case JSHPINSTATE_DAC_OUT:
      gpio_set_dir(gpio, GPIO_OUT);
      gpio_put(gpio, (state & JSHPINSTATE_PIN_IS_ON) ? 1 : 0);
      break;
    case JSHPINSTATE_GPIO_IN_PULLUP:
      gpio_set_dir(gpio, GPIO_IN);
      gpio_pull_up(gpio);
      break;
    case JSHPINSTATE_GPIO_IN_PULLDOWN:
      gpio_set_dir(gpio, GPIO_IN);
      gpio_pull_down(gpio);
      break;
    default:
      gpio_set_dir(gpio, GPIO_IN);
      break;
  }
}

static bool rpFlashAddrValid(uint32_t addr, uint32_t len) {
  if (addr < FLASH_START) return false;
  if (len > FLASH_TOTAL) return false;
  return (addr - FLASH_START) <= (FLASH_TOTAL - len);
}

static uint32_t rpFlashOffset(uint32_t addr) {
  return addr - FLASH_START;
}

static void rpEarlyLogInit(void) {
#if RP2040_EARLY_LOG_ENABLED
  if (rpEarlyLogInitialised) return;
  uart_init(uart0, 115200);
  gpio_set_function(0, GPIO_FUNC_UART);
  gpio_set_function(1, GPIO_FUNC_UART);
  uart_set_hw_flow(uart0, false, false);
  uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
  rpEarlyLogInitialised = true;
#endif
}

void rp2040EarlyLog(const char *msg) {
#if RP2040_EARLY_LOG_ENABLED
  if (!rpEarlyLogInitialised || !msg) return;
  uart_puts(uart0, msg);
#else
  NOT_USED(msg);
#endif
}

void rp2040EarlyLogf(const char *fmt, ...) {
#if RP2040_EARLY_LOG_ENABLED
  if (!rpEarlyLogInitialised || !fmt) return;
  char msg[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  rp2040EarlyLog(msg);
#else
  NOT_USED(fmt);
#endif
}

void rp2040UsbInitNow(void) {
#ifdef USB
  if (!rpUsbInitialised) {
    rp2040EarlyLog("RP2040 usb: init start\r\n");
    tusb_init();
    tud_connect();
    rpUsbInitialised = true;
  }
#endif
}

void jshInit() {
#ifdef USB
  board_init();
  rpUsbInitialised = false;
#endif
  rpEarlyLogInit();
  rp2040EarlyLog("RP2040 boot: jshInit ok\r\n");
  memset(rpPinState, JSHPINSTATE_GPIO_IN, sizeof(rpPinState));
  for (int i = 0; i < ESPR_EXTI_COUNT; i++) rpWatchPins[i] = PIN_UNDEFINED;
  rpSystemTimeOffsetUs = 0;
  rpFirstIdle = true;
  jshInitDevices();
}

void jshReset() {
  jshResetDevices();
  memset(rpPinState, JSHPINSTATE_GPIO_IN, sizeof(rpPinState));
  for (int i = 0; i < ESPR_EXTI_COUNT; i++) rpWatchPins[i] = PIN_UNDEFINED;
}

void jshIdle() {
#ifdef USB
  tud_task();
  if (tud_cdc_available()) {
    char rxbuf[64];
    while (tud_cdc_available()) {
      uint32_t len = tud_cdc_read(rxbuf, sizeof(rxbuf));
      if (!len) break;
      jshPushIOCharEvents(EV_USBSERIAL, rxbuf, len);
    }
  }
#endif
  if (rpFirstIdle) {
    jsiOneSecondAfterStartup();
    rpFirstIdle = false;
  }
}

void jshBusyIdle() {
  jshIdle();
  tight_loop_contents();
}

bool jshSleep(JsSysTime timeUntilWake) {
  jshIdle();
  if (timeUntilWake == JSSYSTIME_MAX) return true;
  JsVarFloat ms = jshGetMillisecondsFromTime(timeUntilWake);
  if (ms > 0) sleep_ms((uint32_t)ms);
  return true;
}

void jshKill() {
}

int jshGetSerialNumber(unsigned char *data, int maxChars) {
  if (!data || maxChars <= 0) return 0;
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  int len = (maxChars < (int)PICO_UNIQUE_BOARD_ID_SIZE_BYTES) ? maxChars : (int)PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
  memcpy(data, id.id, (size_t)len);
  return len;
}

bool jshIsUSBSERIALConnected() {
#ifdef USB
  tud_task();
  return tud_mounted();
#else
  return false;
#endif
}

JsSysTime jshGetSystemTime() {
  return (JsSysTime)((int64_t)time_us_64() + rpSystemTimeOffsetUs);
}

void jshSetSystemTime(JsSysTime time) {
  rpSystemTimeOffsetUs = (int64_t)time - (int64_t)time_us_64();
}

JsSysTime jshGetTimeFromMilliseconds(JsVarFloat ms) {
  return (JsSysTime)(ms * 1000.0);
}

JsVarFloat jshGetMillisecondsFromTime(JsSysTime time) {
  return ((JsVarFloat)time) / 1000.0;
}

void jshInterruptOff() {
  uint32_t irqState = save_and_disable_interrupts();
  if (rpIrqStateStackLen < (sizeof(rpIrqStateStack) / sizeof(rpIrqStateStack[0])))
    rpIrqStateStack[rpIrqStateStackLen++] = irqState;
}

void jshInterruptOn() {
  if (rpIrqStateStackLen)
    restore_interrupts(rpIrqStateStack[--rpIrqStateStackLen]);
  else
    restore_interrupts(0);
}

bool jshIsInInterrupt() {
  return false;
}

void jshDelayMicroseconds(int microsec) {
  if (microsec > 0) busy_wait_us_32((uint32_t)microsec);
}

void jshPinSetValue(Pin pin, bool value) {
  if (!rpPinIsValid(pin)) return;
  if (pinInfo[pin].port & JSH_PIN_NEGATED) value = !value;
  gpio_put(rpPinToGpio(pin), value);
}

bool jshPinGetValue(Pin pin) {
  if (!rpPinIsValid(pin)) return false;
  bool value = gpio_get(rpPinToGpio(pin));
  if (pinInfo[pin].port & JSH_PIN_NEGATED) value = !value;
  return value;
}

void jshPinSetState(Pin pin, JshPinState state) {
  if (!rpPinIsValid(pin)) return;
  rpPinState[pin] = state;
  rpPinApplyState(pin, state);
}

JshPinState jshPinGetState(Pin pin) {
  if (!rpPinIsValid(pin)) return JSHPINSTATE_UNDEFINED;
  return rpPinState[pin];
}

JsVarFloat jshPinAnalog(Pin pin) {
  NOT_USED(pin);
  return 0;
}

int jshPinAnalogFast(Pin pin) {
  NOT_USED(pin);
  return 0;
}

JshPinFunction jshPinAnalogOutput(Pin pin, JsVarFloat value, JsVarFloat freq, JshAnalogOutputFlags flags) {
  NOT_USED(pin);
  NOT_USED(value);
  NOT_USED(freq);
  NOT_USED(flags);
  return JSH_NOTHING;
}

bool jshCanWatch(Pin pin) {
  return rpPinIsValid(pin);
}

IOEventFlags jshPinWatch(Pin pin, bool shouldWatch, JshPinWatchFlags flags) {
  NOT_USED(flags);
  if (!rpPinIsValid(pin)) return EV_NONE;
  if (shouldWatch) {
    for (int i = 0; i < ESPR_EXTI_COUNT; i++) {
      if (rpWatchPins[i] == PIN_UNDEFINED) {
        rpWatchPins[i] = pin;
        return (IOEventFlags)(EV_EXTI0 + i);
      }
      if (rpWatchPins[i] == pin) return (IOEventFlags)(EV_EXTI0 + i);
    }
  } else {
    for (int i = 0; i < ESPR_EXTI_COUNT; i++) {
      if (rpWatchPins[i] == pin) {
        rpWatchPins[i] = PIN_UNDEFINED;
        return EV_NONE;
      }
    }
  }
  return EV_NONE;
}

bool jshGetWatchedPinState(IOEventFlags device) {
  int i = (int)IOEVENTFLAGS_GETTYPE(device) - (int)EV_EXTI0;
  if (i < 0 || i >= ESPR_EXTI_COUNT) return false;
  Pin pin = rpWatchPins[i];
  if (!rpPinIsValid(pin)) return false;
  return jshPinGetValue(pin);
}

bool jshIsEventForPin(IOEventFlags eventFlags, Pin pin) {
  IOEventFlags evt = IOEVENTFLAGS_GETTYPE(eventFlags);
  if (evt < EV_EXTI0 || evt > EV_EXTI_MAX) return false;
  int i = (int)evt - (int)EV_EXTI0;
  if (i < 0 || i >= ESPR_EXTI_COUNT) return false;
  return rpWatchPins[i] == pin;
}

bool jshIsDeviceInitialised(IOEventFlags device) {
#ifdef USB
  if (device == EV_USBSERIAL) return rpUsbInitialised;
#endif
  return false;
}

void jshUSARTSetup(IOEventFlags device, JshUSARTInfo *inf) {
  NOT_USED(device);
  NOT_USED(inf);
}

void jshUSARTKick(IOEventFlags device) {
#ifdef USB
  if (device == EV_USBSERIAL) {
    tud_task();
    if (!tud_ready()) return;
    bool transmitted = false;
    while (tud_cdc_write_available()) {
      int c = jshGetCharToTransmit(EV_USBSERIAL);
      if (c < 0) break;
      tud_cdc_write_char((char)c);
      transmitted = true;
    }
    tud_cdc_write_flush();
    NOT_USED(transmitted);
  }
#else
  NOT_USED(device);
#endif
}

void jshSPISetup(IOEventFlags device, JshSPIInfo *inf) {
  NOT_USED(device);
  NOT_USED(inf);
}

int jshSPISend(IOEventFlags device, int data) {
  NOT_USED(device);
  NOT_USED(data);
  return -1;
}

void jshSPISend16(IOEventFlags device, int data) {
  NOT_USED(device);
  NOT_USED(data);
}

void jshSPISet16(IOEventFlags device, bool is16) {
  NOT_USED(device);
  NOT_USED(is16);
}

void jshSPISetReceive(IOEventFlags device, bool isReceive) {
  NOT_USED(device);
  NOT_USED(isReceive);
}

void jshSPIWait(IOEventFlags device) {
  NOT_USED(device);
}

void jshI2CSetup(IOEventFlags device, JshI2CInfo *inf) {
  NOT_USED(device);
  NOT_USED(inf);
}

void jshI2CWrite(IOEventFlags device, unsigned char address, int nBytes, const unsigned char *data, bool sendStop) {
  NOT_USED(device);
  NOT_USED(address);
  NOT_USED(nBytes);
  NOT_USED(data);
  NOT_USED(sendStop);
}

void jshI2CRead(IOEventFlags device, unsigned char address, int nBytes, unsigned char *data, bool sendStop) {
  NOT_USED(device);
  NOT_USED(address);
  NOT_USED(sendStop);
  if (data && nBytes > 0) memset(data, 0, (size_t)nBytes);
}

bool jshFlashGetPage(uint32_t addr, uint32_t *startAddr, uint32_t *pageSize) {
  if (!rpFlashAddrValid(addr, 1)) return false;
  if (startAddr) *startAddr = addr - (addr % RP2040_FLASH_PAGE_SIZE);
  if (pageSize) *pageSize = RP2040_FLASH_PAGE_SIZE;
  return true;
}

JsVar *jshFlashGetFree() {
  return jsvNewEmptyArray();
}

void jshFlashErasePage(uint32_t addr) {
  NOT_USED(addr);
}

void jshFlashRead(void *buf, uint32_t addr, uint32_t len) {
  if (!buf || !len) return;
  if (!rpFlashAddrValid(addr, len)) {
    memset(buf, 0xFF, len);
    return;
  }
  memcpy(buf, (const void *)(RP2040_XIP_BASE + rpFlashOffset(addr)), len);
}

void jshFlashWrite(void *buf, uint32_t addr, uint32_t len) {
  NOT_USED(buf);
  NOT_USED(addr);
  NOT_USED(len);
}

size_t jshFlashGetMemMapAddress(size_t ptr) {
  uint32_t addr = (uint32_t)ptr;
  if (rpFlashAddrValid(addr, 1))
    return RP2040_XIP_BASE + rpFlashOffset(addr);
  return ptr;
}

void jshUtilTimerStart(JsSysTime period) {
  NOT_USED(period);
}

void jshUtilTimerReschedule(JsSysTime period) {
  NOT_USED(period);
}

void jshUtilTimerDisable() {
}

JshPinFunction jshGetCurrentPinFunction(Pin pin) {
  NOT_USED(pin);
  return JSH_NOTHING;
}

void jshSetOutputValue(JshPinFunction func, int value) {
  NOT_USED(func);
  NOT_USED(value);
}

void jshEnableWatchDog(JsVarFloat timeout) {
  NOT_USED(timeout);
}

void jshKickWatchDog() {
}

JsVarFloat jshReadTemperature() {
  return NAN;
}

JsVarFloat jshReadVRef() {
  return 3.3;
}

JsVarFloat jshReadVDDH() {
  return NAN;
}

unsigned int jshGetRandomNumber() {
  return (unsigned int)rand();
}

unsigned int jshSetSystemClock(JsVar *options) {
  NOT_USED(options);
  return CLOCK_SPEED_MHZ * 1000000;
}

void jshReboot() {
  watchdog_reboot(0, 0, 0);
  while (1) {
  }
}
