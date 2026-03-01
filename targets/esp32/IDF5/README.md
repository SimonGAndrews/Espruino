ESP32 IDF5 builds
=================

First install the 5.5.3 IDF and have it on your path: https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/get-started/index.html

```
BOARD=ESP32_IDF5 RELEASE=1 make
```

## STATUS 

Work in progress

- added IDF5 files and repo
- fixed make issues
- working on ESP32 warning turned to error

Next steps

- update to IDF5 api

updated 03-01-2026


## How it works

To enable, in `BOARD.py` set `family` to `ESP32_IDF5`. 

* `make/family/ESP32_IDF5.make` is then called, and creates a `build` folder in `bin/build`
* `make/targets/ESP32_IDF5.make` creates a CMakeFile and calls `idf.py` to do the build

## Setup

So far we need:

```
idf.py set-target esp32

idf.py menuconfig
# then
# Component - > FreeRTOS -> Kernel -> CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY must be enabled
# ESP NETIF Adapter -> Enable backward compatible tcpip_adapter interface
```
