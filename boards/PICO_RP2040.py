#!/bin/false
# This file is part of Espruino, a JavaScript interpreter for Microcontrollers
#
# Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pinutils

info = {
  "name": "Raspberry Pi Pico (RP2040)",
  "boardname": "ESPR_PICO_RP2040",
  "default_console": "EV_USBSERIAL",
  "variables": 12000,
  "io_buffer_size": 4096,
  "binary_name": "espruino_%v_pico_rp2040.uf2",
  "build": {
    "optimizeflags": "-Og",
    "libraries": [],
    "makefile": [
      "DEFINES+=-DRP2040=1"
    ]
  }
}

chip = {
  "part": "RP2040",
  "family": "RP2040",
  "package": "QFN56",
  "ram": 264,
  "flash": 2048,
  "speed": 133,
  "usart": 0,
  "spi": 0,
  "i2c": 0,
  "adc": 1,
  "dac": 0
}

devices = {
  "USB": {}
}


def get_pins():
  # Pico exposes GPIO 0..29.
  pins = pinutils.generate_pins(0, 29)

  # ADC channels on GPIO 26..29.
  pinutils.findpin(pins, "PD26", True)["functions"]["ADC1_IN0"] = 0
  pinutils.findpin(pins, "PD27", True)["functions"]["ADC1_IN1"] = 0
  pinutils.findpin(pins, "PD28", True)["functions"]["ADC1_IN2"] = 0
  pinutils.findpin(pins, "PD29", True)["functions"]["ADC1_IN3"] = 0

  for pin in pins:
    pin["functions"]["3.3"] = 0

  return pins
