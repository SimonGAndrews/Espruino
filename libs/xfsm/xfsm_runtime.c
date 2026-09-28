/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "xfsm_runtime.h"

#include "jsparse.h"
#include "jsvariterator.h"
#include "jswrapper.h"
#include "xfsm_internal.h"
#include "xfsm_measure.h"
#include "xfsm_native.h"
#include "xfsm_test.h"

#include <string.h>

#define XFC_MAX_MICROSTEPS UINT16_C(256)

#ifndef XFC_STACK_RESERVE
#define XFC_STACK_RESERVE 1024U
#endif

#define XFC_ESPRUINO_STACK_SAFETY 512U

typedef struct {
  JsVar *arena;
  JsVar *retained;
  const uint8_t *bytes;
  size_t length;
  XfcArenaHeader header;
} XfcRuntimeView;

typedef struct {
  JsVar *actor;
  JsVar *machine;
  XfcRuntimeView view;
  XfcActorData data;
} XfcRuntime;

static JsVar *xfcActorStart(JsVar *actor);
static void xfcActorSend(JsVar *actor, JsVar *event);
static JsVar *xfcActorStop(JsVar *actor);
static JsVar *xfcActorGetSnapshot(JsVar *actor);
static JsVar *xfcActorSubscribe(JsVar *actor, JsVar *arguments);
static bool xfcSnapshotMatches(JsVar *snapshot, JsVar *value);
static void xfcSubscriptionUnsubscribe(JsVar *subscription);

static bool xfcIsPlainObject(JsVar *value) {
  return value && jsvIsObject(value) && !jsvIsArray(value);
}

static JsVar *xfcUndefined(void) { return 0; }

static JsVar *xfcPrivateObject(const char *name) {
  JsVar *value = jsvObjectGetChildIfExists(execInfo.hiddenRoot, name);
  if (value) return value;
  value = jsvNewObject();
  if (value) jsvObjectSetChild(execInfo.hiddenRoot, name, value);
  return value;
}

static bool xfcSetBrand(JsVar *object, const char *child_name,
                        const char *token_name) {
  JsVar *token = xfcPrivateObject(token_name);
  bool ok = object && token &&
            jsvObjectSetChild(object, child_name, token) == token;
  jsvUnLock(token);
  return ok;
}

static bool xfcHasBrand(JsVar *object, const char *child_name,
                        const char *token_name) {
  JsVar *brand = 0;
  JsVar *token = 0;
  bool valid = false;
  if (!xfcIsPlainObject(object)) return false;
  brand = jsvObjectGetChildIfExists(object, child_name);
  token = jsvObjectGetChildIfExists(execInfo.hiddenRoot, token_name);
  valid = brand && token && brand == token;
  jsvUnLock2(brand, token);
  return valid;
}

bool xfcBrandMachine(JsVar *machine) {
  return xfcSetBrand(machine, XFC_MACHINE_BRAND_NAME, XFC_ROOT_MACHINE_TOKEN);
}

bool xfcIsMachine(JsVar *machine) {
  return xfcHasBrand(machine, XFC_MACHINE_BRAND_NAME, XFC_ROOT_MACHINE_TOKEN);
}

bool xfcBrandAssignment(JsVar *descriptor) {
  return xfcSetBrand(descriptor, XFC_ASSIGN_BRAND_NAME, XFC_ROOT_ASSIGN_TOKEN);
}

bool xfcIsAssignment(JsVar *descriptor) {
  return xfcHasBrand(descriptor, XFC_ASSIGN_BRAND_NAME, XFC_ROOT_ASSIGN_TOKEN);
}

static bool xfcSetMethod(JsVar *prototype, const char *name,
                         void (*function)(void), unsigned short arguments) {
  JsVar *native = jsvNewNativeFunction(function, arguments);
  bool ok = native && jsvObjectSetChild(prototype, name, native) == native;
  jsvUnLock(native);
  return ok;
}

static JsVar *xfcPrototype(const char *root_name) {
  JsVar *prototype = jsvObjectGetChildIfExists(execInfo.hiddenRoot, root_name);
  if (prototype) return prototype;
  prototype = jsvNewObject();
  if (!prototype) return 0;
  if (strcmp(root_name, XFC_ROOT_ACTOR_PROTOTYPE) == 0) {
    if (!xfcSetMethod(prototype, "start", (void (*)(void))xfcActorStart,
                      JSWAT_JSVAR | JSWAT_THIS_ARG) ||
        !xfcSetMethod(prototype, "send", (void (*)(void))xfcActorSend,
                      JSWAT_VOID | JSWAT_THIS_ARG |
                          (JSWAT_JSVAR << JSWAT_BITS)) ||
        !xfcSetMethod(prototype, "stop", (void (*)(void))xfcActorStop,
                      JSWAT_JSVAR | JSWAT_THIS_ARG) ||
        !xfcSetMethod(prototype, "getSnapshot",
                      (void (*)(void))xfcActorGetSnapshot,
                      JSWAT_JSVAR | JSWAT_THIS_ARG) ||
        !xfcSetMethod(prototype, "subscribe",
                      (void (*)(void))xfcActorSubscribe,
                      JSWAT_JSVAR | JSWAT_THIS_ARG |
                          (JSWAT_ARGUMENT_ARRAY << JSWAT_BITS)))
      goto fail;
  } else if (strcmp(root_name, XFC_ROOT_SNAPSHOT_PROTOTYPE) == 0) {
    if (!xfcSetMethod(prototype, "matches",
                      (void (*)(void))xfcSnapshotMatches,
                      JSWAT_BOOL | JSWAT_THIS_ARG |
                          (JSWAT_JSVAR << JSWAT_BITS)))
      goto fail;
  } else if (strcmp(root_name, XFC_ROOT_SUBSCRIPTION_PROTOTYPE) == 0) {
    if (!xfcSetMethod(prototype, "unsubscribe",
                      (void (*)(void))xfcSubscriptionUnsubscribe,
                      JSWAT_VOID | JSWAT_THIS_ARG))
      goto fail;
  } else {
    goto fail;
  }
  jsvObjectSetChild(execInfo.hiddenRoot, root_name, prototype);
  return prototype;
fail:
  jsvUnLock(prototype);
  return 0;
}

static bool xfcAttachPrototype(JsVar *object, const char *root_name) {
  JsVar *prototype = xfcPrototype(root_name);
  bool ok = prototype &&
            jsvObjectSetChild(object, JSPARSE_INHERITS_VAR, prototype) ==
                prototype;
  jsvUnLock(prototype);
  return ok;
}

static void xfcNoMemory(const char *path) {
  jsExceptionHere(JSET_ERROR, "XFC E_NO_MEMORY @ %s", path);
}

static bool xfcSetProperty(JsVar *object, const char *name, JsVar *value) {
  JsVar *property = jsvFindOrAddChildFromString(object, name);
  if (!property) return false;
  jsvSetValueOfName(property, value);
  jsvUnLock(property);
  return true;
}

static void xfcActorInvalid(const char *method) {
  jsExceptionHere(JSET_TYPEERROR, "XFC E_ACTOR_INVALID @ actor.%s", method);
}

static void xfcReceiverInvalid(const char *path) {
  jsExceptionHere(JSET_TYPEERROR, "XFC E_RECEIVER_INVALID @ %s", path);
}

static bool xfcTakeException(JsVar **exception) {
  if ((execInfo.execute & EXEC_EXCEPTION) == 0) return false;
  *exception = jspGetException();
  execInfo.execute = (JsExecFlags)(execInfo.execute &
                                   (JsExecFlags)(~(unsigned int)EXEC_EXCEPTION));
  if (!*exception)
    *exception = xfcPrivateObject(XFC_ROOT_UNDEFINED_EXCEPTION);
  return true;
}

static bool xfcIsUndefinedException(JsVar *exception) {
  JsVar *token = jsvObjectGetChildIfExists(execInfo.hiddenRoot,
                                           XFC_ROOT_UNDEFINED_EXCEPTION);
  bool matches = exception && token && exception == token;
  jsvUnLock(token);
  return matches;
}

static void xfcRaise(JsVar *exception) {
  if (exception)
    jspSetException(xfcIsUndefinedException(exception) ? 0 : exception);
}

static bool xfcRefreshView(XfcRuntimeView *view) {
  if (!view || !jsvIsString(view->arena)) return false;
  view->length = jsvGetStringLength(view->arena);
  view->bytes = (const uint8_t *)jsvGetFlatStringPointer(view->arena);
  if (!view->bytes || view->length < sizeof(view->header)) return false;
  memcpy(&view->header, view->bytes, sizeof(view->header));
  return view->header.format_version == XFC_ARENA_FORMAT_VERSION &&
         view->header.header_size == XFC_ARENA_HEADER_SIZE &&
         view->header.arena_size == view->length;
}

