#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "platform_config.h"
#include "jshardware.h"
#include "jsdevices.h"
#include "jsinteractive.h"
#include "jstimer.h"

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "bsp/board_api.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
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
static bool rpWatchIrqHandlerInstalled = false;
static bool rpAdcInitialised = false;
static bool rpI2cInitialised[2] = { false, false };
static bool rpSpiInitialised[2] = { false, false };
static bool rpSpiIs16[2] = { false, false };
static bool rpSpiReceive[2] = { true, true };
static unsigned char rpSpiMode[2] = { SPIF_SPI_MODE_0, SPIF_SPI_MODE_0 };
static bool rpSpiMsb[2] = { true, true };

static JshPinState rpPinState[JSH_PIN_COUNT];
static Pin rpWatchPins[ESPR_EXTI_COUNT];
static volatile bool rpWatchLastState[ESPR_EXTI_COUNT];
static Pin rpI2cPinScl[2] = { PIN_UNDEFINED, PIN_UNDEFINED };
static Pin rpI2cPinSda[2] = { PIN_UNDEFINED, PIN_UNDEFINED };
static Pin rpSpiPinSck[2] = { PIN_UNDEFINED, PIN_UNDEFINED };
static Pin rpSpiPinMiso[2] = { PIN_UNDEFINED, PIN_UNDEFINED };
static Pin rpSpiPinMosi[2] = { PIN_UNDEFINED, PIN_UNDEFINED };
BITFIELD_DECL(jshPinSoftPWM, JSH_PIN_COUNT);

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

static bool rpPinHasAdc(Pin pin) {
  return rpPinIsValid(pin) && pinInfo[pin].analog != JSH_ANALOG_NONE;
}

static uint rpPinToAdcChannel(Pin pin) {
  return (uint)(pinInfo[pin].analog & JSH_MASK_ANALOG_CH);
}

static void rpAdcEnsureInitialised(void) {
  if (rpAdcInitialised) return;
  adc_init();
  rpAdcInitialised = true;
}

static uint32_t rpPwmGetSliceHz(uint32_t offset, uint32_t div16) {
  uint32_t source_hz = clock_get_hz(clk_sys);
  if (source_hz + offset / 16 > 268000000) {
    return (16 * (uint64_t)source_hz + offset) / div16;
  } else {
    return (16 * source_hz + offset) / div16;
  }
}

static uint32_t rpPwmGetSliceHzRound(uint32_t div16) {
  return rpPwmGetSliceHz(div16 / 2, div16);
}

static uint32_t rpPwmGetSliceHzCeil(uint32_t div16) {
  return rpPwmGetSliceHz(div16 - 1, div16);
}

static bool rpPwmConfigure(uint slice, JsVarFloat freq) {
  const uint32_t top_max = 65534;
  uint32_t source_hz = clock_get_hz(clk_sys);
  if (!isfinite(freq) || freq <= 0) freq = 1000;
  uint32_t target_hz = (uint32_t)freq;
  if (target_hz < 1) target_hz = 1;

  uint32_t div16;
  uint32_t top;
  if ((source_hz + target_hz / 2) / target_hz < top_max) {
    div16 = 16;
    top = (source_hz + target_hz / 2) / target_hz - 1;
  } else {
    div16 = rpPwmGetSliceHzCeil(top_max * target_hz);
    top = rpPwmGetSliceHzRound(div16 * target_hz) - 1;
  }

  if (div16 < 16 || div16 >= 256 * 16) return false;
  pwm_set_clkdiv_int_frac(slice, div16 / 16, div16 & 0xF);
  pwm_set_wrap(slice, top);
  return true;
}

static int rpI2cIndexFromDevice(IOEventFlags device) {
  if (device == EV_I2C1) return 0;
  if (device == EV_I2C2) return 1;
  return -1;
}

static int rpSpiIndexFromDevice(IOEventFlags device) {
  if (device == EV_SPI1) return 0;
  if (device == EV_SPI2) return 1;
  return -1;
}

static i2c_inst_t *rpI2cFromIndex(int idx) {
  if (idx == 0) return i2c0;
  if (idx == 1) return i2c1;
  return NULL;
}

