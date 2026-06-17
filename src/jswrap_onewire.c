/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * JavaScript OneWire Functions
 * ----------------------------------------------------------------------------
 */
#include "jswrap_onewire.h"
#include "jsdevices.h"
#include "jsinteractive.h"

#ifdef ESP32
#include "freertos/FreeRTOS.h"
static portMUX_TYPE JSQuietTimingMux = portMUX_INITIALIZER_UNLOCKED;
// Override the normal ESP32 interrupt stubs in this file only so OneWire
// timing windows use an IDF critical section.
#define jshInterruptOff() portENTER_CRITICAL(&JSQuietTimingMux)
#define jshInterruptOn() portEXIT_CRITICAL(&JSQuietTimingMux)
#endif

#if defined(BLUETOOTH) && defined(NRF52_SERIES)
#define ESPR_ONEWIRE_IN_TIMESLOT
#endif

#ifdef ESPR_ONEWIRE_IN_TIMESLOT
#include "bluetooth.h"
#include "nrf_soc.h"
#define ONEWIREREAD_FN _OneWireRead
#define ONEWIREWRITE_FN _OneWireWrite
#else
#define ONEWIREREAD_FN OneWireRead
#define ONEWIREWRITE_FN OneWireWrite
#endif

/*JSON{
  "type" : "class",
  "class" : "OneWire"
}
This class provides a software-defined OneWire master. It is designed to be
similar to Arduino's OneWire library.

**Note:** OneWire commands are very timing-sensitive, and on nRF52 devices
(Bluetooth LE Espruino boards) the bluetooth stack can get in the way. Before
version 2v18 of Espruino OneWire could be unreliable, but as of firmware 2v18
Espruino now schedules OneWire accesses with the bluetooth stack to ensure it doesn't interfere.
OneWire is now reliable but some functions such as `OneWire.search` can now take
a while to execute (around 1 second).

 */

static Pin onewire_getpin(JsVar *parent) {
  return jshGetPinFromVarAndUnLock(jsvObjectGetChildIfExists(parent, "pin"));
}

#define ONEWIRE_SEARCH_DEBUG_MAX_PASSES 8
#define ONEWIRE_SEARCH_DEBUG_MAX_SLOTS 64

typedef struct {
  bool resetOk;
  bool abortedOn11;
  bool searchResult;
  bool rom0Valid;
  int abortBit;
  int slotsUsed;
  int idBitsRead;
  int lastZero;
  int lastDiscrepancyIn;
  int lastDiscrepancyOut;
  bool lastDeviceFlagIn;
  bool lastDeviceFlagOut;
  unsigned char romNo[8];
  uint32_t slots[ONEWIRE_SEARCH_DEBUG_MAX_SLOTS];
} OneWireSearchDebugPass;

static void onewire_rom_to_string(const unsigned char romNo[8], char buf[17]) {
  int i;
  for (i=0;i<8;i++) {
    buf[i*2] = itoch((romNo[i]>>4) & 15);
    buf[i*2+1] = itoch(romNo[i] & 15);
  }
  buf[16] = 0;
}

static uint32_t onewire_search_debug_encode_slot(
  int idBitNumber,
  unsigned char idBit,
  unsigned char cmpIdBit,
  unsigned char searchDirection,
  int lastZero,
  int romByteNumber,
  unsigned char romByteValue
) {
  return ((uint32_t)(idBitNumber & 0x7F)) |
         ((uint32_t)(idBit & 1) << 7) |
         ((uint32_t)(cmpIdBit & 1) << 8) |
         ((uint32_t)(searchDirection & 1) << 9) |
         ((uint32_t)((idBit == cmpIdBit) ? 1 : 0) << 10) |
         ((uint32_t)(romByteNumber & 7) << 11) |
         ((uint32_t)(lastZero & 0x7F) << 14) |
         ((uint32_t)romByteValue << 21);
}

static JsVar *onewire_search_debug_slots_to_js(const uint32_t *slots, int count) {
  JsVar *arr = jsvNewTypedArray(ARRAYBUFFERVIEW_UINT32, count);
  if (!arr) return 0;
  JsvArrayBufferIterator it;
  jsvArrayBufferIteratorNew(&it, arr, 0);
  while (count-- > 0) {
    jsvArrayBufferIteratorSetIntegerValue(&it, (JsVarInt)*(slots++));
    jsvArrayBufferIteratorNext(&it);
  }
  jsvArrayBufferIteratorFree(&it);
  return arr;
}