static bool xfcReadRecord(const XfcRuntimeView *view, uint16_t table,
                          uint16_t index, void *record, size_t size) {
  XfcTableRef reference;
  uint32_t offset;
  if (!view || !record || table >= XFC_TABLE_COUNT) return false;
  reference = view->header.tables[table];
  if (reference.record_size != size || index >= reference.count) return false;
  offset = reference.offset + (uint32_t)index * reference.record_size;
  if (offset > view->header.arena_size ||
      (uint32_t)size > view->header.arena_size - offset)
    return false;
  memcpy(record, view->bytes + offset, size);
  return true;
}

#define XFC_READ_FUNCTION(name, table_name, record_type)                 \
  static bool name(const XfcRuntimeView *view, uint16_t index,           \
                   record_type *record) {                               \
    return xfcReadRecord(view, table_name, index, record, sizeof(*record)); \
  }

XFC_READ_FUNCTION(xfcReadState, XFC_TABLE_STATE, XfcStateRecord)
XFC_READ_FUNCTION(xfcReadSymbol, XFC_TABLE_SYMBOL, XfcSymbolRecord)
XFC_READ_FUNCTION(xfcReadHandler, XFC_TABLE_HANDLER, XfcHandlerRecord)
XFC_READ_FUNCTION(xfcReadTransition, XFC_TABLE_TRANSITION,
                  XfcTransitionRecord)
XFC_READ_FUNCTION(xfcReadGuard, XFC_TABLE_GUARD, XfcGuardRecord)
XFC_READ_FUNCTION(xfcReadAction, XFC_TABLE_ACTION, XfcActionRecord)
XFC_READ_FUNCTION(xfcReadAssignment, XFC_TABLE_ASSIGNMENT,
                  XfcAssignmentRecord)
XFC_READ_FUNCTION(xfcReadAssignmentEntry, XFC_TABLE_ASSIGNMENT_ENTRY,
                  XfcAssignmentEntryRecord)

static bool xfcSymbolBounds(const XfcRuntimeView *view, uint16_t index,
                            XfcSymbolRecord *symbol, const uint8_t **bytes) {
  uint32_t end;
  if (!xfcReadSymbol(view, index, symbol)) return false;
  end = symbol->string_offset + symbol->byte_length;
  if (symbol->string_offset < view->header.string_offset ||
      end < symbol->string_offset || end > view->header.arena_size)
    return false;
  *bytes = view->bytes + symbol->string_offset;
  return true;
}