static spi_inst_t *rpSpiFromIndex(int idx) {
  if (idx == 0) return spi0;
  if (idx == 1) return spi1;
  return NULL;
}

static bool rpI2cPinMatchesDevice(IOEventFlags device, Pin pin, JshPinFunction info) {
  JshPinFunction funcType = jshGetPinFunctionFromDevice(device);
  JshPinFunction func = jshGetPinFunctionForPin(pin, funcType);
  return func && ((func & JSH_MASK_INFO) == info);
}

static bool rpSpiPinMatchesDevice(IOEventFlags device, Pin pin, JshPinFunction info) {
  JshPinFunction funcType = jshGetPinFunctionFromDevice(device);
  JshPinFunction func = jshGetPinFunctionForPin(pin, funcType);
  return func && ((func & JSH_MASK_INFO) == info);
}

static void rpI2cMarkPins(int idx, Pin pinScl, Pin pinSda) {
  if (rpPinIsValid(pinScl)) rpPinState[pinScl] = JSHPINSTATE_AF_OUT_OPENDRAIN;
  if (rpPinIsValid(pinSda)) rpPinState[pinSda] = JSHPINSTATE_AF_OUT_OPENDRAIN;
  rpI2cPinScl[idx] = pinScl;
  rpI2cPinSda[idx] = pinSda;
}

static void rpSpiMarkPins(int idx, Pin pinSck, Pin pinMiso, Pin pinMosi) {
  if (rpPinIsValid(pinSck)) rpPinState[pinSck] = JSHPINSTATE_AF_OUT;
  if (rpPinIsValid(pinMiso)) rpPinState[pinMiso] = JSHPINSTATE_AF_OUT;
  if (rpPinIsValid(pinMosi)) rpPinState[pinMosi] = JSHPINSTATE_AF_OUT;
  rpSpiPinSck[idx] = pinSck;
  rpSpiPinMiso[idx] = pinMiso;
  rpSpiPinMosi[idx] = pinMosi;
}

static void rpI2cReleasePins(int idx) {
  Pin pinScl = rpI2cPinScl[idx];
  Pin pinSda = rpI2cPinSda[idx];
  rpI2cPinScl[idx] = PIN_UNDEFINED;
  rpI2cPinSda[idx] = PIN_UNDEFINED;
  if (rpPinIsValid(pinScl)) {
    gpio_set_function(rpPinToGpio(pinScl), GPIO_FUNC_SIO);
    jshPinSetState(pinScl, JSHPINSTATE_GPIO_IN_PULLUP);
  }
  if (rpPinIsValid(pinSda)) {
    gpio_set_function(rpPinToGpio(pinSda), GPIO_FUNC_SIO);
    jshPinSetState(pinSda, JSHPINSTATE_GPIO_IN_PULLUP);
  }
}

static void rpSpiReleasePins(int idx) {
  Pin pinSck = rpSpiPinSck[idx];
  Pin pinMiso = rpSpiPinMiso[idx];
  Pin pinMosi = rpSpiPinMosi[idx];
  rpSpiPinSck[idx] = PIN_UNDEFINED;
  rpSpiPinMiso[idx] = PIN_UNDEFINED;
  rpSpiPinMosi[idx] = PIN_UNDEFINED;
  if (rpPinIsValid(pinSck)) {
    gpio_set_function(rpPinToGpio(pinSck), GPIO_FUNC_SIO);
    jshPinSetState(pinSck, JSHPINSTATE_GPIO_IN);
  }
  if (rpPinIsValid(pinMiso)) {
    gpio_set_function(rpPinToGpio(pinMiso), GPIO_FUNC_SIO);
    jshPinSetState(pinMiso, JSHPINSTATE_GPIO_IN);
  }
  if (rpPinIsValid(pinMosi)) {
    gpio_set_function(rpPinToGpio(pinMosi), GPIO_FUNC_SIO);
    jshPinSetState(pinMosi, JSHPINSTATE_GPIO_IN);
  }
}

