#
# Definitions for the build of the RP2040
#

RP2040=1

DEFINES += -DRP2040 -DARM -DESPR_DEFINES_ON_COMMANDLINE -DPICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
INCLUDE += -I$(ROOT)/targets/rp2040
INCLUDE += -I$(ROOT)/targetlibs/arm

SOURCES += targets/rp2040/main.c \
targets/rp2040/jshardware.c