static JsVar *onewire_search_debug_to_js(
  Pin pin,
  int command,
  JsVar *devices,
  const OneWireSearchDebugPass *passes,
  int passCount,
  bool passOverflow
) {
  JsVar *result = jsvNewObject();
  JsVar *passArray = jsvNewEmptyArray();
  int i;
  if (!result || !passArray) {
    jsvUnLock2(result, passArray);
    jsvUnLock(devices);
    return 0;
  }

  jsvObjectSetChildAndUnLock(result, "pin", jsvNewFromPin(pin));
  jsvObjectSetChildAndUnLock(result, "command", jsvNewFromInteger(command));
  jsvObjectSetChildAndUnLock(result, "devices", devices);
  jsvObjectSetChildAndUnLock(result, "passOverflow", jsvNewFromBool(passOverflow));

  for (i=0;i<passCount;i++) {
    JsVar *pass = jsvNewObject();
    if (!pass) continue;
    jsvObjectSetChildAndUnLock(pass, "resetOk", jsvNewFromBool(passes[i].resetOk));
    jsvObjectSetChildAndUnLock(pass, "abortedOn11", jsvNewFromBool(passes[i].abortedOn11));
    jsvObjectSetChildAndUnLock(pass, "searchResult", jsvNewFromBool(passes[i].searchResult));
    jsvObjectSetChildAndUnLock(pass, "rom0Valid", jsvNewFromBool(passes[i].rom0Valid));
    jsvObjectSetChildAndUnLock(pass, "abortBit", jsvNewFromInteger(passes[i].abortBit));
    jsvObjectSetChildAndUnLock(pass, "idBitsRead", jsvNewFromInteger(passes[i].idBitsRead));
    jsvObjectSetChildAndUnLock(pass, "lastZero", jsvNewFromInteger(passes[i].lastZero));
    jsvObjectSetChildAndUnLock(pass, "lastDiscrepancyIn", jsvNewFromInteger(passes[i].lastDiscrepancyIn));
    jsvObjectSetChildAndUnLock(pass, "lastDiscrepancyOut", jsvNewFromInteger(passes[i].lastDiscrepancyOut));
    jsvObjectSetChildAndUnLock(pass, "lastDeviceFlagIn", jsvNewFromBool(passes[i].lastDeviceFlagIn));
    jsvObjectSetChildAndUnLock(pass, "lastDeviceFlagOut", jsvNewFromBool(passes[i].lastDeviceFlagOut));

    {
      char rom[17];
      onewire_rom_to_string(passes[i].romNo, rom);
      jsvObjectSetChildAndUnLock(pass, "rom", jsvNewFromString(rom));
    }

    jsvObjectSetChildAndUnLock(
      pass,
      "slots",
      onewire_search_debug_slots_to_js(passes[i].slots, passes[i].slotsUsed)
    );
    jsvArrayPushAndUnLock(passArray, pass);
  }

  jsvObjectSetChildAndUnLock(result, "passes", passArray);
  return result;
}

/** Reset one-wire, return true if a device was present */
static bool NO_INLINE OneWireReset(Pin pin) {
  jshPinSetState(pin, JSHPINSTATE_GPIO_OUT_OPENDRAIN_PULLUP);
  jshInterruptOff();
  jshPinSetValue(pin, 0);
  jshDelayMicroseconds(500);
  jshPinSetValue(pin, 1);
  jshDelayMicroseconds(80);
  bool hasDevice = !jshPinGetValue(pin);
  jshInterruptOn();
  jshDelayMicroseconds(420);
  return hasDevice;
}

/** Write 'bits' bits, and return what was read (to read, you must send all 1s) */
static JsVarInt NO_INLINE ONEWIREREAD_FN(Pin pin, int bits) {
  jshPinSetState(pin, JSHPINSTATE_GPIO_OUT_OPENDRAIN_PULLUP);
  JsVarInt result = 0;
  JsVarInt mask = 1;
  while (bits-- > 0) {
    jshInterruptOff();
    jshPinSetValue(pin, 0);
    jshDelayMicroseconds(3);
    jshPinSetValue(pin, 1);
    jshDelayMicroseconds(10); // leave time to let it rise
    if (jshPinGetValue(pin))
      result = result | mask;
    jshInterruptOn();
    jshDelayMicroseconds(53);
    mask = mask << 1;
  }
  return result;
}