static void rpI2cUnsetupIdx(int idx) {
  i2c_inst_t *inst = rpI2cFromIndex(idx);
  if (!inst) return;
  if (rpI2cInitialised[idx]) {
    i2c_deinit(inst);
    rpI2cInitialised[idx] = false;
  }
  rpI2cReleasePins(idx);
}

static void rpSpiApplyFormat(int idx) {
  spi_inst_t *inst = rpSpiFromIndex(idx);
  if (!inst) return;
  spi_set_format(inst,
                 rpSpiIs16[idx] ? 16 : 8,
                 (rpSpiMode[idx] & SPIF_CPOL) ? SPI_CPOL_1 : SPI_CPOL_0,
                 (rpSpiMode[idx] & SPIF_CPHA) ? SPI_CPHA_1 : SPI_CPHA_0,
                 rpSpiMsb[idx] ? SPI_MSB_FIRST : SPI_LSB_FIRST);
}

static void rpSpiUnsetupIdx(int idx) {
  spi_inst_t *inst = rpSpiFromIndex(idx);
  if (!inst) return;
  if (rpSpiInitialised[idx]) {
    spi_deinit(inst);
    rpSpiInitialised[idx] = false;
  }
  rpSpiReleasePins(idx);
  rpSpiIs16[idx] = false;
  rpSpiReceive[idx] = true;
  rpSpiMode[idx] = SPIF_SPI_MODE_0;
  rpSpiMsb[idx] = true;
}

static void rpI2cResetAll(void) {
  for (int i = 0; i < 2; i++)
    rpI2cUnsetupIdx(i);
}

static void rpSpiResetAll(void) {
  for (int i = 0; i < 2; i++)
    rpSpiUnsetupIdx(i);
}

static bool rpI2cEnsureInitialised(IOEventFlags device, JshI2CInfo *inf) {
  int idx = rpI2cIndexFromDevice(device);
  if (idx < 0) {
    jsExceptionHere(JSET_ERROR, "Only I2C1 and I2C2 supported");
    return false;
  }
  JshPinFunction funcType = jshGetPinFunctionFromDevice(device);
  if (!jshIsPinValid(inf->pinSCL)) inf->pinSCL = jshFindPinForFunction(funcType, JSH_I2C_SCL);
  if (!jshIsPinValid(inf->pinSDA)) inf->pinSDA = jshFindPinForFunction(funcType, JSH_I2C_SDA);

  if (!jshIsPinValid(inf->pinSCL) || !jshIsPinValid(inf->pinSDA)) {
    jsExceptionHere(JSET_ERROR, "I2C pins not valid");
    return false;
  }
  if (!rpI2cPinMatchesDevice(device, inf->pinSCL, JSH_I2C_SCL) ||
      !rpI2cPinMatchesDevice(device, inf->pinSDA, JSH_I2C_SDA)) {
    jsExceptionHere(JSET_ERROR, "Selected pins do not match %s", jshGetDeviceString(device));
    return false;
  }

  rpI2cUnsetupIdx(idx);

  if (inf->bitrate <= 0) inf->bitrate = 100000;
  i2c_inst_t *inst = rpI2cFromIndex(idx);
  if (!inst) {
    jsExceptionHere(JSET_ERROR, "I2C backend unavailable");
    return false;
  }
  i2c_init(inst, (uint)inf->bitrate);
  gpio_set_function(rpPinToGpio(inf->pinSCL), GPIO_FUNC_I2C);
  gpio_set_function(rpPinToGpio(inf->pinSDA), GPIO_FUNC_I2C);
  gpio_set_pulls(rpPinToGpio(inf->pinSCL), true, false);
  gpio_set_pulls(rpPinToGpio(inf->pinSDA), true, false);
  rpI2cMarkPins(idx, inf->pinSCL, inf->pinSDA);
  rpI2cInitialised[idx] = true;
  return true;
}

static bool rpI2cGetReady(IOEventFlags device, i2c_inst_t **inst) {
  int idx = rpI2cIndexFromDevice(device);
  if (idx < 0) {
    jsExceptionHere(JSET_ERROR, "Only I2C1 and I2C2 supported");
    return false;
  }
  if (!rpI2cInitialised[idx]) {
    jsExceptionHere(JSET_ERROR, "%s not initialised", jshGetDeviceString(device));
    return false;
  }
  *inst = rpI2cFromIndex(idx);
  return *inst != NULL;
}