static uint32_t xfcStringHash(JsVar *text) {
  JsvStringIterator iterator;
  uint32_t hash = UINT32_C(2166136261);
  jsvStringIteratorNew(&iterator, text, 0);
  while (jsvStringIteratorHasChar(&iterator)) {
    hash ^= (uint8_t)jsvStringIteratorGetChar(&iterator);
    hash *= UINT32_C(16777619);
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return hash;
}

static size_t xfcStringByteLength(JsVar *text) {
  JsvStringIterator iterator;
  size_t length = 0;
  jsvStringIteratorNew(&iterator, text, 0);
  while (jsvStringIteratorHasChar(&iterator)) {
    length++;
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return length;
}

static bool xfcSymbolEqualsHashed(const XfcRuntimeView *view, uint16_t index,
                                  JsVar *text, uint32_t hash) {
  XfcSymbolRecord symbol;
  const uint8_t *bytes;
  JsvStringIterator iterator;
  size_t length;
  uint16_t offset = 0;
  bool equal;
  if (!jsvIsString(text)) return false;
  length = xfcStringByteLength(text);
  if (length > UINT16_MAX ||
      !xfcSymbolBounds(view, index, &symbol, &bytes) ||
      hash != symbol.hash || length != symbol.byte_length)
    return false;
  equal = true;
  jsvStringIteratorNew(&iterator, text, 0);
  while (equal && jsvStringIteratorHasChar(&iterator)) {
    equal = (uint8_t)jsvStringIteratorGetChar(&iterator) == bytes[offset++];
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return equal && offset == symbol.byte_length;
}

static bool xfcSymbolEquals(const XfcRuntimeView *view, uint16_t index,
                            JsVar *text) {
  return jsvIsString(text) &&
         xfcSymbolEqualsHashed(view, index, text, xfcStringHash(text));
}

static JsVar *xfcSymbolString(const XfcRuntimeView *view, uint16_t index) {
  XfcSymbolRecord symbol;
  const uint8_t *bytes;
  if (!xfcSymbolBounds(view, index, &symbol, &bytes)) return 0;
  return jsvNewStringOfLength(symbol.byte_length, (const char *)bytes);
}

static bool xfcOpenMachine(JsVar *machine, XfcRuntimeView *view,
                           bool full_validation) {
  memset(view, 0, sizeof(*view));
  if (!xfcIsMachine(machine)) return false;
  view->arena = jsvObjectGetChildIfExists(machine, XFC_MACHINE_ARENA_NAME);
  view->retained =
      jsvObjectGetChildIfExists(machine, XFC_MACHINE_RETAINED_NAME);
  if (!jsvIsString(view->arena) || !jsvIsArray(view->retained) ||
      !xfcRefreshView(view) ||
      (full_validation &&
       xfcValidateArena(view->bytes, view->length) != XFC_VALIDATION_OK)) {
    jsvUnLock2(view->arena, view->retained);
    memset(view, 0, sizeof(*view));
    return false;
  }
  return true;
}

static void xfcCloseView(XfcRuntimeView *view) {
  jsvUnLock2(view->arena, view->retained);
  memset(view, 0, sizeof(*view));
}

static bool xfcReadActorData(JsVar *actor, XfcActorData *data,
                             JsVar **storage) {
  JsVar *value = jsvObjectGetChildIfExists(actor, XFC_ACTOR_DATA_NAME);
  const char *bytes;
  if (!jsvIsString(value) ||
      jsvGetStringLength(value) != XFC_ACTOR_DATA_SIZE ||
      !(bytes = jsvGetFlatStringPointer(value))) {
    jsvUnLock(value);
    return false;
  }
  memcpy(data, bytes, sizeof(*data));
  *storage = value;
  return true;
}

static void xfcWriteActorData(JsVar *storage, const XfcActorData *data) {
  char *bytes = jsvGetFlatStringPointer(storage);
  if (bytes) memcpy(bytes, data, sizeof(*data));
}

static bool xfcOpenActor(JsVar *actor, const char *method, XfcRuntime *runtime,
                         JsVar **data_storage) {
  memset(runtime, 0, sizeof(*runtime));
  *data_storage = 0;
  if (!xfcHasBrand(actor, XFC_ACTOR_BRAND_NAME, XFC_ROOT_ACTOR_TOKEN)) {
    xfcActorInvalid(method);
    return false;
  }
  runtime->actor = actor;
  runtime->machine =
      jsvObjectGetChildIfExists(actor, XFC_ACTOR_MACHINE_NAME);
  if (!runtime->machine ||
      !xfcOpenMachine(runtime->machine, &runtime->view, false) ||
      !xfcReadActorData(actor, &runtime->data, data_storage) ||
      xfcValidateActorData(
          &runtime->data,
          runtime->view.header.tables[XFC_TABLE_STATE].count) !=
          XFC_VALIDATION_OK) {
    jsvUnLock(*data_storage);
    *data_storage = 0;
    xfcCloseView(&runtime->view);
    jsvUnLock(runtime->machine);
    runtime->machine = 0;
    xfcActorInvalid(method);
    return false;
  }
  return true;
}

static void xfcCloseActor(XfcRuntime *runtime, JsVar *data_storage) {
  jsvUnLock(data_storage);
  xfcCloseView(&runtime->view);
  jsvUnLock(runtime->machine);
  memset(runtime, 0, sizeof(*runtime));
}

static const char *xfcOperationName(uint8_t operation) {
  static const char *const names[] = {"idle", "start", "send", "stop",
                                      "notify"};
  return operation <= XFC_OPERATION_NOTIFY ? names[operation] : "unknown";
}

static bool xfcRequireIdle(XfcRuntime *runtime, const char *method) {
  if (runtime->data.operation != XFC_OPERATION_IDLE) {
    jsExceptionHere(JSET_ERROR,
                    "XFC E_ACTOR_BUSY @ actor.%s: operation=%s", method,
                    xfcOperationName(runtime->data.operation));
    return false;
  }
  return true;
}

static bool xfcBeginOperation(XfcRuntime *runtime, JsVar *storage,
                              uint8_t operation, const char *method) {
  size_t free_stack;
  if (!xfcRequireIdle(runtime, method)) return false;
  free_stack = jsuGetFreeStack();
  if (free_stack != SIZE_MAX &&
      free_stack < XFC_STACK_RESERVE + XFC_ESPRUINO_STACK_SAFETY) {
    jsExceptionHere(JSET_ERROR,
                    "XFC E_LIMIT_EXCEEDED @ actor.%s: stack", method);
    return false;
  }
  runtime->data.operation = operation;
  runtime->data.microsteps = 0;
  xfcWriteActorData(storage, &runtime->data);
  xfcMeasureOperationBegin(operation);
  return true;
}

static JsVar *xfcRetained(const XfcRuntimeView *view, uint16_t index) {
  if (index == XFC_INDEX_NONE ||
      index >= view->header.retained_count)
    return 0;
  return jsvGetArrayItem(view->retained, index);
}

static bool xfcCall(JsVar *function, int argument_count, JsVar **arguments,
                    JsVar **result, JsVar **error) {
  *result = jspExecuteFunction(function, 0, argument_count, arguments);
  if (xfcTakeException(error)) {
    jsvUnLock(*result);
    *result = 0;
    return false;
  }
  return true;
}

static bool xfcCopyProperties(JsVar *target, JsVar *source, JsVar **error) {
  JsvObjectIterator iterator;
  bool ok = true;
  jsvObjectIteratorNew(&iterator, source);
  while (ok && jsvObjectIteratorHasValue(&iterator)) {
    JsVar *key = jsvObjectIteratorGetKey(&iterator);
    if (!jsvIsInternalObjectKey(key)) {
      JsVar *value = jspGetVarNamedField(source, key, false);
      if (xfcTakeException(error)) {
        ok = false;
      } else if (jsvObjectSetChildVar(target, key, value) != value && value) {
        xfcNoMemory("actor.runtime.assign");
        xfcTakeException(error);
        ok = false;
      }
      jsvUnLock(value);
    }
    jsvUnLock(key);
    if (ok) jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  return ok;
}

static bool xfcExecuteAssignment(XfcRuntime *runtime,
                                 const XfcAssignmentRecord *assignment,
                                 JsVar **context, JsVar *event,
                                 JsVar **error) {
  JsVar *old_context = *context;
  JsVar *next_context = xfcTestTakeFault(XFC_TEST_FAULT_ASSIGN_CONTEXT)
                            ? 0
                            : jsvNewObject();
  bool ok = next_context != 0;
  uint16_t offset;
  if (!ok) {
    xfcNoMemory("actor.runtime.assign");
    xfcTakeException(error);
    return false;
  }
  if (!xfcCopyProperties(next_context, old_context, error)) goto done;
  if (assignment->kind == XFC_ASSIGN_PARTIAL) {
    JsVar *function = xfcRetained(&runtime->view,
                                  assignment->retained_slot);
    JsVar *arguments[2] = {old_context, event};
    JsVar *partial = 0;
    if (!function || !xfcCall(function, 2, arguments, &partial, error)) {
      ok = false;
    } else if (!xfcIsPlainObject(partial)) {
      jsExceptionHere(JSET_TYPEERROR,
                      "XFC E_CONTEXT_INVALID @ actor.runtime.assign");
      xfcTakeException(error);
      ok = false;
    } else if (!xfcCopyProperties(next_context, partial, error)) {
      ok = false;
    }
    jsvUnLock3(function, partial, 0);
  } else {
    for (offset = 0; ok && offset < assignment->entries.count; offset++) {
      XfcAssignmentEntryRecord entry;
      JsVar *key = 0;
      JsVar *value = 0;
      if (!xfcRefreshView(&runtime->view) ||
          !xfcReadAssignmentEntry(
              &runtime->view,
              (uint16_t)(assignment->entries.first + offset), &entry)) {
        jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.runtime.assign");
        xfcTakeException(error);
        ok = false;
        break;
      }
      key = xfcSymbolString(&runtime->view, entry.key_symbol);
      if (!key) {
        xfcNoMemory("actor.runtime.assign");
        xfcTakeException(error);
        ok = false;
        break;
      }
      if ((entry.flags & XFC_ASSIGN_ENTRY_EXPRESSION) != 0) {
        JsVar *function = xfcRetained(&runtime->view, entry.retained_slot);
        JsVar *arguments[2] = {old_context, event};
        if (!function || !xfcCall(function, 2, arguments, &value, error))
          ok = false;
        jsvUnLock(function);
      } else {
        value = xfcRetained(&runtime->view, entry.retained_slot);
      }
      if (ok && jsvObjectSetChildVar(next_context, key, value) != value &&
          value) {
        xfcNoMemory("actor.runtime.assign");
        xfcTakeException(error);
        ok = false;
      }
      jsvUnLock2(key, value);
    }
  }
done:
  if (ok) {
    *context = next_context;
    jsvUnLock(old_context);
  } else {
    jsvUnLock(next_context);
  }
  return ok;
}

static bool xfcExecuteActions(XfcRuntime *runtime, XfcRange range,
                              JsVar **context, JsVar *event,
                              bool *context_changed, JsVar **error) {
  uint16_t offset;
  xfcMeasureStackSample();
  for (offset = 0; offset < range.count; offset++) {
    XfcActionRecord action;
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadAction(&runtime->view,
                       (uint16_t)(range.first + offset), &action)) {
      jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.runtime.action");
      xfcTakeException(error);
      return false;
    }
    if (action.kind == XFC_ACTION_USER) {
      JsVar *function = xfcRetained(&runtime->view, action.reference);
      JsVar *arguments[2] = {*context, event};
      JsVar *result = 0;
      bool ok = function &&
                xfcCall(function, 2, arguments, &result, error);
      jsvUnLock2(function, result);
      if (!ok) return false;
    } else {
      XfcAssignmentRecord assignment;
      if (!xfcReadAssignment(&runtime->view, action.reference, &assignment) ||
          !xfcExecuteAssignment(runtime, &assignment, context, event, error))
        return false;
      *context_changed = true;
    }
  }
  return true;
}

static JsVar *xfcInternalEvent(const char *type) {
  JsVar *event = jsvNewObject();
  JsVar *event_type = jsvNewFromString(type);
  if (!event || !event_type ||
      jsvObjectSetChild(event, "type", event_type) != event_type) {
    jsvUnLock2(event, event_type);
    return 0;
  }
  jsvUnLock(event_type);
  return event;
}

static JsVar *xfcInternalEventFromSymbol(XfcRuntimeView *view,
                                         uint16_t symbol_index) {
  JsVar *event = xfcTestTakeFault(XFC_TEST_FAULT_COMPLETION_EVENT)
                     ? 0
                     : jsvNewObject();
  JsVar *event_type = xfcSymbolString(view, symbol_index);
  if (!event || !event_type ||
      jsvObjectSetChild(event, "type", event_type) != event_type) {
    jsvUnLock2(event, event_type);
    return 0;
  }
  jsvUnLock(event_type);
  return event;
}

static bool xfcBeginMicrostep(XfcRuntime *runtime, JsVar **error) {
  const char *method = runtime->data.operation == XFC_OPERATION_START
                           ? "start"
                           : "send";
  if (runtime->data.microsteps >= XFC_MAX_MICROSTEPS) {
    jsExceptionHere(JSET_ERROR,
                    "XFC E_MICROSTEP_LIMIT @ actor.%s: max=256", method);
    xfcTakeException(error);
    return false;
  }
  runtime->data.microsteps++;
  return true;
}

static bool xfcInitialDescent(XfcRuntime *runtime, uint16_t state_index,
                              bool enter_state, bool run_initial_actions,
                              JsVar **context,
                              JsVar *event, bool *context_changed,
                              uint16_t *leaf, JsVar **error) {
  XfcStateRecord state;
  uint16_t steps = 0;
  xfcMeasureStackSample();
  while (steps++ <= XFC_MAX_STATE_DEPTH) {
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, state_index, &state)) {
      jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.start.initial");
      xfcTakeException(error);
      return false;
    }
    if (enter_state &&
        !xfcExecuteActions(runtime, state.entry_actions, context, event,
                           context_changed, error))
      return false;
    if ((state.flags & XFC_STATE_TYPE_MASK) != XFC_STATE_COMPOUND) {
      *leaf = state_index;
      return true;
    }
    if (run_initial_actions &&
        !xfcExecuteActions(runtime, state.initial_actions, context, event,
                           context_changed, error))
      return false;
    state_index = state.initial;
    enter_state = true;
    run_initial_actions = true;
  }
  jsExceptionHere(JSET_ERROR, "XFC E_LIMIT_EXCEEDED @ actor.start.initial");
  xfcTakeException(error);
  return false;
}

static bool xfcGuardEnabled(XfcRuntime *runtime, uint16_t guard_index,
                            JsVar *context, JsVar *event, bool *enabled,
                            JsVar **error) {
  XfcGuardRecord guard;
  JsVar *function;
  JsVar *arguments[2] = {context, event};
  JsVar *result = 0;
  xfcMeasureStackSample();
  if (!xfcReadGuard(&runtime->view, guard_index, &guard)) return false;
  function = xfcRetained(&runtime->view, guard.retained_slot);
  if (!function || !xfcCall(function, 2, arguments, &result, error)) {
    jsvUnLock(function);
    return false;
  }
  *enabled = jsvGetBool(result);
  jsvUnLock2(function, result);
  return true;
}

static bool xfcSelectFromRange(XfcRuntime *runtime, XfcRange range,
                               JsVar *context, JsVar *event,
                               XfcTransitionRecord *selected,
                               bool *found, JsVar **error) {
  uint16_t offset;
  *found = false;
  for (offset = 0; offset < range.count; offset++) {
    XfcTransitionRecord transition;
    bool enabled = true;
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadTransition(&runtime->view,
                           (uint16_t)(range.first + offset), &transition))
      return false;
    if (transition.guard != XFC_INDEX_NONE &&
        !xfcGuardEnabled(runtime, transition.guard, context, event, &enabled,
                         error))
      return false;
    if (enabled) {
      *selected = transition;
      *found = true;
      return true;
    }
  }
  return true;
}