/** Write 'bits' bits, and return what was read (to read, you must send all 1s) */
static void NO_INLINE ONEWIREWRITE_FN(Pin pin, int bits, unsigned long long data) {
  jshPinSetState(pin, JSHPINSTATE_GPIO_OUT_OPENDRAIN_PULLUP);
  unsigned long long mask = 1;
  while (bits-- > 0) {
    if (data & mask) { // short pulse
      jshInterruptOff();
      jshPinSetValue(pin, 0);
      jshDelayMicroseconds(10);
      jshPinSetValue(pin, 1);
      jshInterruptOn();
      jshDelayMicroseconds(55);
    } else {  // long pulse
      jshInterruptOff();
      jshPinSetValue(pin, 0);
      jshDelayMicroseconds(65);
      jshPinSetValue(pin, 1);
      jshInterruptOn();
      jshDelayMicroseconds(5);
    }
    mask = mask << 1;
  }
}

#ifdef ESPR_ONEWIRE_IN_TIMESLOT
// Timeslot event handler
static bool m_onewire_timeslot_busy = false;
static Pin m_onewire_timeslot_pin;
static int m_onewire_timeslot_bits; // negative value means a read, positive is write
static unsigned long long m_onewire_timeslot_data;
static nrf_radio_signal_callback_return_param_t m_signal_callback_return_param;

nrf_radio_signal_callback_return_param_t * radio_callback(uint8_t signal_type) {
  switch(signal_type) {
      case NRF_RADIO_CALLBACK_SIGNAL_TYPE_START:
          // now perform the command
          if (m_onewire_timeslot_bits < 0) { // read
            m_onewire_timeslot_data = _OneWireRead(m_onewire_timeslot_pin, -m_onewire_timeslot_bits);
          } else { // write
            _OneWireWrite(m_onewire_timeslot_pin, m_onewire_timeslot_bits, m_onewire_timeslot_data);
          }
          m_onewire_timeslot_busy = false;
          // request a timeslot end
          m_signal_callback_return_param.params.request.p_next = NULL;
          m_signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
          break;
      default:
          break;
  }
  return (&m_signal_callback_return_param);
}

JsVarInt OneWirePerformInTimeSlot(Pin pin, int bits, unsigned long long data) {
  uint32_t err_code;
  err_code = sd_radio_session_open(radio_callback);
  if (jsble_check_error(err_code)) return 0;

  m_onewire_timeslot_pin = pin;
  m_onewire_timeslot_bits = bits;
  m_onewire_timeslot_data = data;
  m_onewire_timeslot_busy = true;

  // work out how much time we need - very pessimistic here!
  uint32_t time_us;
  if (bits<0) { //  a read
    time_us = 100 - bits*100;
  } else { //  a write
    time_us = 100 + bits*100;
  }

  static nrf_radio_request_t  m_timeslot_request;
  memset(&m_timeslot_request, 0, sizeof(m_timeslot_request));
  m_timeslot_request.request_type               = NRF_RADIO_REQ_TYPE_EARLIEST;
  m_timeslot_request.params.earliest.hfclk        = NRF_RADIO_HFCLK_CFG_XTAL_GUARANTEED;
  m_timeslot_request.params.earliest.priority     = NRF_RADIO_PRIORITY_HIGH;
  m_timeslot_request.params.earliest.timeout_us  = NRF_RADIO_EARLIEST_TIMEOUT_MAX_US;
  m_timeslot_request.params.earliest.length_us    = time_us;

  err_code = sd_radio_request(&m_timeslot_request);
  if (!jsble_check_error(err_code)) {
    WAIT_UNTIL(!m_onewire_timeslot_busy, "OneWire timeslot");
  }
  m_onewire_timeslot_busy = false;

  err_code = sd_radio_session_close();
  if (jsble_check_error(err_code)) return 0;

  return m_onewire_timeslot_data;
}