static bool rpSpiEnsureInitialised(IOEventFlags device, JshSPIInfo *inf) {
  int idx = rpSpiIndexFromDevice(device);
  if (idx < 0) {
    jsExceptionHere(JSET_ERROR, "Only SPI1 and SPI2 supported");
    return false;
  }
  JshPinFunction funcType = jshGetPinFunctionFromDevice(device);
  bool allPinsUnset = !jshIsPinValid(inf->pinSCK) &&
                      !jshIsPinValid(inf->pinMISO) &&
                      !jshIsPinValid(inf->pinMOSI);
  if (allPinsUnset) {
    inf->pinSCK = jshFindPinForFunction(funcType, JSH_SPI_SCK);
    inf->pinMISO = jshFindPinForFunction(funcType, JSH_SPI_MISO);
    inf->pinMOSI = jshFindPinForFunction(funcType, JSH_SPI_MOSI);
  }

  if (!jshIsPinValid(inf->pinSCK)) {
    jsExceptionHere(JSET_ERROR, "SPI SCK pin not valid");
    return false;
  }
  if (!jshIsPinValid(inf->pinMISO) && !jshIsPinValid(inf->pinMOSI)) {
    jsExceptionHere(JSET_ERROR, "SPI requires MISO or MOSI");
    return false;
  }
  if (!rpSpiPinMatchesDevice(device, inf->pinSCK, JSH_SPI_SCK) ||
      (jshIsPinValid(inf->pinMISO) && !rpSpiPinMatchesDevice(device, inf->pinMISO, JSH_SPI_MISO)) ||
      (jshIsPinValid(inf->pinMOSI) && !rpSpiPinMatchesDevice(device, inf->pinMOSI, JSH_SPI_MOSI))) {
    jsExceptionHere(JSET_ERROR, "Selected pins do not match %s", jshGetDeviceString(device));
    return false;
  }

  rpSpiUnsetupIdx(idx);

  if (inf->baudRate <= 0) inf->baudRate = 100000;
  spi_inst_t *inst = rpSpiFromIndex(idx);
  if (!inst) {
    jsExceptionHere(JSET_ERROR, "SPI backend unavailable");
    return false;
  }
  spi_init(inst, (uint)inf->baudRate);
  rpSpiMode[idx] = inf->spiMode & 3;
  rpSpiMsb[idx] = inf->spiMSB;
  rpSpiIs16[idx] = inf->numBits == 16;
  rpSpiReceive[idx] = jshIsPinValid(inf->pinMISO);
  rpSpiApplyFormat(idx);
  gpio_set_function(rpPinToGpio(inf->pinSCK), GPIO_FUNC_SPI);
  if (jshIsPinValid(inf->pinMISO))
    gpio_set_function(rpPinToGpio(inf->pinMISO), GPIO_FUNC_SPI);
  if (jshIsPinValid(inf->pinMOSI))
    gpio_set_function(rpPinToGpio(inf->pinMOSI), GPIO_FUNC_SPI);
  rpSpiMarkPins(idx, inf->pinSCK, inf->pinMISO, inf->pinMOSI);
  rpSpiInitialised[idx] = true;
  return true;
}

static bool rpSpiGetReady(IOEventFlags device, spi_inst_t **inst, int *idxOut) {
  int idx = rpSpiIndexFromDevice(device);
  if (idx < 0) {
    jsExceptionHere(JSET_ERROR, "Only SPI1 and SPI2 supported");
    return false;
  }
  if (!rpSpiInitialised[idx]) {
    jsExceptionHere(JSET_ERROR, "%s not initialised", jshGetDeviceString(device));
    return false;
  }
  *inst = rpSpiFromIndex(idx);
  if (idxOut) *idxOut = idx;
  return *inst != NULL;
}

static void rpI2cHandleError(const char *caller, int ret) {
  if (ret >= 0) return;
  if (ret == PICO_ERROR_TIMEOUT) {
    jsExceptionHere(JSET_ERROR, "%s: timeout", caller);
  } else {
    jsExceptionHere(JSET_ERROR, "%s: I2C I/O error", caller);
  }
}

