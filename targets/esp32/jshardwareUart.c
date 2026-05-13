/*
 * This file is designed to support FREERTOS functions in Espruino,
 * a JavaScript interpreter for Microcontrollers designed by Gordon Williams
 *
 * Copyright (C) 2016 by Juergen Marsch
 *
 * This Source Code Form is subject to the terms of the Mozilla Publici
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * Contains ESP32 board specific functions.
 * ----------------------------------------------------------------------------
 */

#include "jshardwareUart.h"
#include "driver/uart.h"
#include <stdio.h>
#include <string.h>
#include "jsdevices.h"
#include "jsinteractive.h"

#ifdef ESPR_USE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif

bool serial2_initialized = false;
bool serial3_initialized = false;

void jshSetDeviceInitialised(IOEventFlags device, bool isInit);

void initUart(int uart_num, uart_config_t uart_config, int txpin, int rxpin){
  esp_err_t err;

  err = uart_param_config(uart_num, &uart_config);
  ESP_ERROR_CHECK(err);
  err = uart_set_pin(uart_num, txpin, rxpin, -1, -1);
  ESP_ERROR_CHECK(err);
  err = uart_driver_install(uart_num, 1024, 1024, 0, NULL, 0);
  ESP_ERROR_CHECK(err);
}

void UartReset(){
  uart_driver_delete(uart_console);
  initConsole();

  if(serial2_initialized){
    uart_driver_delete(uart_Serial2);
    serial2_initialized = false;
  }
#if ESPR_USART_COUNT>2
  if(serial3_initialized){
    uart_driver_delete(uart_Serial3);
    serial3_initialized = false;
  }
#endif
}

void initSerial(IOEventFlags device, JshUSARTInfo *inf){
  uart_config_t uart_config = {
    .baud_rate = inf->baudRate,
    .data_bits = (inf->bytesize == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS,
    .stop_bits = (inf->stopbits == 1) ? UART_STOP_BITS_1 : UART_STOP_BITS_2,
    .parity = UART_PARITY_DISABLE,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
  };
  switch(inf->parity){
    case 0: uart_config.parity = UART_PARITY_DISABLE; break;
    case 1: uart_config.parity = UART_PARITY_ODD; break;
    case 2: uart_config.parity = UART_PARITY_EVEN; break;
  }
  if(device == EV_SERIAL1){
    initUart(uart_console, uart_config, -1, -1);
    jshSetFlowControlEnabled(device, inf->xOnXOff, inf->pinCTS);
    jshSetDeviceInitialised(EV_SERIAL1, true);
  }
  else if(device == EV_SERIAL2){
    if(inf->pinTX == 0xFF) inf->pinTX = 4;
    if(inf->pinRX == 0xFF) inf->pinRX = 5;

    if(serial2_initialized) uart_driver_delete(uart_Serial2);

    initUart(uart_Serial2, uart_config, inf->pinTX, inf->pinRX);
    jshSetFlowControlEnabled(device, inf->xOnXOff, inf->pinCTS);
    jshSetDeviceInitialised(EV_SERIAL2, true);
    serial2_initialized = true;
  }
#if ESPR_USART_COUNT>2
  else if(device == EV_SERIAL3){
    if(inf->pinTX == 0xFF) inf->pinTX = 17;
    if(inf->pinRX == 0xFF) inf->pinRX = 16;
    if(serial3_initialized) uart_driver_delete(uart_Serial3);
    initUart(uart_Serial3, uart_config, inf->pinTX, inf->pinRX);
    jshSetFlowControlEnabled(device, inf->xOnXOff, inf->pinCTS);
    jshSetDeviceInitialised(EV_SERIAL3, true);
    serial3_initialized = true;
  }
#endif
}

void initConsole(){
#ifdef ESPR_USE_USB_SERIAL_JTAG
  /* Configure USB-CDC driver */
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t usb_serial_config = {
      .tx_buffer_size = 128,
      .rx_buffer_size = 128
    };
    jsDebug(DBG_INFO, "initConsole: Installing usb_serial_jtag_driver\n");
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_config));
  }
#endif
  uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
  };
  initUart(uart_console, uart_config, -1, -1);
  jshSetFlowControlEnabled(EV_SERIAL1, true, PIN_UNDEFINED);
  jshSetDeviceInitialised(EV_SERIAL1, true);
}

static uint8_t rxbuf[256];

void consoleToEspruino(){
  TickType_t ticksToWait = pdMS_TO_TICKS(50);
#ifdef ESPR_USE_USB_SERIAL_JTAG
  int len = usb_serial_jtag_read_bytes(rxbuf, sizeof(rxbuf), ticksToWait);
#else
  int len = uart_read_bytes(uart_console, rxbuf, sizeof(rxbuf), ticksToWait);
#endif
  if(len > 0) jshPushIOCharEvents(EV_SERIAL1, rxbuf, len);
}

void serialToEspruino(){
  int len;
  if(serial2_initialized){
    len = uart_read_bytes(uart_Serial2, rxbuf, sizeof(rxbuf), 0);
    if(len > 0) jshPushIOCharEvents(EV_SERIAL2, rxbuf, len);
  }

#if ESPR_USART_COUNT>2
  if(serial3_initialized){
    len = uart_read_bytes(uart_Serial3, rxbuf, sizeof(rxbuf), 0);
    if(len > 0) jshPushIOCharEvents(EV_SERIAL3, rxbuf, len);
  }
#endif
}

void writeSerial(IOEventFlags device, uint8_t c){
  char str[2] = { (char)c, '\0' };
  int r = 0;
  if(device == EV_SERIAL2 && serial2_initialized){
    r = uart_write_bytes(uart_Serial2, str, 1);
  }
#if ESPR_USART_COUNT>2
  else if(device == EV_SERIAL3 && serial3_initialized){
    r = uart_write_bytes(uart_Serial3, str, 1);
  }
#endif
  (void)r; 
}