static JsVarInt NO_INLINE OneWireRead(Pin pin, int bits) {
  return OneWirePerformInTimeSlot(pin, -bits, 0);
}

/** Write 'bits' bits, and return what was read (to read, you must send all 1s) */
static void NO_INLINE OneWireWrite(Pin pin, int bits, unsigned long long data) {
  OneWirePerformInTimeSlot(pin, bits, data);
}
#endif

/*JSON{
  "type" : "constructor",
  "class" : "OneWire",
  "name" : "OneWire",
  "generate" : "jswrap_onewire_constructor",
  "params" : [
    ["pin","pin","The pin to implement OneWire on"]
  ],
  "return" : ["JsVar","A OneWire object"]
}
Create a software OneWire implementation on the given pin
 */
JsVar *jswrap_onewire_constructor(Pin pin) {
  JsVar *ow = jspNewObject(0, "OneWire");
  if (!ow) return 0;
  jsvObjectSetChildAndUnLock(ow, "pin", jsvNewFromPin(pin));
  return ow;
}


/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "reset",
  "generate" : "jswrap_onewire_reset",
  "return" : ["bool","True is a device was present (it held the bus low)"]
}
Perform a reset cycle
 */
bool jswrap_onewire_reset(JsVar *parent) {
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return 0;
  return OneWireReset(pin);
}

/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "select",
  "generate" : "jswrap_onewire_select",
  "params" : [
    ["rom","JsVar","The device to select (get this using `OneWire.search()`)"]
  ]
}
Select a ROM - always performs a reset first
 */
void jswrap_onewire_select(JsVar *parent, JsVar *rom) {
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return;
  if (!jsvIsString(rom) || jsvGetStringLength(rom)!=16) {
    jsExceptionHere(JSET_TYPEERROR, "Invalid OneWire device address %q", rom);
    return;
  }

  // perform a reset
  OneWireReset(pin);

  // decode the address
  unsigned long long romdata = 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, rom, 0);
  int i;
  for (i=0;i<8;i++) {
    char b[3];
    b[0] = jsvStringIteratorGetCharAndNext(&it);
    b[1] = jsvStringIteratorGetCharAndNext(&it);
    b[2] = 0;
    romdata = romdata | (((unsigned long long)stringToIntWithRadix(b,16,NULL,NULL)) << (i*8));

  }
  jsvStringIteratorFree(&it);

  // finally write data out
  OneWireWrite(pin, 8, 0x55);
  OneWireWrite(pin, 64, romdata);
}

/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "skip",
  "generate" : "jswrap_onewire_skip"
}
Skip a ROM
 */
void jswrap_onewire_skip(JsVar *parent) {
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return;
  OneWireWrite(pin, 8, 0xCC);
}

/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "write",
  "generate" : "jswrap_onewire_write",
  "params" : [
    ["data","JsVar","A byte (or array of bytes) to write"],
    ["power","bool","Whether to leave power on after write (default is false)"]
  ]
}
Write one or more bytes
 */
void _jswrap_onewire_write_cb(int data, Pin *pin) {
  OneWireWrite(*pin, 8, (unsigned int)data);
}

void jswrap_onewire_write(JsVar *parent, JsVar *data, bool leavePowerOn) {
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return;

  jsvIterateCallback(data, (void (*)(int,  void *))_jswrap_onewire_write_cb, (void*)&pin);

  if (leavePowerOn) {
    // We're asked to leave power on for parasitically powered devices, to do that properly we
    // need to actively pull the line high. This is required, for example, for parasitically
    // powered DS18B20 temperature sensors.
    jshPinSetValue(pin, 1);
    jshPinSetState(pin, JSHPINSTATE_GPIO_OUT);
  } else {
    // We don't need to leave power on, so just tri-state the pin
    jshPinSetState(pin, JSHPINSTATE_GPIO_IN);
    jshPinSetValue(pin, 1);
  }
}

/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "read",
  "generate" : "jswrap_onewire_read",
  "params" : [["count","JsVar","[optional] The amount of bytes to read"]],
  "return" : ["JsVar","The byte that was read, or a `Uint8Array` if count was specified and >=0"]
}
Read a byte
 */