static bool xfcSelectTransition(XfcRuntime *runtime, JsVar *event_type,
                                uint32_t event_hash, JsVar *context,
                                JsVar *event,
                                XfcTransitionRecord *selected,
                                bool *found, JsVar **error) {
  uint16_t state_index = runtime->data.leaf_state;
  uint16_t depth = 0;
  xfcMeasureStackSample();
  *found = false;
  while (state_index != XFC_INDEX_NONE && depth++ <= XFC_MAX_STATE_DEPTH) {
    XfcStateRecord state;
    uint16_t handler_offset;
    XfcRange exact = {XFC_INDEX_NONE, 0};
    XfcRange wildcard = {XFC_INDEX_NONE, 0};
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, state_index, &state))
      return false;
    for (handler_offset = 0; handler_offset < state.handlers.count;
         handler_offset++) {
      XfcHandlerRecord handler;
      if (!xfcReadHandler(&runtime->view,
                          (uint16_t)(state.handlers.first + handler_offset),
                          &handler))
        return false;
      if ((handler.flags & XFC_HANDLER_WILDCARD) != 0) {
        wildcard = handler.transitions;
      } else if (xfcSymbolEqualsHashed(&runtime->view, handler.event_symbol,
                                       event_type, event_hash))
        exact = handler.transitions;
    }
    if (exact.count != 0) {
      bool ok = xfcSelectFromRange(runtime, exact, context, event, selected,
                                   found, error);
      if (!ok || *found) return ok;
    }
    if (wildcard.count != 0) {
      bool ok = xfcSelectFromRange(runtime, wildcard, context, event,
                                   selected, found, error);
      if (!ok || *found) return ok;
    }
    state_index = state.parent;
  }
  return true;
}

static bool xfcExecuteTransition(XfcRuntime *runtime,
                                 const XfcTransitionRecord *transition,
                                 JsVar **context, JsVar *event,
                                 bool *context_changed, uint16_t *leaf,
                                 bool *state_changed, JsVar **error) {
  uint16_t current;
  XfcStateRecord state;
  uint16_t path[XFC_MAX_STATE_DEPTH + 1];
  uint16_t path_count = 0;
  uint16_t target;
  xfcMeasureStackSample();
  if (transition->target_state == XFC_INDEX_NONE)
    return xfcExecuteActions(runtime, transition->actions, context, event,
                             context_changed, error);

  current = *leaf;
  while (current != transition->domain_state) {
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, current, &state) ||
        !xfcExecuteActions(runtime, state.exit_actions, context, event,
                           context_changed, error))
      return false;
    current = state.parent;
  }
  if (!xfcExecuteActions(runtime, transition->actions, context, event,
                         context_changed, error))
    return false;

  target = transition->target_state;
  current = target;
  while (current != transition->domain_state) {
    if (path_count > XFC_MAX_STATE_DEPTH ||
        !xfcReadState(&runtime->view, current, &state))
      return false;
    path[path_count++] = current;
    current = state.parent;
  }
  while (path_count != 0) {
    uint16_t entered = path[--path_count];
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, entered, &state) ||
        !xfcExecuteActions(runtime, state.entry_actions, context, event,
                           context_changed, error))
      return false;
  }
  if (!xfcInitialDescent(runtime, target, false,
                         transition->domain_state != target, context, event,
                         context_changed, leaf, error))
    return false;
  *state_changed = *leaf != runtime->data.leaf_state;
  return true;
}

static bool xfcCompleteActor(XfcRuntime *runtime, JsVar **context,
                             JsVar *event, bool *context_changed,
                             uint16_t leaf, JsVar **error) {
  uint16_t current = leaf;
  uint16_t depth = 0;
  while (current != XFC_INDEX_NONE && depth++ <= XFC_MAX_STATE_DEPTH) {
    XfcStateRecord state;
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, current, &state) ||
        !xfcExecuteActions(runtime, state.exit_actions, context, event,
                           context_changed, error))
      return false;
    current = state.parent;
  }
  if (current != XFC_INDEX_NONE) {
    jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.runtime.complete");
    xfcTakeException(error);
    return false;
  }
  runtime->data.status = XFC_ACTOR_DONE;
  return true;
}

static bool xfcProcessCompletions(XfcRuntime *runtime, JsVar **context,
                                  JsVar *cause_event,
                                  bool *context_changed, uint16_t *leaf,
                                  bool *state_changed, JsVar **error) {
  JsVar *event = jsvLockAgain(cause_event);
  bool ok = true;
  while (ok) {
    XfcStateRecord final_state;
    XfcStateRecord completed_state;
    XfcTransitionRecord transition;
    JsVar *completion_event = 0;
    bool found = false;
    bool targeted;
    if (!xfcRefreshView(&runtime->view) ||
        !xfcReadState(&runtime->view, *leaf, &final_state)) {
      jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.runtime.complete");
      xfcTakeException(error);
      ok = false;
      break;
    }
    if ((final_state.flags & XFC_STATE_TYPE_MASK) != XFC_STATE_FINAL) break;
    if (final_state.parent == runtime->view.header.root_state) {
      ok = xfcCompleteActor(runtime, context, event, context_changed, *leaf,
                            error);
      break;
    }
    if (!xfcReadState(&runtime->view, final_state.parent, &completed_state)) {
      jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.runtime.complete");
      xfcTakeException(error);
      ok = false;
      break;
    }
    if (completed_state.completion_transitions.count == 0) break;
    completion_event = xfcInternalEventFromSymbol(
        &runtime->view, completed_state.completion_event_symbol);
    if (!completion_event) {
      xfcNoMemory("actor.runtime.complete.event");
      xfcTakeException(error);
      ok = false;
      break;
    }
    jsvUnLock(event);
    event = completion_event;
    if (!xfcSelectFromRange(runtime, completed_state.completion_transitions,
                            *context, event, &transition, &found, error)) {
      if (!*error) {
        jsExceptionHere(JSET_ERROR,
                        "XFC E_INTERNAL @ actor.runtime.complete.select");
        xfcTakeException(error);
      }
      ok = false;
      break;
    }
    if (!found) break;
    if (!xfcBeginMicrostep(runtime, error)) {
      ok = false;
      break;
    }
    targeted = transition.target_state != XFC_INDEX_NONE;
    if (!xfcExecuteTransition(runtime, &transition, context, event,
                              context_changed, leaf, state_changed, error)) {
      if (!*error) {
        jsExceptionHere(JSET_ERROR,
                        "XFC E_INTERNAL @ actor.runtime.complete.transition");
        xfcTakeException(error);
      }
      ok = false;
      break;
    }
    if (!targeted) break;
  }
  jsvUnLock(event);
  return ok;
}

static void xfcInvalidateSnapshot(JsVar *actor) {
  jsvObjectRemoveChild(actor, XFC_ACTOR_SNAPSHOT_NAME);
}