static int rpWatchSlotForPin(Pin pin) {
  for (int i = 0; i < ESPR_EXTI_COUNT; i++)
    if (rpWatchPins[i] == pin) return i;
  return -1;
}

static void rpWatchDisablePinIrq(Pin pin) {
  if (!rpPinIsValid(pin)) return;
  gpio_set_irq_enabled(rpPinToGpio(pin), GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
}

static void rpWatchResetAll(void) {
  uint32_t irqState = save_and_disable_interrupts();
  for (int i = 0; i < ESPR_EXTI_COUNT; i++) {
    Pin pin = rpWatchPins[i];
    rpWatchPins[i] = PIN_UNDEFINED;
    rpWatchLastState[i] = false;
    if (rpPinIsValid(pin))
      rpWatchDisablePinIrq(pin);
  }
  restore_interrupts(irqState);
}

static void CALLED_FROM_INTERRUPT rpWatchBank0Irq(void) {
  for (int slot = 0; slot < ESPR_EXTI_COUNT; slot++) {
    Pin pin = rpWatchPins[slot];
    if (!rpPinIsValid(pin)) continue;
    uint gpio = rpPinToGpio(pin);
    uint32_t events = gpio_get_irq_event_mask(gpio) & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    if (!events) continue;
    gpio_acknowledge_irq(gpio, events);
    rpWatchLastState[slot] = jshPinGetValue(pin);
    jshPushIOWatchEvent((IOEventFlags)(EV_EXTI0 + slot));
  }
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
    case JSHPINSTATE_ADC_IN:
      if (rpPinHasAdc(pin)) {
        rpAdcEnsureInitialised();
        adc_gpio_init(gpio);
      } else {
        gpio_set_dir(gpio, GPIO_IN);
      }
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
  if (!rpWatchIrqHandlerInstalled) {
    irq_add_shared_handler(IO_IRQ_BANK0, rpWatchBank0Irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(IO_IRQ_BANK0, true);
    rpWatchIrqHandlerInstalled = true;
  }
  memset(rpPinState, JSHPINSTATE_GPIO_IN, sizeof(rpPinState));
  for (int i = 0; i < ESPR_EXTI_COUNT; i++) {
    rpWatchPins[i] = PIN_UNDEFINED;
    rpWatchLastState[i] = false;
  }
  BITFIELD_CLEAR(jshPinSoftPWM);
  rpI2cInitialised[0] = false;
  rpI2cInitialised[1] = false;
  rpI2cPinScl[0] = rpI2cPinSda[0] = PIN_UNDEFINED;
  rpI2cPinScl[1] = rpI2cPinSda[1] = PIN_UNDEFINED;
  rpSpiInitialised[0] = false;
  rpSpiInitialised[1] = false;
  rpSpiIs16[0] = rpSpiIs16[1] = false;
  rpSpiReceive[0] = rpSpiReceive[1] = true;
  rpSpiMode[0] = rpSpiMode[1] = SPIF_SPI_MODE_0;
  rpSpiMsb[0] = rpSpiMsb[1] = true;
  rpSpiPinSck[0] = rpSpiPinMiso[0] = rpSpiPinMosi[0] = PIN_UNDEFINED;
  rpSpiPinSck[1] = rpSpiPinMiso[1] = rpSpiPinMosi[1] = PIN_UNDEFINED;
  rpSystemTimeOffsetUs = 0;
  rpFirstIdle = true;
  jshInitDevices();
}

void jshReset() {
  jshResetDevices();
  memset(rpPinState, JSHPINSTATE_GPIO_IN, sizeof(rpPinState));
  BITFIELD_CLEAR(jshPinSoftPWM);
  rpWatchResetAll();
  rpI2cResetAll();
  rpSpiResetAll();
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
  rpSpiResetAll();
  rpI2cResetAll();
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
  if (BITFIELD_GET(jshPinSoftPWM, pin)) {
    BITFIELD_SET(jshPinSoftPWM, pin, 0);
    jstPinPWM(0, 0, pin);
  }
  rpPinState[pin] = state;
  rpPinApplyState(pin, state);
}

JshPinState jshPinGetState(Pin pin) {
  if (!rpPinIsValid(pin)) return JSHPINSTATE_UNDEFINED;
  return rpPinState[pin];
}

JsVarFloat jshPinAnalog(Pin pin) {
  if (!rpPinHasAdc(pin)) return NAN;
  if (!jshGetPinStateIsManual(pin))
    jshPinSetState(pin, JSHPINSTATE_ADC_IN);
  rpAdcEnsureInitialised();
  adc_select_input(rpPinToAdcChannel(pin));
  uint16_t raw = adc_read();
  JsVarFloat value = (JsVarFloat)raw / 4096.0;
  if (pinInfo[pin].port & JSH_PIN_NEGATED) value = 1.0 - value;
  return value;
}

int jshPinAnalogFast(Pin pin) {
  if (!rpPinHasAdc(pin)) return 0;
  if (!jshGetPinStateIsManual(pin))
    jshPinSetState(pin, JSHPINSTATE_ADC_IN);
  rpAdcEnsureInitialised();
  adc_select_input(rpPinToAdcChannel(pin));
  int value = adc_read() << 4;
  if (pinInfo[pin].port & JSH_PIN_NEGATED) value = 65535 - value;
  return value;
}

JshPinFunction jshPinAnalogOutput(Pin pin, JsVarFloat value, JsVarFloat freq, JshAnalogOutputFlags flags) {
  if (!rpPinIsValid(pin)) return JSH_NOTHING;
  if (value < 0) value = 0;
  if (value > 1) value = 1;
  if (pinInfo[pin].port & JSH_PIN_NEGATED) value = 1.0 - value;

  if (flags & JSAOF_FORCE_SOFTWARE) {
    if (!jshGetPinStateIsManual(pin)) {
      BITFIELD_SET(jshPinSoftPWM, pin, 0);
      jshPinSetState(pin, JSHPINSTATE_GPIO_OUT);
    }
    BITFIELD_SET(jshPinSoftPWM, pin, 1);
    if (freq <= 0 || !isfinite(freq)) freq = 50;
    jstPinPWM(freq, value, pin);
    return JSH_NOTHING;
  }

  uint gpio = rpPinToGpio(pin);
  uint slice = pwm_gpio_to_slice_num(gpio);
  uint channel = pwm_gpio_to_channel(gpio);
  if (!rpPwmConfigure(slice, freq)) {
    if (flags & JSAOF_ALLOW_SOFTWARE) {
      if (!jshGetPinStateIsManual(pin)) {
        BITFIELD_SET(jshPinSoftPWM, pin, 0);
        jshPinSetState(pin, JSHPINSTATE_GPIO_OUT);
      }
      BITFIELD_SET(jshPinSoftPWM, pin, 1);
      if (freq <= 0 || !isfinite(freq)) freq = 50;
      jstPinPWM(freq, value, pin);
      return JSH_NOTHING;
    }
    return JSH_NOTHING;
  }

  if (!jshGetPinStateIsManual(pin))
    jshPinSetState(pin, JSHPINSTATE_AF_OUT);
  gpio_set_function(gpio, GPIO_FUNC_PWM);
  uint32_t top = pwm_hw->slice[slice].top;
  uint32_t cc = (uint32_t)((value * (JsVarFloat)(top + 1)) + 0.5);
  if (cc > top + 1) cc = top + 1;
  pwm_set_chan_level(slice, channel, cc);
  pwm_set_enabled(slice, true);
  return JSH_NOTHING;
}

bool jshCanWatch(Pin pin) {
  // Current RP2040 policy is intentionally permissive. If later target-specific
  // pin conflicts appear, this is the place to exclude them.
  return rpPinIsValid(pin);
}

IOEventFlags jshPinWatch(Pin pin, bool shouldWatch, JshPinWatchFlags flags) {
  // RP2040 currently uses one IRQ-backed watch path for all watch modes.
  // `flags` is reserved for later refinement if Espruino watch-speed/accuracy
  // distinctions need target-specific handling here.
  NOT_USED(flags);
  if (!rpPinIsValid(pin)) return EV_NONE;
  if (shouldWatch) {
    uint32_t irqState = save_and_disable_interrupts();
    for (int i = 0; i < ESPR_EXTI_COUNT; i++) {
      if (rpWatchPins[i] == pin) {
        rpWatchLastState[i] = jshPinGetValue(pin);
        restore_interrupts(irqState);
        return (IOEventFlags)(EV_EXTI0 + i);
      }
      if (rpWatchPins[i] == PIN_UNDEFINED) {
        rpWatchPins[i] = pin;
        rpWatchLastState[i] = jshPinGetValue(pin);
        restore_interrupts(irqState);
        uint gpio = rpPinToGpio(pin);
        gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        return (IOEventFlags)(EV_EXTI0 + i);
      }
    }
    restore_interrupts(irqState);
  } else {
    uint32_t irqState = save_and_disable_interrupts();
    int slot = rpWatchSlotForPin(pin);
    if (slot >= 0) {
      rpWatchPins[slot] = PIN_UNDEFINED;
      rpWatchLastState[slot] = false;
      restore_interrupts(irqState);
      rpWatchDisablePinIrq(pin);
      return EV_NONE;
    }
    restore_interrupts(irqState);
  }
  return EV_NONE;
}

bool jshGetWatchedPinState(IOEventFlags device) {
  int i = (int)IOEVENTFLAGS_GETTYPE(device) - (int)EV_EXTI0;
  if (i < 0 || i >= ESPR_EXTI_COUNT) return false;
  return rpWatchLastState[i];
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
  int spiIdx = rpSpiIndexFromDevice(device);
  if (spiIdx >= 0) return rpSpiInitialised[spiIdx];
  int i2cIdx = rpI2cIndexFromDevice(device);
  if (i2cIdx >= 0) return rpI2cInitialised[i2cIdx];
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
  if (!inf) return;
  rpSpiEnsureInitialised(device, inf);
}

int jshSPISend(IOEventFlags device, int data) {
  spi_inst_t *inst;
  int idx;
  if (!rpSpiGetReady(device, &inst, &idx)) return -1;
  if (rpSpiIs16[idx]) {
    uint16_t tx = data >= 0 ? (uint16_t)data : 0xFFFFu;
    if (!rpSpiReceive[idx]) {
      spi_write16_blocking(inst, &tx, 1);
      return -1;
    }
    uint16_t rx = 0;
    spi_write16_read16_blocking(inst, &tx, &rx, 1);
    return rx;
  }

  uint8_t tx = data >= 0 ? (uint8_t)data : 0xFFu;
  if (!rpSpiReceive[idx]) {
    spi_write_blocking(inst, &tx, 1);
    return -1;
  }
  uint8_t rx = 0;
  spi_write_read_blocking(inst, &tx, &rx, 1);
  return rx;
}

void jshSPISend16(IOEventFlags device, int data) {
  spi_inst_t *inst;
  int idx;
  if (!rpSpiGetReady(device, &inst, &idx)) return;
  uint16_t tx = (uint16_t)data;
  if (rpSpiIs16[idx]) {
    if (rpSpiReceive[idx]) {
      uint16_t rx;
      spi_write16_read16_blocking(inst, &tx, &rx, 1);
    } else {
      spi_write16_blocking(inst, &tx, 1);
    }
  } else {
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)data };
    if (rpSpiReceive[idx]) {
      uint8_t rx[2];
      spi_write_read_blocking(inst, buf, rx, 2);
    } else {
      spi_write_blocking(inst, buf, 2);
    }
  }
}