JsVar *jswrap_onewire_read(JsVar *parent, JsVar *count) {
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return 0;
  if (jsvIsNumeric(count)) {
    JsVarInt c = jsvGetInteger(count);
    JsVar *arr = jsvNewTypedArray(ARRAYBUFFERVIEW_UINT8, c);
    if (!arr) return 0;
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, arr, 0);
    while (c--) {
      jsvArrayBufferIteratorSetByteValue(&it, (char)OneWireRead(pin, 8));
      jsvArrayBufferIteratorNext(&it);
    }
    jsvArrayBufferIteratorFree(&it);
    return arr;
  } else {
    return jsvNewFromInteger(OneWireRead(pin, 8));
  }
}


/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "search",
  "generate" : "jswrap_onewire_search",
  "params" : [
    ["command","int32","(Optional) command byte. If not specified (or zero), this defaults to 0xF0. This can could be set to 0xEC to perform a DS18B20 'Alarm Search Command'"]
  ],
  "return" : ["JsVar","An array of devices that were found"]
}
Search for devices
 */
static JsVar *jswrap_onewire_search_internal(JsVar *parent, int command, bool returnDebug) {
  // search - code from http://www.maximintegrated.com/app-notes/index.mvp/id/187
  Pin pin = onewire_getpin(parent);
  if (!jshIsPinValid(pin)) return 0;

  JsVar *array = jsvNewEmptyArray();
  if (!array) return 0;

  OneWireSearchDebugPass debugPasses[ONEWIRE_SEARCH_DEBUG_MAX_PASSES];
  int debugPassCount = 0;
  bool debugPassOverflow = false;
  memset(debugPasses, 0, sizeof(debugPasses));

  if (command<=0 || command>255)
    command = 0xF0; // normal search command

  // global search state
  unsigned char ROM_NO[8];
  int LastDiscrepancy;
  int LastFamilyDiscrepancy;
  int LastDeviceFlag;

  // reset the search state
  memset(ROM_NO, 0, sizeof(ROM_NO));
  LastDiscrepancy = 0;
  LastDeviceFlag = FALSE;
  LastFamilyDiscrepancy = 0;

  int search_result = true;

  while (search_result) {

    int id_bit_number;
    int last_zero, rom_byte_number;
    unsigned char id_bit, cmp_id_bit;
    unsigned char rom_byte_mask, search_direction;

    // initialize for search
    id_bit_number = 1;
    last_zero = 0;
    rom_byte_number = 0;
    rom_byte_mask = 1;
    search_result = 0;

    // if the last call was not the last one
    if (!LastDeviceFlag)
    {
      OneWireSearchDebugPass *debugPass = 0;
      if (returnDebug) {
        if (debugPassCount < ONEWIRE_SEARCH_DEBUG_MAX_PASSES) {
          debugPass = &debugPasses[debugPassCount++];
          memset(debugPass, 0, sizeof(*debugPass));
          debugPass->lastDiscrepancyIn = LastDiscrepancy;
          debugPass->lastDeviceFlagIn = LastDeviceFlag;
        } else {
          debugPassOverflow = true;
        }
      }

      // 1-Wire reset
      if (!OneWireReset(pin))
      {
        if (debugPass)
          debugPass->resetOk = false;
        // reset the search
        LastDiscrepancy = 0;
        LastDeviceFlag = FALSE;
        LastFamilyDiscrepancy = 0;
        if (returnDebug)
          return onewire_search_debug_to_js(pin, command, array, debugPasses, debugPassCount, debugPassOverflow);
        return array;
      }
      if (debugPass)
        debugPass->resetOk = true;

      // issue the search command
      OneWireWrite(pin, 8, (unsigned long long)command);

      // loop to do the search
      do
      {
        // read a bit and its complement
        id_bit = (unsigned char)OneWireRead(pin, 1);
        cmp_id_bit = (unsigned char)OneWireRead(pin, 1);

        // check for no devices on 1-wire
        if ((id_bit == 1) && (cmp_id_bit == 1)) {
          if (debugPass) {
            debugPass->abortedOn11 = true;
            debugPass->abortBit = id_bit_number;
            if (debugPass->slotsUsed < ONEWIRE_SEARCH_DEBUG_MAX_SLOTS)
              debugPass->slots[debugPass->slotsUsed++] = onewire_search_debug_encode_slot(
                id_bit_number, id_bit, cmp_id_bit, 0, last_zero, rom_byte_number, ROM_NO[rom_byte_number]
              );
          }
          break;
        } else
        {
          // all devices coupled have 0 or 1
          if (id_bit != cmp_id_bit)
            search_direction = id_bit;  // bit write value for search
          else
          {
            // if this discrepancy if before the Last Discrepancy
            // on a previous next then pick the same as last time
            if (id_bit_number < LastDiscrepancy)
              search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
            else
              // if equal to last pick 1, if not then pick 0
              search_direction = (id_bit_number == LastDiscrepancy);

            // if 0 was picked then record its position in LastZero
            if (search_direction == 0)
            {
              last_zero = id_bit_number;

              // check for Last discrepancy in family
              if (last_zero < 9)
                LastFamilyDiscrepancy = last_zero;
            }
          }

          // set or clear the bit in the ROM byte rom_byte_number
          // with mask rom_byte_mask
          if (search_direction == 1)
            ROM_NO[rom_byte_number] |= rom_byte_mask;
          else
            ROM_NO[rom_byte_number] &= (unsigned char)~rom_byte_mask;

          if (debugPass && debugPass->slotsUsed < ONEWIRE_SEARCH_DEBUG_MAX_SLOTS)
            debugPass->slots[debugPass->slotsUsed++] = onewire_search_debug_encode_slot(
              id_bit_number, id_bit, cmp_id_bit, search_direction, last_zero, rom_byte_number, ROM_NO[rom_byte_number]
            );

          // serial number search direction write bit
          OneWireWrite(pin, 1, search_direction);

          // increment the byte counter id_bit_number
          // and shift the mask rom_byte_mask
          id_bit_number++;
          rom_byte_mask = (unsigned char)(rom_byte_mask << 1);

          // if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask
          if (rom_byte_mask == 0)
          {
            rom_byte_number++;
            rom_byte_mask = 1;
          }
        }
      }
      while(rom_byte_number < 8);  // loop until through all ROM bytes 0-7

      // if the search was successful then
      if (!((id_bit_number < 65)))
      {
        // search successful so set LastDiscrepancy,LastDeviceFlag,search_result
        LastDiscrepancy = last_zero;

        // check for last device
        if (LastDiscrepancy == 0)
          LastDeviceFlag = TRUE;

        search_result = TRUE;
      }

      if (debugPass) {
        debugPass->searchResult = search_result;
        debugPass->rom0Valid = ROM_NO[0] != 0;
        debugPass->idBitsRead = id_bit_number - 1;
        debugPass->lastZero = last_zero;
        debugPass->lastDiscrepancyOut = LastDiscrepancy;
        debugPass->lastDeviceFlagOut = LastDeviceFlag;
        memcpy(debugPass->romNo, ROM_NO, sizeof(ROM_NO));
      }
    }

    // if no device found then reset counters so next 'search' will be like a first
    if (!search_result || !ROM_NO[0])
    {
      LastDiscrepancy = 0;
      LastDeviceFlag = FALSE;
      LastFamilyDiscrepancy = 0;
      search_result = FALSE;
    }

    if (search_result) {
      int i;
      char buf[17];
      onewire_rom_to_string(ROM_NO, buf);
      jsvArrayPushString(array, buf);
    }

    NOT_USED(LastFamilyDiscrepancy);
  }

  if (returnDebug)
    return onewire_search_debug_to_js(pin, command, array, debugPasses, debugPassCount, debugPassOverflow);
  return array;
}

JsVar *jswrap_onewire_search(JsVar *parent, int command) {
  return jswrap_onewire_search_internal(parent, command, false);
}

/*JSON{
  "type" : "method",
  "class" : "OneWire",
  "name" : "searchDebug",
  "generate" : "jswrap_onewire_searchDebug",
  "params" : [
    ["command","int32","(Optional) command byte. If not specified (or zero), this defaults to 0xF0. This can could be set to 0xEC to perform a DS18B20 'Alarm Search Command'"]
  ],
  "return" : ["JsVar","A debug object containing devices plus compact per-pass slot traces"]
}
Search for devices and return compact debug trace data for each ROM search pass.
 */
JsVar *jswrap_onewire_searchDebug(JsVar *parent, int command) {
  return jswrap_onewire_search_internal(parent, command, true);
}
