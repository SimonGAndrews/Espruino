# Test/product profile layered over ESP32_IDF5 through Make's SETDEFINES hook.
# Keep the stock 14-byte JsVar format and trade 5 KB of native-heap reserve for
# additional JsVar blocks. The stock board remains unchanged.
DEFINES := $(filter-out -DESP_HEAP_SIZE=70000,$(DEFINES))
DEFINES += -DESP_HEAP_SIZE=65000