void jshSPISet16(IOEventFlags device, bool is16) {
  spi_inst_t *inst;
  int idx;
  if (!rpSpiGetReady(device, &inst, &idx)) return;
  NOT_USED(inst);
  rpSpiIs16[idx] = is16;
  rpSpiApplyFormat(idx);
}

void jshSPISetReceive(IOEventFlags device, bool isReceive) {
  spi_inst_t *inst;
  int idx;
  if (!rpSpiGetReady(device, &inst, &idx)) return;
  NOT_USED(inst);
  rpSpiReceive[idx] = isReceive && rpPinIsValid(rpSpiPinMiso[idx]);
}

void jshSPIWait(IOEventFlags device) {
  spi_inst_t *inst;
  if (!rpSpiGetReady(device, &inst, NULL)) return;
  while (spi_get_hw(inst)->sr & SPI_SSPSR_BSY_BITS)
    tight_loop_contents();
  while (spi_is_readable(inst))
    (void)spi_get_hw(inst)->dr;
  spi_get_hw(inst)->icr = SPI_SSPICR_RORIC_BITS;
}

bool jshSPISendMany(IOEventFlags device, unsigned char *tx, unsigned char *rx, size_t count, void (*callback)()) {
  spi_inst_t *inst;
  int idx;
  if (!rpSpiGetReady(device, &inst, &idx)) return false;
  if (!count) {
    if (callback) callback();
    return true;
  }

  if (rpSpiIs16[idx]) {
    size_t halfwords = count / 2;
    if (halfwords) {
      if (tx && rx) {
        for (size_t i = 0; i < halfwords; i++) {
          uint16_t t = ((uint16_t)tx[2 * i] << 8) | tx[2 * i + 1];
          uint16_t r = 0;
          spi_write16_read16_blocking(inst, &t, &r, 1);
          rx[2 * i] = (unsigned char)(r >> 8);
          rx[2 * i + 1] = (unsigned char)r;
        }
      } else if (tx) {
        for (size_t i = 0; i < halfwords; i++) {
          uint16_t t = ((uint16_t)tx[2 * i] << 8) | tx[2 * i + 1];
          spi_write16_blocking(inst, &t, 1);
        }
      } else if (rx) {
        for (size_t i = 0; i < halfwords; i++) {
          uint16_t r = 0;
          spi_read16_blocking(inst, 0xFFFFu, &r, 1);
          rx[2 * i] = (unsigned char)(r >> 8);
          rx[2 * i + 1] = (unsigned char)r;
        }
      }
    }
    if (count & 1) {
      unsigned char tailTx = tx ? tx[count - 1] : 0xFFu;
      if (rx) {
        unsigned char tailRx = 0;
        spi_write_read_blocking(inst, &tailTx, &tailRx, 1);
        rx[count - 1] = tailRx;
      } else {
        spi_write_blocking(inst, &tailTx, 1);
      }
    }
  } else {
    if (tx && rx)
      spi_write_read_blocking(inst, tx, rx, count);
    else if (tx)
      spi_write_blocking(inst, tx, count);
    else if (rx)
      spi_read_blocking(inst, 0xFFu, rx, count);
  }

  if (callback) callback();
  return true;
}