static void xfcDeactivateSubscription(JsVar *subscription) {
  if (!subscription) return;
  jsvObjectRemoveChild(subscription, XFC_SUBSCRIPTION_ACTOR_NAME);
  jsvObjectRemoveChild(subscription, XFC_SUBSCRIPTION_LISTENER_NAME);
  jsvObjectRemoveChild(subscription, XFC_SUBSCRIPTION_SEQUENCE_NAME);
}

static void xfcClearSubscriptions(JsVar *actor) {
  JsVar *subscriptions =
      jsvObjectGetChildIfExists(actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
  if (jsvIsArray(subscriptions)) {
    JsVarInt index;
    JsVarInt length = jsvGetArrayLength(subscriptions);
    for (index = 0; index < length; index++) {
      JsVar *subscription = jsvGetArrayItem(subscriptions, index);
      xfcDeactivateSubscription(subscription);
      jsvUnLock(subscription);
    }
  }
  jsvUnLock(subscriptions);
  jsvObjectRemoveChild(actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
  jsvObjectRemoveChild(actor, XFC_ACTOR_SUBSCRIPTION_SEQUENCE_NAME);
}

static void xfcFault(XfcRuntime *runtime, JsVar *storage, JsVar *error) {
  runtime->data.status = XFC_ACTOR_ERROR;
  runtime->data.operation = XFC_OPERATION_IDLE;
  runtime->data.microsteps = 0;
  xfcWriteActorData(storage, &runtime->data);
  if (error)
    jsvObjectSetChild(runtime->actor, XFC_ACTOR_ERROR_NAME,
                      xfcIsUndefinedException(error) ? 0 : error);
  xfcInvalidateSnapshot(runtime->actor);
  xfcClearSubscriptions(runtime->actor);
}

static const char *xfcStatusName(uint8_t status) {
  static const char *const names[] = {"notStarted", "active", "done",
                                      "stopped", "error"};
  return status <= XFC_ACTOR_ERROR ? names[status] : "error";
}

static JsVar *xfcStateValue(XfcRuntimeView *view, uint16_t leaf) {
  uint16_t chain[XFC_MAX_STATE_DEPTH + 1];
  uint16_t count = 0;
  uint16_t current = leaf;
  XfcStateRecord state;
  JsVar *value = 0;
  if (leaf == XFC_INDEX_NONE) return xfcUndefined();
  if (!xfcReadState(view, leaf, &state)) return 0;
  if ((state.flags & XFC_STATE_ROOT) != 0) return jsvNewObject();
  while (current != view->header.root_state &&
         count <= XFC_MAX_STATE_DEPTH) {
    if (!xfcReadState(view, current, &state)) return 0;
    chain[count++] = current;
    current = state.parent;
  }
  if (current != view->header.root_state || count == 0) return 0;
  if (!xfcReadState(view, chain[0], &state)) return 0;
  value = xfcSymbolString(view, state.key_symbol);
  {
    uint16_t ancestor;
    for (ancestor = 1; value && ancestor < count; ancestor++) {
      JsVar *container;
      JsVar *key;
      if (!xfcReadState(view, chain[ancestor], &state)) {
        jsvUnLock(value);
        return 0;
      }
      key = xfcSymbolString(view, state.key_symbol);
      container = jsvNewObject();
      if (!key || !container ||
          jsvObjectSetChildVar(container, key, value) != value) {
        jsvUnLock3(key, container, value);
        return 0;
      }
      jsvUnLock2(key, value);
      value = container;
    }
  }
  return value;
}

static JsVar *xfcCreateSnapshot(XfcRuntime *runtime, JsVar *context) {
  JsVar *snapshot = 0;
  JsVar *value = 0;
  JsVar *status = 0;
  JsVar *leaf = 0;
  JsVar *error = 0;
  bool ok = false;
  snapshot = jsvNewObject();
  if (!snapshot) return 0;
  value = xfcStateValue(&runtime->view, runtime->data.leaf_state);
  if (runtime->data.leaf_state != XFC_INDEX_NONE && !value) goto done;
  status = jsvNewFromString(xfcStatusName(runtime->data.status));
  leaf = jsvNewFromInteger(runtime->data.leaf_state);
  ok = status && leaf &&
       xfcSetBrand(snapshot, XFC_SNAPSHOT_BRAND_NAME,
                   XFC_ROOT_SNAPSHOT_TOKEN) &&
       xfcAttachPrototype(snapshot, XFC_ROOT_SNAPSHOT_PROTOTYPE) &&
       jsvObjectSetChild(snapshot, "status", status) == status &&
       jsvObjectSetChild(snapshot, XFC_SNAPSHOT_MACHINE_NAME,
                         runtime->machine) == runtime->machine &&
       jsvObjectSetChild(snapshot, XFC_SNAPSHOT_LEAF_NAME, leaf) == leaf;
  if (ok) {
    ok = xfcSetProperty(snapshot, "value", value) &&
         xfcSetProperty(snapshot, "context", context);
  }
  if (ok && runtime->data.status == XFC_ACTOR_ERROR) {
    error = jsvObjectGetChildIfExists(runtime->actor, XFC_ACTOR_ERROR_NAME);
    ok = xfcSetProperty(snapshot, "error", error);
  }
done:
  jsvUnLockMany(4, (JsVar *[]){value, status, leaf, error});
  if (!ok) {
    jsvUnLock(snapshot);
    return 0;
  }
  return snapshot;
}

static JsVar *xfcMaterializeSnapshot(XfcRuntime *runtime) {
  JsVar *snapshot =
      jsvObjectGetChildIfExists(runtime->actor, XFC_ACTOR_SNAPSHOT_NAME);
  JsVar *context = 0;
  if (snapshot) return snapshot;
  if (xfcTestTakeFault(XFC_TEST_FAULT_GET_SNAPSHOT)) return 0;
  context = jsvObjectGetChildIfExists(runtime->actor, XFC_ACTOR_CONTEXT_NAME);
  snapshot = xfcCreateSnapshot(runtime, context);
  jsvUnLock(context);
  if (snapshot &&
      jsvObjectSetChild(runtime->actor, XFC_ACTOR_SNAPSHOT_NAME, snapshot) !=
          snapshot) {
    jsvUnLock(snapshot);
    snapshot = 0;
  }
  return snapshot;
}

static bool xfcPrepareNotification(XfcRuntime *runtime, JsVar *context,
                                   bool changed, JsVar **snapshot,
                                   bool *cache_snapshot, JsVar **error) {
  JsVar *subscriptions = jsvObjectGetChildIfExists(
      runtime->actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
  bool required = jsvIsArray(subscriptions) &&
                  jsvGetArrayLength(subscriptions) != 0;
  jsvUnLock(subscriptions);
  *snapshot = 0;
  *cache_snapshot = false;
  if (!required) return true;
  if (!changed)
    *snapshot =
        jsvObjectGetChildIfExists(runtime->actor, XFC_ACTOR_SNAPSHOT_NAME);
  if (*snapshot) return true;
  if (!xfcTestTakeFault(XFC_TEST_FAULT_NOTIFY_SNAPSHOT))
    *snapshot = xfcCreateSnapshot(runtime, context);
  if (*snapshot) {
    *cache_snapshot = true;
    return true;
  }
  xfcNoMemory("actor.notify.snapshot");
  xfcTakeException(error);
  return false;
}

static bool xfcCommitPublication(XfcRuntime *runtime, JsVar *storage,
                                 JsVar *context, bool publish_context,
                                 bool changed, JsVar *snapshot,
                                 bool cache_snapshot, JsVar **error) {
  if (cache_snapshot &&
      jsvObjectSetChild(runtime->actor, XFC_ACTOR_SNAPSHOT_NAME, snapshot) !=
          snapshot) {
    xfcNoMemory("actor.notify.snapshot");
    xfcTakeException(error);
    return false;
  }
  if (publish_context &&
      jsvObjectSetChild(runtime->actor, XFC_ACTOR_CONTEXT_NAME, context) !=
          context) {
    xfcNoMemory("actor.runtime.context");
    xfcTakeException(error);
    return false;
  }
  xfcWriteActorData(storage, &runtime->data);
  if (changed && !cache_snapshot) xfcInvalidateSnapshot(runtime->actor);
  return true;
}

static JsVar *xfcNotify(XfcRuntime *runtime, JsVar *storage,
                        JsVar *snapshot) {
  xfcMeasureStackSample();
  JsVar *subscriptions = jsvObjectGetChildIfExists(
      runtime->actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
  JsVar *first_error = 0;
  JsVarInt length;
  JsVarInt publication_sequence;
  JsVarInt index;
  if (!jsvIsArray(subscriptions) ||
      (length = jsvGetArrayLength(subscriptions)) == 0) {
    jsvUnLock(subscriptions);
    return 0;
  }
  if (!snapshot) {
    jsvUnLock(subscriptions);
    jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.notify.snapshot");
    xfcTakeException(&first_error);
    return first_error;
  }
  runtime->data.operation = XFC_OPERATION_NOTIFY;
  xfcWriteActorData(storage, &runtime->data);
  publication_sequence = jsvObjectGetIntegerChildOr(
      runtime->actor, XFC_ACTOR_SUBSCRIPTION_SEQUENCE_NAME, 0);
  for (index = 0; index < length; index++) {
    JsVar *subscription = jsvGetArrayItem(subscriptions, index);
    JsVarInt sequence = subscription
                            ? jsvObjectGetIntegerChildOr(
                                  subscription,
                                  XFC_SUBSCRIPTION_SEQUENCE_NAME, 0)
                            : 0;
    JsVar *listener = subscription && sequence <= publication_sequence
                          ? jsvObjectGetChildIfExists(
                                subscription, XFC_SUBSCRIPTION_LISTENER_NAME)
                          : 0;
    if (jsvIsFunction(listener)) {
      JsVar *result = jspExecuteFunction(listener, 0, 1, &snapshot);
      JsVar *error = 0;
      jsvUnLock(result);
      if (xfcTakeException(&error)) {
        if (!first_error)
          first_error = error;
        else
          jsvUnLock(error);
      }
    }
    jsvUnLock2(listener, subscription);
  }
  runtime->data.operation = XFC_OPERATION_IDLE;
  runtime->data.microsteps = 0;
  xfcWriteActorData(storage, &runtime->data);
  jsvUnLock(subscriptions);
  return first_error;
}

static bool xfcEventTypeReserved(JsVar *type, size_t length) {
  char prefix[8];
  size_t count = length < sizeof(prefix) ? length : sizeof(prefix);
  memset(prefix, 0, sizeof(prefix));
  jsvGetStringChars(type, 0, prefix, count);
  return (length >= 7 && memcmp(prefix, "xstate.", 7) == 0) ||
         (length >= 8 && memcmp(prefix, "@xstate.", 8) == 0);
}

static bool xfcPrepareEvent(JsVar *input, JsVar **event, JsVar **type,
                            bool *allocation_failure) {
  size_t type_length = 0;
  *event = 0;
  *type = 0;
  *allocation_failure = false;
  if (jsvIsString(input)) {
    *type = jsvLockAgain(input);
    *event = xfcTestTakeFault(XFC_TEST_FAULT_SEND_EVENT) ? 0 : jsvNewObject();
    if (*event &&
        jsvObjectSetChild(*event, "type", *type) != *type) {
      jsvUnLock(*event);
      *event = 0;
    }
  } else if (xfcIsPlainObject(input)) {
    *event = jsvLockAgain(input);
    *type = jspGetNamedField(input, "type", false);
  }
  if ((execInfo.execute & EXEC_EXCEPTION) != 0) return false;
  if (jsvIsString(input) && !*event) {
    jsvUnLock(*type);
    *type = 0;
    *allocation_failure = true;
    xfcNoMemory("actor.send.event");
    return false;
  }
  if (*event && jsvIsString(*type))
    type_length = xfcStringByteLength(*type);
  if (!*event || !jsvIsString(*type) || type_length == 0 ||
      xfcEventTypeReserved(*type, type_length)) {
    jsvUnLock2(*event, *type);
    *event = 0;
    *type = 0;
    jsExceptionHere(JSET_TYPEERROR, "XFC E_EVENT_INVALID @ actor.send.event");
    return false;
  }
  if (type_length > UINT16_MAX) {
    jsvUnLock2(*event, *type);
    *event = 0;
    *type = 0;
    jsExceptionHere(JSET_ERROR,
                    "XFC E_LIMIT_EXCEEDED @ actor.send.event.type");
    return false;
  }
  return true;
}

JsVar *xfcCreateActor(JsVar *machine, JsVar *options) {
  XfcRuntimeView view;
  JsVar *actor = 0;
  JsVar *storage = 0;
  XfcActorData data;
  bool ok = false;
  if (options && !jsvIsUndefined(options)) {
    jsExceptionHere(JSET_ERROR,
                    "XFC E_UNSUPPORTED_FEATURE @ createActor.options");
    return 0;
  }
  if (!xfcOpenMachine(machine, &view, true)) {
    jsExceptionHere(JSET_TYPEERROR,
                    "XFC E_MACHINE_INVALID @ createActor.machine");
    return 0;
  }
  if (!xfcTestTakeFault(XFC_TEST_FAULT_CREATE_ACTOR)) {
    actor = jsvNewObject();
    storage = jsvNewFlatStringOfLength(XFC_ACTOR_DATA_SIZE);
  }
  if (!actor || !storage) goto done;
  xfcActorDataInit(&data);
  memcpy(jsvGetFlatStringPointer(storage), &data, sizeof(data));
  ok = xfcSetBrand(actor, XFC_ACTOR_BRAND_NAME, XFC_ROOT_ACTOR_TOKEN) &&
       xfcAttachPrototype(actor, XFC_ROOT_ACTOR_PROTOTYPE) &&
       jsvObjectSetChild(actor, XFC_ACTOR_MACHINE_NAME, machine) == machine &&
       jsvObjectSetChild(actor, XFC_ACTOR_DATA_NAME, storage) == storage;
done:
  xfcCloseView(&view);
  jsvUnLock(storage);
  if (!ok) {
    jsvUnLock(actor);
    actor = 0;
    xfcNoMemory("createActor");
  }
  return actor;
}

static JsVar *xfcActorStart(JsVar *actor) {
  XfcRuntime runtime;
  JsVar *storage = 0;
  JsVar *context = 0;
  JsVar *event = 0;
  JsVar *error = 0;
  JsVar *listener_error = 0;
  JsVar *publication_snapshot = 0;
  bool context_changed = false;
  bool cache_snapshot = false;
  uint16_t leaf = XFC_INDEX_NONE;
  if (!xfcOpenActor(actor, "start", &runtime, &storage)) return 0;
  if (!xfcRequireIdle(&runtime, "start")) goto fail;
  if (runtime.data.status == XFC_ACTOR_ACTIVE) goto success;
  if (runtime.data.status == XFC_ACTOR_ERROR) {
    jsExceptionHere(JSET_ERROR, "XFC E_ACTOR_FAULTED @ actor.start");
    goto fail;
  }
  if (runtime.data.status != XFC_ACTOR_NOT_STARTED) {
    jsExceptionHere(JSET_ERROR, "XFC E_ACTOR_STATE @ actor.start");
    goto fail;
  }
  if (!xfcBeginOperation(&runtime, storage, XFC_OPERATION_START, "start"))
    goto fail;
  if (runtime.view.header.context_kind == XFC_CONTEXT_OMITTED) {
    context = xfcTestTakeFault(XFC_TEST_FAULT_START_CONTEXT)
                  ? 0
                  : jsvNewObject();
    if (!context) {
      xfcNoMemory("actor.start.context");
      xfcTakeException(&error);
    }
  } else if (runtime.view.header.context_kind == XFC_CONTEXT_LITERAL) {
    context = xfcRetained(&runtime.view, runtime.view.header.context_slot);
  } else {
    JsVar *factory = xfcRetained(&runtime.view,
                                 runtime.view.header.context_slot);
    if (factory) xfcCall(factory, 0, 0, &context, &error);
    jsvUnLock(factory);
  }
  if (error) goto fault;
  if (!xfcIsPlainObject(context)) {
    jsExceptionHere(JSET_TYPEERROR,
                    "XFC E_CONTEXT_INVALID @ actor.start.context");
    xfcTakeException(&error);
    goto fault;
  }
  event = xfcTestTakeFault(XFC_TEST_FAULT_START_EVENT)
              ? 0
              : xfcInternalEvent("xstate.init");
  if (!event) {
    xfcNoMemory("actor.start.event");
    xfcTakeException(&error);
    goto fault;
  }
  if (!xfcBeginMicrostep(&runtime, &error)) goto fault;
  if (!xfcInitialDescent(&runtime, runtime.view.header.root_state, true, true,
                         &context, event, &context_changed, &leaf, &error))
    goto fault;
  runtime.data.status = XFC_ACTOR_ACTIVE;
  {
    bool state_changed = true;
    if (!xfcProcessCompletions(&runtime, &context, event, &context_changed,
                               &leaf, &state_changed, &error))
      goto fault;
  }
  runtime.data.operation = XFC_OPERATION_IDLE;
  runtime.data.leaf_state = leaf;
  runtime.data.microsteps = 0;
  if (!xfcPrepareNotification(&runtime, context, true,
                              &publication_snapshot, &cache_snapshot,
                              &error) ||
      !xfcCommitPublication(&runtime, storage, context, true, true,
                            publication_snapshot, cache_snapshot, &error))
    goto fault;
  listener_error = xfcNotify(&runtime, storage, publication_snapshot);
  if (runtime.data.status == XFC_ACTOR_DONE) xfcClearSubscriptions(actor);
  if (listener_error) xfcRaise(listener_error);
  goto success;
fault:
  runtime.data.leaf_state = XFC_INDEX_NONE;
  xfcFault(&runtime, storage, error);
  xfcRaise(error);
fail:
  xfcMeasureOperationEnd();
  jsvUnLockMany(5, (JsVar *[]){context, event, error, listener_error,
                               publication_snapshot});
  xfcCloseActor(&runtime, storage);
  return 0;
success:
  xfcMeasureOperationEnd();
  jsvUnLockMany(5, (JsVar *[]){context, event, error, listener_error,
                               publication_snapshot});
  xfcCloseActor(&runtime, storage);
  return jsvLockAgain(actor);
}

static void xfcActorSend(JsVar *actor, JsVar *input) {
  XfcRuntime runtime;
  JsVar *storage = 0;
  JsVar *event = 0;
  JsVar *event_type = 0;
  JsVar *context = 0;
  JsVar *error = 0;
  JsVar *listener_error = 0;
  JsVar *publication_snapshot = 0;
  XfcTransitionRecord transition;
  uint32_t event_hash;
  bool found = false;
  bool context_changed = false;
  bool state_changed = false;
  bool cache_snapshot = false;
  bool allocation_failure = false;
  uint8_t initial_status;
  uint16_t stable_leaf;
  uint16_t leaf;
  if (!xfcOpenActor(actor, "send", &runtime, &storage)) return;
  initial_status = runtime.data.status;
  stable_leaf = runtime.data.leaf_state;
  if (!xfcRequireIdle(&runtime, "send")) goto done;
  if (runtime.data.status == XFC_ACTOR_ERROR) {
    jsExceptionHere(JSET_ERROR, "XFC E_ACTOR_FAULTED @ actor.send");
    goto done;
  }
  if (runtime.data.status == XFC_ACTOR_DONE ||
      runtime.data.status == XFC_ACTOR_STOPPED)
    goto done;
  if (runtime.data.status != XFC_ACTOR_ACTIVE) {
    jsExceptionHere(JSET_ERROR,
                    "XFC E_ACTOR_STATE @ actor.send: status=notStarted");
    goto done;
  }
  if (!xfcBeginOperation(&runtime, storage, XFC_OPERATION_SEND, "send"))
    goto done;
  if (!xfcPrepareEvent(input, &event, &event_type, &allocation_failure)) {
    if (allocation_failure) {
      xfcTakeException(&error);
      goto fault;
    }
    runtime.data.operation = XFC_OPERATION_IDLE;
    xfcWriteActorData(storage, &runtime.data);
    goto done;
  }
  event_hash = xfcStringHash(event_type);
  context = jsvObjectGetChildIfExists(actor, XFC_ACTOR_CONTEXT_NAME);
  leaf = runtime.data.leaf_state;
  if (!xfcSelectTransition(&runtime, event_type, event_hash, context, event,
                           &transition, &found, &error)) {
    if (!error) {
      jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.send.select");
      xfcTakeException(&error);
    }
    goto fault;
  }
  if (found) {
    bool targeted = transition.target_state != XFC_INDEX_NONE;
    if (!xfcBeginMicrostep(&runtime, &error)) goto fault;
    if (!xfcExecuteTransition(&runtime, &transition, &context, event,
                              &context_changed, &leaf, &state_changed,
                              &error)) {
      if (!error) {
        jsExceptionHere(JSET_ERROR, "XFC E_INTERNAL @ actor.send.transition");
        xfcTakeException(&error);
      }
      goto fault;
    }
    if (targeted &&
        !xfcProcessCompletions(&runtime, &context, event, &context_changed,
                               &leaf, &state_changed, &error))
      goto fault;
  }
  runtime.data.operation = XFC_OPERATION_IDLE;
  runtime.data.microsteps = 0;
  runtime.data.leaf_state = leaf;
  {
    bool publication_changed = context_changed || state_changed ||
                               runtime.data.status != initial_status;
    if (!xfcPrepareNotification(&runtime, context, publication_changed,
                                &publication_snapshot, &cache_snapshot,
                                &error) ||
        !xfcCommitPublication(&runtime, storage, context, context_changed,
                              publication_changed, publication_snapshot,
                              cache_snapshot, &error))
      goto fault;
  }
  listener_error = xfcNotify(&runtime, storage, publication_snapshot);
  if (runtime.data.status == XFC_ACTOR_DONE) xfcClearSubscriptions(actor);
  if (listener_error) xfcRaise(listener_error);
  goto done;
fault:
  runtime.data.leaf_state = stable_leaf;
  xfcFault(&runtime, storage, error);
  xfcRaise(error);
done:
  xfcMeasureOperationEnd();
  jsvUnLockMany(7, (JsVar *[]){event, event_type, context, error,
                               listener_error, publication_snapshot,
                               storage});
  xfcCloseView(&runtime.view);
  jsvUnLock(runtime.machine);
}

static JsVar *xfcActorStop(JsVar *actor) {
  XfcRuntime runtime;
  JsVar *storage = 0;
  JsVar *context = 0;
  JsVar *event = 0;
  JsVar *error = 0;
  JsVar *listener_error = 0;
  JsVar *publication_snapshot = 0;
  bool context_changed = false;
  bool cache_snapshot = false;
  if (!xfcOpenActor(actor, "stop", &runtime, &storage)) return 0;
  if (!xfcRequireIdle(&runtime, "stop")) goto fail;
  if (runtime.data.status == XFC_ACTOR_ERROR) {
    jsExceptionHere(JSET_ERROR, "XFC E_ACTOR_FAULTED @ actor.stop");
    goto fail;
  }
  if (runtime.data.status == XFC_ACTOR_DONE ||
      runtime.data.status == XFC_ACTOR_STOPPED)
    goto success;
  if (!xfcBeginOperation(&runtime, storage, XFC_OPERATION_STOP, "stop"))
    goto fail;
  if (runtime.data.status == XFC_ACTOR_ACTIVE) {
    uint16_t current = runtime.data.leaf_state;
    XfcStateRecord state;
    context = jsvObjectGetChildIfExists(actor, XFC_ACTOR_CONTEXT_NAME);
    event = xfcTestTakeFault(XFC_TEST_FAULT_STOP_EVENT)
                ? 0
                : xfcInternalEvent("xstate.stop");
    if (!event) {
      xfcNoMemory("actor.stop.event");
      xfcTakeException(&error);
      goto fault;
    }
    while (current != XFC_INDEX_NONE) {
      if (!xfcRefreshView(&runtime.view) ||
          !xfcReadState(&runtime.view, current, &state) ||
          !xfcExecuteActions(&runtime, state.exit_actions, &context, event,
                             &context_changed, &error))
        goto fault;
      current = state.parent;
    }
  }
  runtime.data.status = XFC_ACTOR_STOPPED;
  runtime.data.operation = XFC_OPERATION_IDLE;
  runtime.data.microsteps = 0;
  if (!xfcPrepareNotification(&runtime, context, true,
                              &publication_snapshot, &cache_snapshot,
                              &error) ||
      !xfcCommitPublication(&runtime, storage, context, context_changed, true,
                            publication_snapshot, cache_snapshot, &error))
    goto fault;
  listener_error = xfcNotify(&runtime, storage, publication_snapshot);
  xfcClearSubscriptions(actor);
  if (listener_error) xfcRaise(listener_error);
  goto success;
fault:
  xfcFault(&runtime, storage, error);
  xfcRaise(error);
fail:
  xfcMeasureOperationEnd();
  jsvUnLockMany(6, (JsVar *[]){context, event, error, listener_error,
                               publication_snapshot, storage});
  xfcCloseView(&runtime.view);
  jsvUnLock(runtime.machine);
  return 0;
success:
  xfcMeasureOperationEnd();
  jsvUnLockMany(6, (JsVar *[]){context, event, error, listener_error,
                               publication_snapshot, storage});
  xfcCloseView(&runtime.view);
  jsvUnLock(runtime.machine);
  return jsvLockAgain(actor);
}

static JsVar *xfcActorGetSnapshot(JsVar *actor) {
  XfcRuntime runtime;
  JsVar *storage = 0;
  JsVar *snapshot;
  if (!xfcOpenActor(actor, "getSnapshot", &runtime, &storage)) return 0;
  snapshot = xfcMaterializeSnapshot(&runtime);
  if (!snapshot) xfcNoMemory("actor.getSnapshot");
  xfcCloseActor(&runtime, storage);
  return snapshot;
}

static JsVar *xfcActorSubscribe(JsVar *actor, JsVar *arguments) {
  XfcRuntime runtime;
  JsVar *storage = 0;
  JsVar *listener = 0;
  JsVar *subscription = 0;
  JsVar *subscriptions = 0;
  bool active;
  bool ok;
  if (!xfcOpenActor(actor, "subscribe", &runtime, &storage)) return 0;
  if (jsvIsArray(arguments) && jsvGetArrayLength(arguments) == 1)
    listener = jsvGetArrayItem(arguments, 0);
  if (!jsvIsFunction(listener)) {
    jsExceptionHere(JSET_TYPEERROR,
                    "XFC E_LISTENER_INVALID @ actor.subscribe.listener");
    jsvUnLock(listener);
    xfcCloseActor(&runtime, storage);
    return 0;
  }
  subscription = xfcTestTakeFault(XFC_TEST_FAULT_SUBSCRIBE)
                     ? 0
                     : jsvNewObject();
  active = runtime.data.status == XFC_ACTOR_NOT_STARTED ||
           runtime.data.status == XFC_ACTOR_ACTIVE;
  ok = subscription &&
       xfcSetBrand(subscription, XFC_SUBSCRIPTION_BRAND_NAME,
                   XFC_ROOT_SUBSCRIPTION_TOKEN) &&
       xfcAttachPrototype(subscription, XFC_ROOT_SUBSCRIPTION_PROTOTYPE);
  if (ok && active) {
    JsVarInt sequence = jsvObjectGetIntegerChildOr(
                            actor, XFC_ACTOR_SUBSCRIPTION_SEQUENCE_NAME, 0) +
                        1;
    subscriptions = jsvObjectGetChildIfExists(
        actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
    if (!subscriptions) {
      subscriptions = jsvNewEmptyArray();
      if (subscriptions)
        jsvObjectSetChild(actor, XFC_ACTOR_SUBSCRIPTIONS_NAME, subscriptions);
    }
    ok = subscriptions &&
         jsvObjectSetChild(subscription, XFC_SUBSCRIPTION_ACTOR_NAME, actor) ==
             actor &&
         jsvObjectSetChild(subscription, XFC_SUBSCRIPTION_LISTENER_NAME,
                           listener) == listener;
    if (ok) {
      jsvObjectSetIntChild(actor, XFC_ACTOR_SUBSCRIPTION_SEQUENCE_NAME,
                           sequence);
      jsvObjectSetIntChild(subscription, XFC_SUBSCRIPTION_SEQUENCE_NAME,
                           sequence);
      JsVarInt before = jsvGetArrayLength(subscriptions);
      jsvArrayPush(subscriptions, subscription);
      ok = jsvGetArrayLength(subscriptions) == before + 1;
    }
  }
  jsvUnLock(subscriptions);
  jsvUnLock(listener);
  xfcCloseActor(&runtime, storage);
  if (!ok) {
    jsvUnLock(subscription);
    xfcNoMemory("actor.subscribe");
    return 0;
  }
  return subscription;
}

static void xfcSubscriptionUnsubscribe(JsVar *subscription) {
  JsVar *actor = 0;
  JsVar *subscriptions = 0;
  if (!xfcHasBrand(subscription, XFC_SUBSCRIPTION_BRAND_NAME,
                   XFC_ROOT_SUBSCRIPTION_TOKEN)) {
    xfcReceiverInvalid("subscription.unsubscribe");
    return;
  }
  actor = jsvObjectGetChildIfExists(subscription,
                                    XFC_SUBSCRIPTION_ACTOR_NAME);
  if (actor) {
    subscriptions = jsvObjectGetChildIfExists(
        actor, XFC_ACTOR_SUBSCRIPTIONS_NAME);
    if (jsvIsArray(subscriptions)) {
      JsVarInt index;
      JsVarInt length = jsvGetArrayLength(subscriptions);
      for (index = 0; index < length; index++) {
        JsVar *candidate = jsvGetArrayItem(subscriptions, index);
        bool same = candidate == subscription;
        jsvUnLock(candidate);
        if (same) {
          jsvRemoveArrayItem(subscriptions, index);
          break;
        }
      }
    }
    xfcDeactivateSubscription(subscription);
  }
  jsvUnLock2(subscriptions, actor);
}

static bool xfcSingleVisibleProperty(JsVar *object, JsVar **key,
                                     JsVar **value) {
  JsvObjectIterator iterator;
  unsigned int count = 0;
  bool valid = true;
  *key = 0;
  *value = 0;
  jsvObjectIteratorNew(&iterator, object);
  while (valid && jsvObjectIteratorHasValue(&iterator)) {
    JsVar *candidate = jsvObjectIteratorGetKey(&iterator);
    if (!jsvIsInternalObjectKey(candidate)) {
      count++;
      if (count != 1 || jsvIsGetterOrSetter(candidate)) {
        valid = false;
      } else {
        *key = jsvLockAgain(candidate);
        *value = jsvObjectIteratorGetValue(&iterator);
      }
    }
    jsvUnLock(candidate);
    jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  if (!valid || count != 1) {
    jsvUnLock2(*key, *value);
    *key = 0;
    *value = 0;
    return false;
  }
  return true;
}

static bool xfcSnapshotMatches(JsVar *snapshot, JsVar *value) {
  JsVar *machine = 0;
  JsVar *leaf_value = 0;
  XfcRuntimeView view;
  uint16_t chain[XFC_MAX_STATE_DEPTH + 1];
  uint16_t count = 0;
  uint16_t current;
  uint16_t position = 0;
  XfcStateRecord state;
  JsVar *match_value = 0;
  bool matches = false;
  if (!xfcHasBrand(snapshot, XFC_SNAPSHOT_BRAND_NAME,
                   XFC_ROOT_SNAPSHOT_TOKEN)) {
    xfcReceiverInvalid("snapshot.matches");
    return false;
  }
  machine = jsvObjectGetChildIfExists(snapshot, XFC_SNAPSHOT_MACHINE_NAME);
  leaf_value = jsvObjectGetChildIfExists(snapshot, XFC_SNAPSHOT_LEAF_NAME);
  if (!machine || !jsvIsInt(leaf_value) ||
      !xfcOpenMachine(machine, &view, false))
    goto done;
  current = (uint16_t)jsvGetInteger(leaf_value);
  if (current == XFC_INDEX_NONE) goto close;
  if (!xfcReadState(&view, current, &state)) goto close;
  if ((state.flags & XFC_STATE_ROOT) != 0) {
    matches = xfcIsPlainObject(value) && jsvGetChildren(value) == 0;
    goto close;
  }
  while (current != view.header.root_state && count <= XFC_MAX_STATE_DEPTH) {
    if (!xfcReadState(&view, current, &state)) goto close;
    chain[count++] = current;
    current = state.parent;
  }
  if (current != view.header.root_state || count == 0) goto close;
  match_value = jsvLockAgainSafe(value);
  while (position < count) {
    uint16_t expected = chain[count - position - 1];
    if (!xfcReadState(&view, expected, &state)) goto close;
    if (jsvIsString(match_value)) {
      matches = jsvGetStringLength(match_value) != 0 &&
                xfcSymbolEquals(&view, state.key_symbol, match_value);
      goto close;
    }
    if (xfcIsPlainObject(match_value)) {
      JsVar *key = 0;
      JsVar *child = 0;
      if (!xfcSingleVisibleProperty(match_value, &key, &child) ||
          !xfcSymbolEquals(&view, state.key_symbol, key)) {
        jsvUnLock2(key, child);
        goto close;
      }
      jsvUnLock(key);
      jsvUnLock(match_value);
      match_value = child;
      position++;
      if (position == count) goto close;
      continue;
    }
    goto close;
  }
close:
  jsvUnLock(match_value);
  xfcCloseView(&view);
done:
  jsvUnLock2(machine, leaf_value);
  return matches;
}