void jshI2CSetup(IOEventFlags device, JshI2CInfo *inf) {
  if (!inf) return;
  rpI2cEnsureInitialised(device, inf);
}

void jshI2CUnSetup(IOEventFlags device) {
  int idx = rpI2cIndexFromDevice(device);
  if (idx >= 0) rpI2cUnsetupIdx(idx);
}

void jshI2CWrite(IOEventFlags device, unsigned char address, int nBytes, const unsigned char *data, bool sendStop) {
  if (nBytes <= 0) return;
  if (!data) {
    jsExceptionHere(JSET_ERROR, "jshI2CWrite: no data");
    return;
  }
  i2c_inst_t *inst;
  if (!rpI2cGetReady(device, &inst)) return;
  int ret = i2c_write_timeout_us(inst, address, data, (size_t)nBytes, !sendStop, 50000);
  rpI2cHandleError("jshI2CWrite", ret);
}

void jshI2CRead(IOEventFlags device, unsigned char address, int nBytes, unsigned char *data, bool sendStop) {
  if (!data || nBytes <= 0) return;
  i2c_inst_t *inst;
  if (!rpI2cGetReady(device, &inst)) {
    memset(data, 0, (size_t)nBytes);
    return;
  }
  int ret = i2c_read_timeout_us(inst, address, data, (size_t)nBytes, !sendStop, 50000);
  if (ret < 0) {
    memset(data, 0, (size_t)nBytes);
    rpI2cHandleError("jshI2CRead", ret);
  }
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
