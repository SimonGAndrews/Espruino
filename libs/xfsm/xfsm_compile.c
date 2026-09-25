/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "xfsm_compile.h"

#include "jsparse.h"
#include "jsutils.h"
#include "jsvariterator.h"
#include "xfsm_native.h"

#include <limits.h>
#include <string.h>

#define XFC_MACHINE_ARENA_NAME JS_HIDDEN_CHAR_STR "xfcA"
#define XFC_MACHINE_RETAINED_NAME JS_HIDDEN_CHAR_STR "xfcR"
#define XFC_ASSIGN_BRAND_NAME JS_HIDDEN_CHAR_STR "xfcB"
#define XFC_ASSIGN_VALUE_NAME JS_HIDDEN_CHAR_STR "xfcV"

#define XFC_ASSIGN_BRAND "XFAD1"
#define XFC_ASSIGN_BRAND_LENGTH 5

enum {
  XFC_META_CONFIG = 0,
  XFC_META_KEY,
  XFC_META_PARENT,
  XFC_META_DEPTH,
  XFC_META_PATH,
  XFC_META_EFFECTIVE_ID,
  XFC_META_IMPLICIT_ID,
  XFC_META_TYPE,
  XFC_META_COMPLETION_SYMBOL,
  XFC_META_FIELD_COUNT
};

enum {
  XFC_COMPILE_COUNT = 0,
  XFC_COMPILE_EMIT = 1
};

typedef enum {
  XFC_NODE_ATOMIC = 0,
  XFC_NODE_COMPOUND = 1
} XfcNodeType;

typedef enum {
  XFC_DIAG_NONE = 0,
  XFC_DIAG_CONFIG_TYPE,
  XFC_DIAG_UNKNOWN_PROPERTY,
  XFC_DIAG_UNSUPPORTED_FEATURE,
  XFC_DIAG_INITIAL_REQUIRED,
  XFC_DIAG_INITIAL_UNKNOWN,
  XFC_DIAG_TARGET_UNKNOWN,
  XFC_DIAG_TARGET_AMBIGUOUS,
  XFC_DIAG_ID_DUPLICATE,
  XFC_DIAG_ACTION_UNRESOLVED,
  XFC_DIAG_GUARD_UNRESOLVED,
  XFC_DIAG_LIMIT_EXCEEDED,
  XFC_DIAG_NO_MEMORY,
  XFC_DIAG_INTERNAL
} XfcDiagnostic;

typedef struct {
  uint32_t states;
  uint32_t symbols;
  uint32_t handlers;
  uint32_t transitions;
  uint32_t guards;
  uint32_t actions;
  uint32_t assignments;
  uint32_t assignment_entries;
  uint32_t string_bytes;
} XfcCompileCounts;

typedef struct {
  uint8_t *bytes;
  XfcArenaHeader header;
  uint16_t handler;
  uint16_t transition;
  uint16_t guard;
  uint16_t action;
  uint16_t assignment;
  uint16_t assignment_entry;
} XfcArenaWriter;

typedef struct {
  int phase;
  XfcCompileCounts counts;
  XfcArenaWriter *writer;
  JsVar *states;
  JsVar *symbols;
  JsVar *retained;
  JsVar *actions_map;
  JsVar *guards_map;
  uint8_t context_kind;
  uint16_t context_slot;
  XfcDiagnostic diagnostic;
  JsVar *error_path;
  JsVar *error_detail;
} XfcCompiler;

static const char *const xfcDiagnosticNames[] = {
    "",                        "E_CONFIG_TYPE",
    "E_UNKNOWN_PROPERTY",      "E_UNSUPPORTED_FEATURE",
    "E_INITIAL_REQUIRED",      "E_INITIAL_UNKNOWN",
    "E_TARGET_UNKNOWN",        "E_TARGET_AMBIGUOUS",
    "E_ID_DUPLICATE",          "E_ACTION_UNRESOLVED",
    "E_GUARD_UNRESOLVED",      "E_LIMIT_EXCEEDED",
    "E_NO_MEMORY",             "E_INTERNAL"};

static bool xfcIsObject(const JsVar *value) {
  return value && jsvIsObject(value) && !jsvIsArray(value) &&
         !jsvIsFunction(value);
}

static bool xfcGetOwn(JsVar *object, const char *name, JsVar **value) {
  JsVar *property;
  if (value) *value = 0;
  if (!object) return false;
  property = jsvFindChildFromString(object, name);
  if (!property) return false;
  if (value) *value = jsvSkipNameAndUnLock(property);
  else jsvUnLock(property);
  return true;
}

static bool xfcArrayAppend(JsVar *array, JsVar *value) {
  JsVarInt before;
  if (!array || !value) return false;
  before = jsvGetArrayLength(array);
  jsvArrayPush(array, value);
  return jsvGetArrayLength(array) == before + 1;
}

static JsVar *xfcPathProperty(JsVar *base, const char *property) {
  JsVar *path = jsvNewFromStringVarComplete(base);
  if (!path) return 0;
  jsvAppendCharacter(path, '.');
  jsvAppendString(path, property);
  return path;
}

static bool xfcIsIdentifierKey(JsVar *key) {
  JsvStringIterator iterator;
  bool first = true;
  bool valid = jsvGetStringLength(key) != 0;
  jsvStringIteratorNew(&iterator, key, 0);
  while (valid && jsvStringIteratorHasChar(&iterator)) {
    unsigned char ch = (unsigned char)jsvStringIteratorGetChar(&iterator);
    if (first) {
      valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              ch == '_' || ch == '$';
      first = false;
    } else {
      valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
    }
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return valid;
}

static JsVar *xfcPathKey(JsVar *base, JsVar *key) {
  JsVar *path = jsvNewFromStringVarComplete(base);
  JsvStringIterator iterator;
  if (!path) return 0;
  if (xfcIsIdentifierKey(key)) {
    jsvAppendCharacter(path, '.');
    jsvAppendStringVarComplete(path, key);
    return path;
  }
  jsvAppendString(path, "[\"");
  jsvStringIteratorNew(&iterator, key, 0);
  while (jsvStringIteratorHasChar(&iterator)) {
    unsigned char ch = (unsigned char)jsvStringIteratorGetChar(&iterator);
    if (ch == '\\' || ch == '"') {
      jsvAppendCharacter(path, '\\');
      jsvAppendCharacter(path, (char)ch);
    } else if (ch >= 0x20 && ch != 0x7F) {
      jsvAppendCharacter(path, (char)ch);
    } else {
      static const char hex[] = "0123456789ABCDEF";
      jsvAppendString(path, "\\x");
      jsvAppendCharacter(path, hex[ch >> 4]);
      jsvAppendCharacter(path, hex[ch & 15]);
    }
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  jsvAppendString(path, "\"]");
  return path;
}

static JsVar *xfcPathIndex(JsVar *base, JsVarInt index) {
  JsVar *path = jsvNewFromStringVarComplete(base);
  JsVar *suffix;
  if (!path) return 0;
  suffix = jsvVarPrintf("[%d]", index);
  if (!suffix) {
    jsvUnLock(path);
    return 0;
  }
  jsvAppendStringVarComplete(path, suffix);
  jsvUnLock(suffix);
  return path;
}

static void xfcFail(XfcCompiler *compiler, XfcDiagnostic diagnostic,
                    JsVar *path, JsVar *detail) {
  if (compiler->diagnostic != XFC_DIAG_NONE) return;
  if (!path && diagnostic != XFC_DIAG_NO_MEMORY) {
    compiler->diagnostic = XFC_DIAG_NO_MEMORY;
    return;
  }
  compiler->diagnostic = diagnostic;
  compiler->error_path = jsvLockAgainSafe(path);
  compiler->error_detail = jsvLockAgainSafe(detail);
}

static void xfcFailProperty(XfcCompiler *compiler, XfcDiagnostic diagnostic,
                            JsVar *base, const char *property,
                            JsVar *detail) {
  JsVar *path;
  if (compiler->diagnostic != XFC_DIAG_NONE) return;
  path = xfcPathProperty(base, property);
  xfcFail(compiler, path ? diagnostic : XFC_DIAG_NO_MEMORY, path, detail);
  jsvUnLock(path);
}

static void xfcFailKey(XfcCompiler *compiler, XfcDiagnostic diagnostic,
                       JsVar *base, JsVar *key, JsVar *detail) {
  JsVar *path;
  if (compiler->diagnostic != XFC_DIAG_NONE) return;
  path = xfcPathKey(base, key);
  xfcFail(compiler, path ? diagnostic : XFC_DIAG_NO_MEMORY, path, detail);
  jsvUnLock(path);
}

static void xfcThrowFailure(XfcCompiler *compiler) {
  const char *name;
  if (compiler->diagnostic == XFC_DIAG_NONE) return;
  if (compiler->diagnostic == XFC_DIAG_NO_MEMORY || !compiler->error_path) {
    jsExceptionHere(JSET_ERROR, "XFC E_NO_MEMORY @ createMachine");
    return;
  }
  name = xfcDiagnosticNames[compiler->diagnostic];
  if (compiler->error_detail)
    jsExceptionHere(JSET_ERROR, "XFC %s @ %v: %q", name,
                    compiler->error_path, compiler->error_detail);
  else
    jsExceptionHere(JSET_ERROR, "XFC %s @ %v", name,
                    compiler->error_path);
}

static bool xfcStringEquals(JsVar *left, JsVar *right) {
  return left && right && jsvIsString(left) && jsvIsString(right) &&
         jsvCompareString(left, right, 0, 0, false) == 0;
}

static bool xfcIncrement(XfcCompiler *compiler, uint32_t *value,
                         JsVar *path, const char *kind) {
  JsVar *detail;
  if (*value < UINT16_MAX) {
    (*value)++;
    return true;
  }
  detail = jsvVarPrintf("%s=65536 max=65535", kind);
  xfcFail(compiler, detail ? XFC_DIAG_LIMIT_EXCEEDED : XFC_DIAG_NO_MEMORY,
          path, detail);
  jsvUnLock(detail);
  return false;
}

static int xfcFindRetained(XfcCompiler *compiler, JsVar *value) {
  JsVarInt length = jsvGetArrayLength(compiler->retained);
  JsVarInt index;
  for (index = 0; index < length; index++) {
    JsVar *existing = jsvGetArrayItem(compiler->retained, index);
    bool equal = jsvIsEqual(existing, value);
    jsvUnLock(existing);
    if (equal) return (int)index;
  }
  return -1;
}

static uint16_t xfcRetain(XfcCompiler *compiler, JsVar *value,
                          JsVar *path) {
  int found = xfcFindRetained(compiler, value);
  JsVarInt length;
  if (found >= 0) return (uint16_t)found;
  if (compiler->phase == XFC_COMPILE_EMIT) {
    xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
    return XFC_INDEX_NONE;
  }
  length = jsvGetArrayLength(compiler->retained);
  if (length >= (JsVarInt)UINT16_MAX) {
    JsVar *detail = jsvNewFromString("retained=65536 max=65535");
    xfcFail(compiler, detail ? XFC_DIAG_LIMIT_EXCEEDED : XFC_DIAG_NO_MEMORY,
            path, detail);
    jsvUnLock(detail);
    return XFC_INDEX_NONE;
  }
  if (!xfcArrayAppend(compiler->retained, value)) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    return XFC_INDEX_NONE;
  }
  return (uint16_t)length;
}

static int xfcFindSymbol(XfcCompiler *compiler, JsVar *value) {
  JsVarInt length = jsvGetArrayLength(compiler->symbols);
  JsVarInt index;
  for (index = 0; index < length; index++) {
    JsVar *entry = jsvGetArrayItem(compiler->symbols, index);
    JsVar *text = jsvGetArrayItem(entry, 0);
    bool equal = xfcStringEquals(text, value);
    jsvUnLock2(text, entry);
    if (equal) return (int)index;
  }
  return -1;
}

static uint16_t xfcInternSymbol(XfcCompiler *compiler, JsVar *value,
                                uint16_t flags, JsVar *path) {
  int found;
  size_t length;
  JsVar *entry;
  JsVar *flag_value;
  if (!jsvIsString(value)) {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    return XFC_INDEX_NONE;
  }
  length = jsvGetStringLength(value);
  if (length > UINT16_MAX) {
    JsVar *detail = jsvVarPrintf("bytes=%d max=65535", (int)length);
    xfcFail(compiler, detail ? XFC_DIAG_LIMIT_EXCEEDED : XFC_DIAG_NO_MEMORY,
            path, detail);
    jsvUnLock(detail);
    return XFC_INDEX_NONE;
  }
  found = xfcFindSymbol(compiler, value);
  if (found >= 0) {
    entry = jsvGetArrayItem(compiler->symbols, (JsVarInt)found);
    flag_value = jsvGetArrayItem(entry, 1);
    flags |= (uint16_t)jsvGetInteger(flag_value);
    jsvUnLock(flag_value);
    if (compiler->phase == XFC_COMPILE_COUNT) {
      flag_value = jsvNewFromInteger(flags);
      if (!flag_value) {
        xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      } else {
        jsvSetArrayItem(entry, 1, flag_value);
        jsvUnLock(flag_value);
      }
    }
    jsvUnLock(entry);
    return (uint16_t)found;
  }
  if (compiler->phase == XFC_COMPILE_EMIT) {
    xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
    return XFC_INDEX_NONE;
  }
  if (compiler->counts.symbols >= UINT16_MAX) {
    JsVar *detail = jsvNewFromString("symbols=65536 max=65535");
    xfcFail(compiler, detail ? XFC_DIAG_LIMIT_EXCEEDED : XFC_DIAG_NO_MEMORY,
            path, detail);
    jsvUnLock(detail);
    return XFC_INDEX_NONE;
  }
  if (compiler->counts.string_bytes > UINT32_MAX - (uint32_t)length) {
    xfcFail(compiler, XFC_DIAG_LIMIT_EXCEEDED, path, 0);
    return XFC_INDEX_NONE;
  }
  entry = jsvNewEmptyArray();
  flag_value = jsvNewFromInteger(flags);
  if (!entry || !flag_value || !xfcArrayAppend(entry, value) ||
      !xfcArrayAppend(entry, flag_value) ||
      !xfcArrayAppend(compiler->symbols, entry)) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    jsvUnLock2(entry, flag_value);
    return XFC_INDEX_NONE;
  }
  jsvUnLock2(entry, flag_value);
  compiler->counts.symbols++;
  compiler->counts.string_bytes += (uint32_t)length;
  return (uint16_t)(compiler->counts.symbols - 1);
}

static JsVar *xfcMetaGet(JsVar *meta, int field) {
  return jsvGetArrayItem(meta, (JsVarInt)field);
}

static int xfcMetaInteger(JsVar *meta, int field) {
  return (int)jsvGetIntegerAndUnLock(xfcMetaGet(meta, field));
}

static JsVar *xfcStateMeta(XfcCompiler *compiler, uint16_t index) {
  return jsvGetArrayItem(compiler->states, (JsVarInt)index);
}

static bool xfcKeyInList(JsVar *key, const char *const *names,
                         size_t name_count) {
  size_t index;
  for (index = 0; index < name_count; index++) {
    if (jsvIsStringEqual(key, names[index])) return true;
  }
  return false;
}

static bool xfcValidateProperties(XfcCompiler *compiler, JsVar *object,
                                  JsVar *path,
                                  const char *const *allowed,
                                  size_t allowed_count) {
  static const char *const unsupported[] = {
      "invoke", "after", "always", "activities", "output", "tags",
      "history", "delimiter"};
  JsvObjectIterator iterator;
  jsvObjectIteratorNew(&iterator, object);
  while (jsvObjectIteratorHasValue(&iterator) &&
         compiler->diagnostic == XFC_DIAG_NONE) {
    JsVar *key = jsvObjectIteratorGetKey(&iterator);
    if (jsvIsInternalObjectKey(key)) {
      jsvUnLock(key);
      jsvObjectIteratorNext(&iterator);
      continue;
    }
    if (jsvIsGetterOrSetter(key)) {
      xfcFailKey(compiler, XFC_DIAG_CONFIG_TYPE, path, key, 0);
    } else if (!xfcKeyInList(key, allowed, allowed_count)) {
      XfcDiagnostic diagnostic =
          xfcKeyInList(key, unsupported,
                       sizeof(unsupported) / sizeof(unsupported[0]))
              ? XFC_DIAG_UNSUPPORTED_FEATURE
              : XFC_DIAG_UNKNOWN_PROPERTY;
      xfcFailKey(compiler, diagnostic, path, key, 0);
    }
    jsvUnLock(key);
    jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static uint32_t xfcVisibleChildren(JsVar *object) {
  JsvObjectIterator iterator;
  uint32_t count = 0;
  if (!xfcIsObject(object)) return 0;
  jsvObjectIteratorNew(&iterator, object);
  while (jsvObjectIteratorHasValue(&iterator)) {
    JsVar *key = jsvObjectIteratorGetKey(&iterator);
    if (!jsvIsInternalObjectKey(key)) count++;
    jsvUnLock(key);
    jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  return count;
}

static bool xfcValidateEmptyObject(XfcCompiler *compiler, JsVar *owner,
                                   JsVar *owner_path,
                                   const char *property) {
  JsVar *value = 0;
  if (!xfcGetOwn(owner, property, &value)) return true;
  if (jsvIsUndefined(value)) {
    jsvUnLock(value);
    return true;
  }
  if (!xfcIsObject(value) || xfcVisibleChildren(value) != 0)
    xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, owner_path,
                    property, 0);
  jsvUnLock(value);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcValidateImplementationMap(XfcCompiler *compiler, JsVar *options,
                                         JsVar *options_path,
                                         const char *property,
                                         JsVar **map_out) {
  JsVar *map = 0;
  JsVar *map_path;
  JsvObjectIterator iterator;
  *map_out = 0;
  if (!xfcGetOwn(options, property, &map) || jsvIsUndefined(map)) {
    jsvUnLock(map);
    return true;
  }
  if (!xfcIsObject(map)) {
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, options_path, property, 0);
    jsvUnLock(map);
    return false;
  }
  map_path = xfcPathProperty(options_path, property);
  if (!map_path) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    jsvUnLock(map);
    return false;
  }
  jsvObjectIteratorNew(&iterator, map);
  while (jsvObjectIteratorHasValue(&iterator) &&
         compiler->diagnostic == XFC_DIAG_NONE) {
    JsVar *key = jsvObjectIteratorGetKey(&iterator);
    if (!jsvIsInternalObjectKey(key)) {
      JsVar *value = jsvObjectIteratorGetValue(&iterator);
      if (jsvIsGetterOrSetter(key) || !jsvIsFunction(value))
        xfcFailKey(compiler, XFC_DIAG_CONFIG_TYPE, map_path, key, 0);
      jsvUnLock(value);
    }
    jsvUnLock(key);
    jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  jsvUnLock(map_path);
  if (compiler->diagnostic != XFC_DIAG_NONE) {
    jsvUnLock(map);
    return false;
  }
  *map_out = map;
  return true;
}

static bool xfcValidateOptions(XfcCompiler *compiler, JsVar *options,
                               JsVar *options_path) {
  static const char *const allowed[] = {
      "actions", "guards", "services", "actors", "delays"};
  if (!options || jsvIsUndefined(options)) return true;
  if (!xfcIsObject(options)) {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, options_path, 0);
    return false;
  }
  if (!xfcValidateProperties(compiler, options, options_path, allowed,
                             sizeof(allowed) / sizeof(allowed[0])) ||
      !xfcValidateImplementationMap(compiler, options, options_path, "actions",
                                    &compiler->actions_map) ||
      !xfcValidateImplementationMap(compiler, options, options_path, "guards",
                                    &compiler->guards_map) ||
      !xfcValidateEmptyObject(compiler, options, options_path, "services") ||
      !xfcValidateEmptyObject(compiler, options, options_path, "actors") ||
      !xfcValidateEmptyObject(compiler, options, options_path, "delays"))
    return false;
  return true;
}

static JsVar *xfcCopyString(JsVar *value) {
  if (!value || !jsvIsString(value)) return 0;
  return jsvNewFromStringVarComplete(value);
}

static JsVar *xfcEscapeIdSegment(JsVar *prefix, JsVar *key) {
  JsVar *result = jsvNewFromStringVarComplete(prefix);
  JsvStringIterator iterator;
  if (!result) return 0;
  jsvAppendCharacter(result, '.');
  jsvStringIteratorNew(&iterator, key, 0);
  while (jsvStringIteratorHasChar(&iterator)) {
    char ch = jsvStringIteratorGetChar(&iterator);
    if (ch == '.' || ch == '\\') jsvAppendCharacter(result, '\\');
    jsvAppendCharacter(result, ch);
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return result;
}

static bool xfcMetadataIdExists(XfcCompiler *compiler, JsVar *effective_id) {
  JsVarInt length = jsvGetArrayLength(compiler->states);
  JsVarInt index;
  for (index = 0; index < length; index++) {
    JsVar *meta = jsvGetArrayItem(compiler->states, index);
    JsVar *existing = xfcMetaGet(meta, XFC_META_EFFECTIVE_ID);
    bool equal = xfcStringEquals(existing, effective_id);
    jsvUnLock2(existing, meta);
    if (equal) return true;
  }
  return false;
}

static bool xfcConfigurationIsAncestor(XfcCompiler *compiler,
                                       uint16_t parent_index,
                                       JsVar *configuration) {
  int current = (int)parent_index;
  while (current >= 0) {
    JsVar *meta = xfcStateMeta(compiler, (uint16_t)current);
    JsVar *existing = xfcMetaGet(meta, XFC_META_CONFIG);
    int next = xfcMetaInteger(meta, XFC_META_PARENT);
    bool equal = jsvIsEqual(existing, configuration);
    jsvUnLock2(existing, meta);
    if (equal) return true;
    current = next;
  }
  return false;
}

static bool xfcDetermineNodeType(XfcCompiler *compiler, JsVar *configuration,
                                 JsVar *path, bool root,
                                 XfcNodeType *type_out,
                                 uint32_t *child_count_out) {
  JsVar *states = 0;
  JsVar *type = 0;
  JsVar *initial = 0;
  bool has_states = xfcGetOwn(configuration, "states", &states);
  bool has_type = xfcGetOwn(configuration, "type", &type);
  bool has_initial = xfcGetOwn(configuration, "initial", &initial);
  uint32_t child_count = 0;
  XfcNodeType node_type = XFC_NODE_ATOMIC;

  if (has_states && !jsvIsUndefined(states)) {
    if (!xfcIsObject(states))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "states", 0);
    else
      child_count = xfcVisibleChildren(states);
  }
  if (compiler->diagnostic == XFC_DIAG_NONE && has_type &&
      !jsvIsUndefined(type)) {
    if (!jsvIsString(type)) {
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "type", 0);
    } else if (jsvIsStringEqual(type, "compound")) {
      node_type = XFC_NODE_COMPOUND;
    } else if (jsvIsStringEqual(type, "atomic")) {
      node_type = XFC_NODE_ATOMIC;
    } else if (jsvIsStringEqual(type, "final")) {
      xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, "type",
                      type);
    } else if (jsvIsStringEqual(type, "parallel") ||
               jsvIsStringEqual(type, "history")) {
      xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, "type",
                      type);
    } else {
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "type", type);
    }
  } else if (compiler->diagnostic == XFC_DIAG_NONE) {
    node_type = child_count == 0 ? XFC_NODE_ATOMIC : XFC_NODE_COMPOUND;
  }

  if (compiler->diagnostic == XFC_DIAG_NONE) {
    if (node_type == XFC_NODE_COMPOUND && child_count == 0)
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "states", 0);
    else if (node_type == XFC_NODE_ATOMIC && child_count != 0)
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "states", 0);
    else if (node_type == XFC_NODE_ATOMIC && has_initial &&
             !jsvIsUndefined(initial))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "initial", 0);
    else if (node_type == XFC_NODE_COMPOUND &&
             (!has_initial || jsvIsUndefined(initial)))
      xfcFailProperty(compiler, XFC_DIAG_INITIAL_REQUIRED, path, "initial", 0);
  }
  (void)root;
  jsvUnLock3(states, type, initial);
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  *type_out = node_type;
  *child_count_out = child_count;
  return true;
}

static bool xfcValidateNode(XfcCompiler *compiler, JsVar *configuration,
                            JsVar *path, bool root, XfcNodeType *type_out,
                            uint32_t *child_count_out) {
  static const char *const root_allowed[] = {
      "id", "type", "context", "initial", "states", "on", "entry",
      "exit", "description", "meta", "predictableActionArguments",
      "preserveActionOrder"};
  static const char *const state_allowed[] = {
      "id", "type", "initial", "states", "on", "onDone", "entry",
      "exit", "description", "meta"};
  JsVar *value = 0;
  if (!xfcIsObject(configuration)) {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    return false;
  }
  if (!xfcValidateProperties(
          compiler, configuration, path,
          root ? root_allowed : state_allowed,
          root ? sizeof(root_allowed) / sizeof(root_allowed[0])
               : sizeof(state_allowed) / sizeof(state_allowed[0])))
    return false;

  if (xfcGetOwn(configuration, "id", &value) && !jsvIsUndefined(value) &&
      (!jsvIsString(value) || jsvGetStringLength(value) == 0))
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "id", 0);
  jsvUnLock(value);
  value = 0;
  if (xfcGetOwn(configuration, "description", &value) &&
      !jsvIsUndefined(value) && !jsvIsString(value))
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "description", 0);
  jsvUnLock(value);
  value = 0;
  if (xfcGetOwn(configuration, "meta", &value) && !jsvIsUndefined(value) &&
      (!xfcIsObject(value) || xfcVisibleChildren(value) != 0))
    xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, "meta", 0);
  jsvUnLock(value);
  value = 0;
  if (xfcGetOwn(configuration, "on", &value) && !jsvIsUndefined(value) &&
      !xfcIsObject(value))
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "on", 0);
  jsvUnLock(value);
  value = 0;
  if (!root && xfcGetOwn(configuration, "onDone", &value) &&
      !jsvIsUndefined(value))
    xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, "onDone", 0);
  jsvUnLock(value);
  value = 0;
  if (root && xfcGetOwn(configuration, "predictableActionArguments", &value) &&
      (!jsvIsBoolean(value) || !jsvGetBool(value)))
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path,
                    "predictableActionArguments", 0);
  jsvUnLock(value);
  value = 0;
  if (root && xfcGetOwn(configuration, "preserveActionOrder", &value) &&
      (!jsvIsBoolean(value) || !jsvGetBool(value)))
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path,
                    "preserveActionOrder", 0);
  jsvUnLock(value);
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  return xfcDetermineNodeType(compiler, configuration, path, root, type_out,
                              child_count_out);
}

static JsVar *xfcCompletionEvent(JsVar *effective_id) {
  JsVar *event = jsvNewFromString("done.state.");
  if (!event) return 0;
  jsvAppendStringVarComplete(event, effective_id);
  return event;
}

static bool xfcAppendMetaField(JsVar *meta, JsVar *value) {
  return value && xfcArrayAppend(meta, value);
}

static bool xfcAddStateMetadata(XfcCompiler *compiler, JsVar *configuration,
                                JsVar *key, int parent, uint8_t depth,
                                JsVar *path, JsVar *implicit_id,
                                bool root) {
  JsVar *explicit_id = 0;
  JsVar *effective_id = 0;
  JsVar *completion = 0;
  JsVar *meta = 0;
  JsVar *parent_value = 0;
  JsVar *depth_value = 0;
  JsVar *type_value = 0;
  JsVar *null_key = 0;
  JsVar *null_completion = 0;
  XfcNodeType node_type;
  uint32_t child_count;
  bool ok = false;

  if (!xfcValidateNode(compiler, configuration, path, root, &node_type,
                       &child_count))
    goto done;
  (void)child_count;
  if (xfcGetOwn(configuration, "id", &explicit_id) &&
      !jsvIsUndefined(explicit_id)) {
    effective_id = jsvLockAgain(explicit_id);
  } else {
    effective_id = jsvLockAgain(implicit_id);
  }
  if (!effective_id) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  if (jsvGetStringLength(effective_id) > UINT16_MAX) {
    xfcFailProperty(compiler, XFC_DIAG_LIMIT_EXCEEDED, path, "id", 0);
    goto done;
  }
  if (xfcMetadataIdExists(compiler, effective_id)) {
    xfcFailProperty(compiler, XFC_DIAG_ID_DUPLICATE, path, "id",
                    effective_id);
    goto done;
  }
  if (!root && xfcInternSymbol(compiler, key, XFC_SYMBOL_STATE_KEY, path) ==
                   XFC_INDEX_NONE)
    goto done;
  if (!root && node_type == XFC_NODE_COMPOUND) {
    completion = xfcCompletionEvent(effective_id);
    if (!completion) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (xfcInternSymbol(compiler, completion,
                        XFC_SYMBOL_EVENT | XFC_SYMBOL_COMPLETION_EVENT,
                        path) == XFC_INDEX_NONE)
      goto done;
  }
  if (compiler->counts.states >= UINT16_MAX) {
    JsVar *detail = jsvNewFromString("states=65536 max=65535");
    xfcFail(compiler, detail ? XFC_DIAG_LIMIT_EXCEEDED : XFC_DIAG_NO_MEMORY,
            path, detail);
    jsvUnLock(detail);
    goto done;
  }

  meta = jsvNewEmptyArray();
  parent_value = jsvNewFromInteger(parent);
  depth_value = jsvNewFromInteger((JsVarInt)depth);
  type_value = jsvNewFromInteger((JsVarInt)node_type);
  null_key = jsvNewNull();
  null_completion = jsvNewNull();
  if (!meta || !parent_value || !depth_value || !type_value || !null_key ||
      !null_completion || !xfcAppendMetaField(meta, configuration) ||
      !xfcAppendMetaField(meta, root ? null_key : key) ||
      !xfcAppendMetaField(meta, parent_value) ||
      !xfcAppendMetaField(meta, depth_value) ||
      !xfcAppendMetaField(meta, path) ||
      !xfcAppendMetaField(meta, effective_id) ||
      !xfcAppendMetaField(meta, implicit_id) ||
      !xfcAppendMetaField(meta, type_value) ||
      !xfcAppendMetaField(meta, completion ? completion : null_completion) ||
      !xfcArrayAppend(compiler->states, meta)) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  compiler->counts.states++;
  ok = true;

done:
  jsvUnLock(explicit_id);
  jsvUnLock(effective_id);
  jsvUnLock(completion);
  jsvUnLock(meta);
  jsvUnLock(parent_value);
  jsvUnLock(depth_value);
  jsvUnLock(type_value);
  jsvUnLock(null_key);
  jsvUnLock(null_completion);
  return ok;
}

static bool xfcEnumerateStates(XfcCompiler *compiler, JsVar *config,
                               JsVar *config_path) {
  JsVar *root_id = 0;
  JsVar *explicit_root_id = 0;
  JsVarInt state_index;
  if (xfcGetOwn(config, "id", &explicit_root_id) &&
      !jsvIsUndefined(explicit_root_id))
    root_id = xfcCopyString(explicit_root_id);
  else
    root_id = jsvNewFromString("(machine)");
  jsvUnLock(explicit_root_id);
  if (!root_id) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    return false;
  }
  if (!xfcAddStateMetadata(compiler, config, 0, -1, 0, config_path,
                           root_id, true)) {
    jsvUnLock(root_id);
    return false;
  }
  jsvUnLock(root_id);

  for (state_index = 0;
       state_index < jsvGetArrayLength(compiler->states) &&
       compiler->diagnostic == XFC_DIAG_NONE;
       state_index++) {
    JsVar *meta = jsvGetArrayItem(compiler->states, state_index);
    JsVar *configuration = xfcMetaGet(meta, XFC_META_CONFIG);
    JsVar *path = xfcMetaGet(meta, XFC_META_PATH);
    JsVar *implicit_id = xfcMetaGet(meta, XFC_META_IMPLICIT_ID);
    JsVar *states = 0;
    int node_type = xfcMetaInteger(meta, XFC_META_TYPE);
    int depth = xfcMetaInteger(meta, XFC_META_DEPTH);
    if (node_type == XFC_NODE_COMPOUND &&
        xfcGetOwn(configuration, "states", &states) && xfcIsObject(states)) {
      JsVar *states_path = xfcPathProperty(path, "states");
      JsvObjectIterator iterator;
      if (!states_path) {
        xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      } else {
        jsvObjectIteratorNew(&iterator, states);
        while (jsvObjectIteratorHasValue(&iterator) &&
               compiler->diagnostic == XFC_DIAG_NONE) {
          JsVar *source_key = jsvObjectIteratorGetKey(&iterator);
          JsVar *child = jsvObjectIteratorGetValue(&iterator);
          if (!jsvIsInternalObjectKey(source_key)) {
            JsVar *key = xfcCopyString(source_key);
            JsVar *child_path = key ? xfcPathKey(states_path, key) : 0;
            JsVar *child_implicit =
                key ? xfcEscapeIdSegment(implicit_id, key) : 0;
            if (jsvIsGetterOrSetter(source_key)) {
              xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, child_path, 0);
            } else if (!key || !child_path || !child_implicit) {
              xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
            } else if (jsvGetStringLength(key) == 0 || !xfcIsObject(child)) {
              xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, child_path, 0);
            } else if (depth + 1 > XFC_MAX_STATE_DEPTH) {
              xfcFail(compiler, XFC_DIAG_LIMIT_EXCEEDED, child_path, 0);
            } else if (xfcConfigurationIsAncestor(
                           compiler, (uint16_t)state_index, child)) {
              xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, child_path, 0);
            } else {
              xfcAddStateMetadata(
                  compiler, child, key, (int)state_index,
                  (uint8_t)(depth + 1), child_path, child_implicit, false);
            }
            jsvUnLock3(key, child_path, child_implicit);
          }
          jsvUnLock2(source_key, child);
          jsvObjectIteratorNext(&iterator);
        }
        jsvObjectIteratorFree(&iterator);
      }
      jsvUnLock(states_path);
    }
    jsvUnLock(states);
    jsvUnLock3(configuration, path, implicit_id);
    jsvUnLock(meta);
  }
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static uint16_t xfcFindDirectChild(XfcCompiler *compiler,
                                   uint16_t parent_index, JsVar *key) {
  JsVarInt length = jsvGetArrayLength(compiler->states);
  JsVarInt index;
  for (index = 1; index < length; index++) {
    JsVar *meta = jsvGetArrayItem(compiler->states, index);
    int parent = xfcMetaInteger(meta, XFC_META_PARENT);
    if (parent == (int)parent_index) {
      JsVar *candidate = xfcMetaGet(meta, XFC_META_KEY);
      bool equal = xfcStringEquals(candidate, key);
      jsvUnLock(candidate);
      jsvUnLock(meta);
      if (equal) return (uint16_t)index;
    } else {
      jsvUnLock(meta);
    }
  }
  return XFC_INDEX_NONE;
}

static uint16_t xfcFindStateById(XfcCompiler *compiler, JsVar *id) {
  JsVarInt length = jsvGetArrayLength(compiler->states);
  JsVarInt index;
  for (index = 0; index < length; index++) {
    JsVar *meta = jsvGetArrayItem(compiler->states, index);
    JsVar *candidate = xfcMetaGet(meta, XFC_META_EFFECTIVE_ID);
    bool equal = xfcStringEquals(candidate, id);
    jsvUnLock2(candidate, meta);
    if (equal) return (uint16_t)index;
  }
  return XFC_INDEX_NONE;
}

static int xfcStateParent(XfcCompiler *compiler, uint16_t index) {
  JsVar *meta = xfcStateMeta(compiler, index);
  int parent = xfcMetaInteger(meta, XFC_META_PARENT);
  jsvUnLock(meta);
  return parent;
}

static bool xfcStateIsDescendantOrSelf(XfcCompiler *compiler,
                                       uint16_t state, uint16_t ancestor) {
  int current = state;
  int steps = 0;
  while (current >= 0 && steps <= XFC_MAX_STATE_DEPTH) {
    if ((uint16_t)current == ancestor) return true;
    current = xfcStateParent(compiler, (uint16_t)current);
    steps++;
  }
  return false;
}

static uint16_t xfcTransitionDomain(XfcCompiler *compiler, uint16_t source,
                                    uint16_t target, bool reenter) {
  int candidate;
  int steps = 0;
  if (target == XFC_INDEX_NONE) return XFC_INDEX_NONE;
  if (!reenter && xfcStateIsDescendantOrSelf(compiler, target, source))
    return source;
  candidate = xfcStateParent(compiler, source);
  while (candidate >= 0 && steps <= XFC_MAX_STATE_DEPTH) {
    if ((uint16_t)candidate != target &&
        xfcStateIsDescendantOrSelf(compiler, target, (uint16_t)candidate))
      return (uint16_t)candidate;
    candidate = xfcStateParent(compiler, (uint16_t)candidate);
    steps++;
  }
  return XFC_INDEX_NONE;
}

static bool xfcStringHasByte(JsVar *value, char wanted) {
  JsvStringIterator iterator;
  bool found = false;
  jsvStringIteratorNew(&iterator, value, 0);
  while (jsvStringIteratorHasChar(&iterator)) {
    if (jsvStringIteratorGetChar(&iterator) == wanted) {
      found = true;
      break;
    }
    jsvStringIteratorNext(&iterator);
  }
  jsvStringIteratorFree(&iterator);
  return found;
}

static uint16_t xfcResolveTarget(XfcCompiler *compiler, uint16_t source,
                                 JsVar *target, JsVar *path) {
  uint16_t resolved = XFC_INDEX_NONE;
  size_t length;
  char first;
  if (!jsvIsString(target) || jsvGetStringLength(target) == 0) {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    return XFC_INDEX_NONE;
  }
  length = jsvGetStringLength(target);
  first = (char)jsvGetCharInString(target, 0);
  if (first == '#') {
    JsVar *id = jsvNewFromStringVar(target, 1, length - 1);
    if (!id) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      return XFC_INDEX_NONE;
    }
    if (xfcStringHasByte(id, '.') || xfcStringHasByte(id, '\\')) {
      xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, target);
    } else {
      resolved = xfcFindStateById(compiler, id);
    }
    jsvUnLock(id);
  } else if (first == '.') {
    JsVar *key = jsvNewFromStringVar(target, 1, length - 1);
    if (!key) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      return XFC_INDEX_NONE;
    }
    if (xfcStringHasByte(key, '\\'))
      xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, target);
    else
      resolved = xfcFindDirectChild(compiler, source, key);
    jsvUnLock(key);
  } else if (first == '\\') {
    xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, target);
  } else {
    int parent = xfcStateParent(compiler, source);
    uint16_t base = parent < 0 ? source : (uint16_t)parent;
    resolved = xfcFindDirectChild(compiler, base, target);
    if (resolved == XFC_INDEX_NONE && xfcStringHasByte(target, '\\'))
      xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, target);
  }
  if (compiler->diagnostic == XFC_DIAG_NONE && resolved == XFC_INDEX_NONE)
    xfcFail(compiler, XFC_DIAG_TARGET_UNKNOWN, path, target);
  return resolved;
}

static bool xfcWriteRecord(XfcArenaWriter *writer, uint16_t table,
                           uint16_t index, const void *record,
                           size_t record_size) {
  XfcTableRef reference;
  uint32_t offset;
  if (!writer || table >= XFC_TABLE_COUNT) return false;
  reference = writer->header.tables[table];
  if (index >= reference.count || record_size != reference.record_size)
    return false;
  offset = reference.offset + (uint32_t)index * reference.record_size;
  memcpy(writer->bytes + offset, record, record_size);
  return true;
}

static JsVar *xfcResolveImplementation(JsVar *map, JsVar *name) {
  JsVar *property;
  if (!map || !name) return 0;
  property = jsvFindChildFromVar(map, name, false);
  if (!property || jsvIsGetterOrSetter(property)) {
    jsvUnLock(property);
    return 0;
  }
  return jsvSkipNameAndUnLock(property);
}

static bool xfcAssignmentDescriptorValue(JsVar *descriptor, JsVar **value) {
  JsVar *brand = 0;
  JsVar *assignment = 0;
  bool valid = false;
  *value = 0;
  if (!xfcIsObject(descriptor) ||
      !xfcGetOwn(descriptor, XFC_ASSIGN_BRAND_NAME, &brand) ||
      !jsvIsString(brand) || !jsvIsStringEqual(brand, XFC_ASSIGN_BRAND) ||
      !xfcGetOwn(descriptor, XFC_ASSIGN_VALUE_NAME, &assignment))
    goto done;
  valid = jsvIsFunction(assignment) || xfcIsObject(assignment);
done:
  jsvUnLock(brand);
  if (valid) {
    *value = assignment;
  } else {
    jsvUnLock(assignment);
  }
  return valid;
}

static bool xfcCompileAssignment(XfcCompiler *compiler, JsVar *assignment,
                                 JsVar *path, uint16_t *assignment_index) {
  XfcAssignmentRecord record;
  uint16_t index;
  if (compiler->phase == XFC_COMPILE_COUNT) {
    if (!xfcIncrement(compiler, &compiler->counts.assignments, path,
                      "assignments"))
      return false;
    index = (uint16_t)(compiler->counts.assignments - 1);
  } else {
    index = compiler->writer->assignment++;
  }
  memset(&record, 0, sizeof(record));
  record.retained_slot = XFC_INDEX_NONE;
  record.entries.first = XFC_INDEX_NONE;
  if (jsvIsFunction(assignment)) {
    record.kind = XFC_ASSIGN_PARTIAL;
    record.retained_slot = xfcRetain(compiler, assignment, path);
    if (record.retained_slot == XFC_INDEX_NONE) return false;
  } else if (xfcIsObject(assignment)) {
    JsvObjectIterator iterator;
    uint16_t first = compiler->phase == XFC_COMPILE_COUNT
                         ? (uint16_t)compiler->counts.assignment_entries
                         : compiler->writer->assignment_entry;
    uint16_t count = 0;
    record.kind = XFC_ASSIGN_PROPERTY_MAP;
    jsvObjectIteratorNew(&iterator, assignment);
    while (jsvObjectIteratorHasValue(&iterator) &&
           compiler->diagnostic == XFC_DIAG_NONE) {
      JsVar *source_key = jsvObjectIteratorGetKey(&iterator);
      JsVar *value = jsvObjectIteratorGetValue(&iterator);
      if (!jsvIsInternalObjectKey(source_key)) {
        JsVar *key = xfcCopyString(source_key);
        JsVar *entry_path = key ? xfcPathKey(path, key) : 0;
        uint16_t key_symbol;
        uint16_t retained_slot;
        if (jsvIsGetterOrSetter(source_key) || !key || !entry_path) {
          xfcFail(compiler, key && entry_path ? XFC_DIAG_CONFIG_TYPE
                                              : XFC_DIAG_NO_MEMORY,
                  entry_path, 0);
        } else {
          key_symbol = xfcInternSymbol(compiler, key, XFC_SYMBOL_CONTEXT_KEY,
                                       entry_path);
          retained_slot = xfcRetain(compiler, value, entry_path);
          if (key_symbol != XFC_INDEX_NONE &&
              retained_slot != XFC_INDEX_NONE) {
            XfcAssignmentEntryRecord entry;
            memset(&entry, 0, sizeof(entry));
            entry.key_symbol = key_symbol;
            entry.retained_slot = retained_slot;
            entry.flags = jsvIsFunction(value)
                              ? XFC_ASSIGN_ENTRY_EXPRESSION
                              : XFC_ASSIGN_ENTRY_LITERAL;
            if (compiler->phase == XFC_COMPILE_COUNT) {
              xfcIncrement(compiler,
                           &compiler->counts.assignment_entries,
                           entry_path, "assignmentEntries");
            } else if (!xfcWriteRecord(
                           compiler->writer, XFC_TABLE_ASSIGNMENT_ENTRY,
                           compiler->writer->assignment_entry++, &entry,
                           sizeof(entry))) {
              xfcFail(compiler, XFC_DIAG_INTERNAL, entry_path, 0);
            }
            count++;
          }
        }
        jsvUnLock2(key, entry_path);
      }
      jsvUnLock2(source_key, value);
      jsvObjectIteratorNext(&iterator);
    }
    jsvObjectIteratorFree(&iterator);
    record.entries.first = count == 0 ? XFC_INDEX_NONE : first;
    record.entries.count = count;
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    return false;
  }
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  if (compiler->phase == XFC_COMPILE_EMIT &&
      !xfcWriteRecord(compiler->writer, XFC_TABLE_ASSIGNMENT, index, &record,
                      sizeof(record))) {
    xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
    return false;
  }
  *assignment_index = index;
  return true;
}

static bool xfcCompileOneAction(XfcCompiler *compiler, JsVar *action,
                                JsVar *path) {
  static const char *const descriptor_allowed[] = {"type"};
  XfcActionRecord record;
  JsVar *implementation = 0;
  JsVar *name = 0;
  JsVar *assignment = 0;
  uint16_t action_index;
  memset(&record, 0, sizeof(record));

  if (jsvIsFunction(action)) {
    record.kind = XFC_ACTION_USER;
    record.reference = xfcRetain(compiler, action, path);
  } else if (jsvIsString(action)) {
    name = jsvLockAgain(action);
  } else if (xfcAssignmentDescriptorValue(action, &assignment)) {
    record.kind = XFC_ACTION_ASSIGN;
    if (!xfcCompileAssignment(compiler, assignment, path,
                              &record.reference))
      goto done;
  } else if (xfcIsObject(action)) {
    if (!xfcValidateProperties(compiler, action, path, descriptor_allowed,
                               sizeof(descriptor_allowed) /
                                   sizeof(descriptor_allowed[0])))
      goto done;
    if (!xfcGetOwn(action, "type", &name) || !jsvIsString(name)) {
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "type", 0);
      goto done;
    }
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    goto done;
  }

  if (name) {
    implementation = xfcResolveImplementation(compiler->actions_map, name);
    if (!jsvIsFunction(implementation)) {
      xfcFail(compiler, XFC_DIAG_ACTION_UNRESOLVED, path, name);
      goto done;
    }
    xfcInternSymbol(compiler, name, XFC_SYMBOL_ACTION, path);
    record.kind = XFC_ACTION_USER;
    record.reference = xfcRetain(compiler, implementation, path);
  }
  if (compiler->diagnostic != XFC_DIAG_NONE ||
      record.reference == XFC_INDEX_NONE)
    goto done;
  if (compiler->phase == XFC_COMPILE_COUNT) {
    if (!xfcIncrement(compiler, &compiler->counts.actions, path, "actions"))
      goto done;
  } else {
    action_index = compiler->writer->action++;
    if (!xfcWriteRecord(compiler->writer, XFC_TABLE_ACTION, action_index,
                        &record, sizeof(record)))
      xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
  }

done:
  jsvUnLock3(implementation, name, assignment);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcCompileActions(XfcCompiler *compiler, JsVar *actions,
                              JsVar *path, XfcRange *range) {
  uint16_t first = compiler->phase == XFC_COMPILE_COUNT
                       ? (uint16_t)compiler->counts.actions
                       : compiler->writer->action;
  uint16_t count = 0;
  range->first = XFC_INDEX_NONE;
  range->count = 0;
  if (!actions || jsvIsUndefined(actions)) return true;
  if (jsvIsArray(actions)) {
    JsVarInt length = jsvGetArrayLength(actions);
    JsVarInt index;
    for (index = 0; index < length &&
                    compiler->diagnostic == XFC_DIAG_NONE;
         index++) {
      JsVar *action = jsvGetArrayItem(actions, index);
      JsVar *action_path = xfcPathIndex(path, index);
      if (!action_path)
        xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      else if (!action || jsvIsUndefined(action))
        xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, action_path, 0);
      else if (xfcCompileOneAction(compiler, action, action_path))
        count++;
      jsvUnLock2(action, action_path);
    }
  } else if (xfcCompileOneAction(compiler, actions, path)) {
    count = 1;
  }
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  range->first = count == 0 ? XFC_INDEX_NONE : first;
  range->count = count;
  return true;
}

static bool xfcCompileGuard(XfcCompiler *compiler, JsVar *guard,
                            JsVar *path, uint16_t *guard_index) {
  static const char *const descriptor_allowed[] = {"type"};
  JsVar *name = 0;
  JsVar *implementation = 0;
  XfcGuardRecord record;
  uint16_t index;
  memset(&record, 0, sizeof(record));
  if (!guard || jsvIsUndefined(guard)) {
    *guard_index = XFC_INDEX_NONE;
    return true;
  }
  if (jsvIsFunction(guard)) {
    implementation = jsvLockAgain(guard);
  } else if (jsvIsString(guard)) {
    name = jsvLockAgain(guard);
  } else if (xfcIsObject(guard)) {
    if (!xfcValidateProperties(compiler, guard, path, descriptor_allowed,
                               sizeof(descriptor_allowed) /
                                   sizeof(descriptor_allowed[0])) ||
        !xfcGetOwn(guard, "type", &name) || !jsvIsString(name)) {
      if (compiler->diagnostic == XFC_DIAG_NONE)
        xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "type", 0);
      goto done;
    }
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    goto done;
  }
  if (name) {
    implementation = xfcResolveImplementation(compiler->guards_map, name);
    if (!jsvIsFunction(implementation)) {
      xfcFail(compiler, XFC_DIAG_GUARD_UNRESOLVED, path, name);
      goto done;
    }
    xfcInternSymbol(compiler, name, XFC_SYMBOL_GUARD, path);
  }
  record.retained_slot = xfcRetain(compiler, implementation, path);
  if (compiler->diagnostic != XFC_DIAG_NONE ||
      record.retained_slot == XFC_INDEX_NONE)
    goto done;
  if (compiler->phase == XFC_COMPILE_COUNT) {
    if (!xfcIncrement(compiler, &compiler->counts.guards, path, "guards"))
      goto done;
    index = (uint16_t)(compiler->counts.guards - 1);
  } else {
    index = compiler->writer->guard++;
    if (!xfcWriteRecord(compiler->writer, XFC_TABLE_GUARD, index, &record,
                        sizeof(record))) {
      xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
      goto done;
    }
  }
  *guard_index = index;

done:
  jsvUnLock2(name, implementation);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcReadReenter(XfcCompiler *compiler, JsVar *descriptor,
                           JsVar *path, bool *reenter) {
  JsVar *canonical = 0;
  JsVar *legacy = 0;
  bool has_canonical = xfcGetOwn(descriptor, "reenter", &canonical);
  bool has_legacy = xfcGetOwn(descriptor, "internal", &legacy);
  *reenter = false;
  if (has_canonical && has_legacy) {
    xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "reenter", 0);
  } else if (has_canonical) {
    if (!jsvIsBoolean(canonical))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "reenter", 0);
    else
      *reenter = jsvGetBool(canonical);
  } else if (has_legacy) {
    if (!jsvIsBoolean(legacy))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "internal", 0);
    else
      *reenter = !jsvGetBool(legacy);
  }
  jsvUnLock2(canonical, legacy);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcCompileTransition(XfcCompiler *compiler, uint16_t source,
                                 JsVar *candidate, JsVar *path) {
  static const char *const allowed[] = {
      "target", "actions", "guard", "cond", "reenter", "internal",
      "description", "meta"};
  XfcTransitionRecord record;
  JsVar *target = 0;
  JsVar *actions = 0;
  JsVar *guard = 0;
  JsVar *legacy_guard = 0;
  JsVar *value = 0;
  JsVar *target_path = 0;
  JsVar *actions_path = 0;
  JsVar *guard_path = 0;
  bool descriptor = xfcIsObject(candidate);
  bool reenter = false;
  uint16_t index;
  memset(&record, 0, sizeof(record));
  record.target_state = XFC_INDEX_NONE;
  record.guard = XFC_INDEX_NONE;
  record.actions.first = XFC_INDEX_NONE;
  record.domain_state = XFC_INDEX_NONE;

  if (jsvIsString(candidate)) {
    target = jsvLockAgain(candidate);
    target_path = jsvLockAgain(path);
  } else if (!candidate || jsvIsUndefined(candidate)) {
    descriptor = false;
  } else if (descriptor) {
    bool has_guard;
    bool has_legacy_guard;
    if (!xfcValidateProperties(compiler, candidate, path, allowed,
                               sizeof(allowed) / sizeof(allowed[0])))
      goto done;
    if (xfcGetOwn(candidate, "description", &value) &&
        !jsvIsUndefined(value) && !jsvIsString(value))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "description", 0);
    jsvUnLock(value);
    value = 0;
    if (xfcGetOwn(candidate, "meta", &value) && !jsvIsUndefined(value) &&
        (!xfcIsObject(value) || xfcVisibleChildren(value) != 0))
      xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, path, "meta", 0);
    jsvUnLock(value);
    value = 0;
    if (compiler->diagnostic != XFC_DIAG_NONE) goto done;
    if (xfcGetOwn(candidate, "target", &target) &&
        !jsvIsUndefined(target)) {
      target_path = xfcPathProperty(path, "target");
      if (jsvIsArray(target))
        xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, target_path, 0);
      else if (!jsvIsString(target))
        xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, target_path, 0);
    } else {
      jsvUnLock(target);
      target = 0;
    }
    has_guard = xfcGetOwn(candidate, "guard", &guard);
    has_legacy_guard = xfcGetOwn(candidate, "cond", &legacy_guard);
    if (has_guard && has_legacy_guard) {
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, path, "guard", 0);
    } else if (has_legacy_guard) {
      guard = legacy_guard;
      legacy_guard = 0;
    }
    if (guard && !jsvIsUndefined(guard))
      guard_path = xfcPathProperty(path,
                                   has_guard ? "guard" : "cond");
    if (xfcGetOwn(candidate, "actions", &actions))
      actions_path = xfcPathProperty(path, "actions");
    if (!xfcReadReenter(compiler, candidate, path, &reenter)) goto done;
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
    goto done;
  }
  if (compiler->diagnostic != XFC_DIAG_NONE) goto done;
  if (target) {
    if (!target_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    record.target_state = xfcResolveTarget(compiler, source, target,
                                           target_path);
    if (compiler->diagnostic != XFC_DIAG_NONE) goto done;
  }
  if (guard && !jsvIsUndefined(guard)) {
    if (!guard_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileGuard(compiler, guard, guard_path, &record.guard))
      goto done;
  }
  if (actions) {
    if (!actions_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileActions(compiler, actions, actions_path, &record.actions))
      goto done;
  }
  if (reenter) record.flags |= XFC_TRANSITION_REENTER;
  record.domain_state = xfcTransitionDomain(
      compiler, source, record.target_state, reenter);
  if (compiler->phase == XFC_COMPILE_COUNT) {
    if (!xfcIncrement(compiler, &compiler->counts.transitions, path,
                      "transitions"))
      goto done;
  } else {
    index = compiler->writer->transition++;
    if (!xfcWriteRecord(compiler->writer, XFC_TABLE_TRANSITION, index,
                        &record, sizeof(record)))
      xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
  }

done:
  jsvUnLock(target);
  jsvUnLock(actions);
  jsvUnLock(guard);
  jsvUnLock(legacy_guard);
  jsvUnLock(value);
  jsvUnLock(target_path);
  jsvUnLock(actions_path);
  jsvUnLock(guard_path);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcCompileCandidates(XfcCompiler *compiler, uint16_t source,
                                 JsVar *definition, JsVar *path,
                                 XfcRange *range) {
  uint16_t first = compiler->phase == XFC_COMPILE_COUNT
                       ? (uint16_t)compiler->counts.transitions
                       : compiler->writer->transition;
  uint16_t count = 0;
  if (jsvIsArray(definition)) {
    JsVarInt length = jsvGetArrayLength(definition);
    JsVarInt index;
    if (length == 0) {
      xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
      return false;
    }
    for (index = 0; index < length &&
                    compiler->diagnostic == XFC_DIAG_NONE;
         index++) {
      JsVar *candidate = jsvGetArrayItem(definition, index);
      JsVar *candidate_path = xfcPathIndex(path, index);
      if (!candidate_path)
        xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      else if (xfcCompileTransition(compiler, source, candidate,
                                    candidate_path))
        count++;
      jsvUnLock2(candidate, candidate_path);
    }
  } else if (xfcCompileTransition(compiler, source, definition, path)) {
    count = 1;
  }
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  range->first = first;
  range->count = count;
  return true;
}

static bool xfcCompileHandlers(XfcCompiler *compiler, uint16_t state_index,
                               JsVar *on, JsVar *path, XfcRange *range) {
  uint16_t first = compiler->phase == XFC_COMPILE_COUNT
                       ? (uint16_t)compiler->counts.handlers
                       : compiler->writer->handler;
  uint16_t count = 0;
  JsvObjectIterator iterator;
  range->first = XFC_INDEX_NONE;
  range->count = 0;
  if (!on || jsvIsUndefined(on)) return true;
  jsvObjectIteratorNew(&iterator, on);
  while (jsvObjectIteratorHasValue(&iterator) &&
         compiler->diagnostic == XFC_DIAG_NONE) {
    JsVar *source_key = jsvObjectIteratorGetKey(&iterator);
    JsVar *definition = jsvObjectIteratorGetValue(&iterator);
    if (!jsvIsInternalObjectKey(source_key)) {
      JsVar *event = xfcCopyString(source_key);
      JsVar *event_path = event ? xfcPathKey(path, event) : 0;
      XfcHandlerRecord record;
      uint16_t handler_index;
      memset(&record, 0, sizeof(record));
      if (jsvIsGetterOrSetter(source_key) || !event || !event_path) {
        xfcFail(compiler, event && event_path ? XFC_DIAG_CONFIG_TYPE
                                              : XFC_DIAG_NO_MEMORY,
                event_path, 0);
      } else if (jsvGetStringLength(event) == 0) {
        xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, event_path, 0);
      } else if (jsvIsStringEqual(event, "*")) {
        record.event_symbol = XFC_INDEX_NONE;
        record.flags = XFC_HANDLER_WILDCARD;
      } else if (xfcStringHasByte(event, '*')) {
        xfcFail(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, event_path, event);
      } else {
        record.event_symbol =
            xfcInternSymbol(compiler, event, XFC_SYMBOL_EVENT, event_path);
      }
      if (compiler->diagnostic == XFC_DIAG_NONE &&
          xfcCompileCandidates(compiler, state_index, definition, event_path,
                               &record.transitions)) {
        if (compiler->phase == XFC_COMPILE_COUNT) {
          if (xfcIncrement(compiler, &compiler->counts.handlers, event_path,
                           "handlers"))
            count++;
        } else {
          handler_index = compiler->writer->handler++;
          if (!xfcWriteRecord(compiler->writer, XFC_TABLE_HANDLER,
                              handler_index, &record, sizeof(record)))
            xfcFail(compiler, XFC_DIAG_INTERNAL, event_path, 0);
          else
            count++;
        }
      }
      jsvUnLock2(event, event_path);
    }
    jsvUnLock2(source_key, definition);
    jsvObjectIteratorNext(&iterator);
  }
  jsvObjectIteratorFree(&iterator);
  if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  range->first = count == 0 ? XFC_INDEX_NONE : first;
  range->count = count;
  return true;
}

static bool xfcCompileInitial(XfcCompiler *compiler, uint16_t state_index,
                              JsVar *configuration, JsVar *path,
                              uint16_t *initial_index,
                              XfcRange *initial_actions) {
  static const char *const allowed[] = {
      "target", "actions", "description", "meta"};
  JsVar *initial = 0;
  JsVar *target = 0;
  JsVar *actions = 0;
  JsVar *value = 0;
  JsVar *initial_path = xfcPathProperty(path, "initial");
  JsVar *target_path = 0;
  JsVar *actions_path = 0;
  uint16_t resolved = XFC_INDEX_NONE;
  bool ok = false;
  initial_actions->first = XFC_INDEX_NONE;
  initial_actions->count = 0;
  *initial_index = XFC_INDEX_NONE;
  if (!initial_path) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  if (!xfcGetOwn(configuration, "initial", &initial) ||
      jsvIsUndefined(initial)) {
    xfcFail(compiler, XFC_DIAG_INITIAL_REQUIRED, initial_path, 0);
    goto done;
  }
  if (jsvIsString(initial)) {
    target = jsvLockAgain(initial);
    target_path = jsvLockAgain(initial_path);
  } else if (xfcIsObject(initial)) {
    if (!xfcValidateProperties(compiler, initial, initial_path, allowed,
                               sizeof(allowed) / sizeof(allowed[0])))
      goto done;
    if (!xfcGetOwn(initial, "target", &target) || !jsvIsString(target)) {
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, initial_path, "target",
                      0);
      goto done;
    }
    target_path = xfcPathProperty(initial_path, "target");
    if (xfcGetOwn(initial, "actions", &actions))
      actions_path = xfcPathProperty(initial_path, "actions");
    if (xfcGetOwn(initial, "description", &value) &&
        !jsvIsUndefined(value) && !jsvIsString(value))
      xfcFailProperty(compiler, XFC_DIAG_CONFIG_TYPE, initial_path,
                      "description", 0);
    jsvUnLock(value);
    value = 0;
    if (xfcGetOwn(initial, "meta", &value) && !jsvIsUndefined(value) &&
        (!xfcIsObject(value) || xfcVisibleChildren(value) != 0))
      xfcFailProperty(compiler, XFC_DIAG_UNSUPPORTED_FEATURE, initial_path,
                      "meta", 0);
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, initial_path, 0);
    goto done;
  }
  if (compiler->diagnostic != XFC_DIAG_NONE) goto done;
  if (!target_path) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  resolved = xfcFindDirectChild(compiler, state_index, target);
  if (resolved == XFC_INDEX_NONE) {
    xfcFail(compiler, XFC_DIAG_INITIAL_UNKNOWN, target_path, target);
    goto done;
  }
  if (actions) {
    if (!actions_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileActions(compiler, actions, actions_path, initial_actions))
      goto done;
  }
  *initial_index = resolved;
  ok = true;

done:
  jsvUnLock(initial);
  jsvUnLock(target);
  jsvUnLock(actions);
  jsvUnLock(value);
  jsvUnLock(initial_path);
  jsvUnLock(target_path);
  jsvUnLock(actions_path);
  return ok && compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcCompileState(XfcCompiler *compiler, uint16_t state_index) {
  JsVar *meta = xfcStateMeta(compiler, state_index);
  JsVar *configuration = xfcMetaGet(meta, XFC_META_CONFIG);
  JsVar *key = xfcMetaGet(meta, XFC_META_KEY);
  JsVar *path = xfcMetaGet(meta, XFC_META_PATH);
  JsVar *completion = xfcMetaGet(meta, XFC_META_COMPLETION_SYMBOL);
  JsVar *entry = 0;
  JsVar *exit = 0;
  JsVar *on = 0;
  JsVar *entry_path = 0;
  JsVar *exit_path = 0;
  JsVar *on_path = 0;
  XfcStateRecord record;
  int parent = xfcMetaInteger(meta, XFC_META_PARENT);
  int depth = xfcMetaInteger(meta, XFC_META_DEPTH);
  int type = xfcMetaInteger(meta, XFC_META_TYPE);
  bool ok = false;
  memset(&record, 0, sizeof(record));
  record.parent = parent < 0 ? XFC_INDEX_NONE : (uint16_t)parent;
  record.initial = XFC_INDEX_NONE;
  record.key_symbol = XFC_INDEX_NONE;
  record.completion_event_symbol = XFC_INDEX_NONE;
  record.handlers.first = XFC_INDEX_NONE;
  record.completion_transitions.first = XFC_INDEX_NONE;
  record.initial_actions.first = XFC_INDEX_NONE;
  record.entry_actions.first = XFC_INDEX_NONE;
  record.exit_actions.first = XFC_INDEX_NONE;
  record.flags = type == XFC_NODE_COMPOUND ? XFC_STATE_COMPOUND
                                           : XFC_STATE_ATOMIC;
  if (state_index == 0) record.flags |= XFC_STATE_ROOT;
  record.depth = (uint8_t)depth;
  if (state_index != 0) {
    record.key_symbol =
        xfcInternSymbol(compiler, key, XFC_SYMBOL_STATE_KEY, path);
    if (record.key_symbol == XFC_INDEX_NONE) goto done;
  }
  if (type == XFC_NODE_COMPOUND) {
    if (!xfcCompileInitial(compiler, state_index, configuration, path,
                           &record.initial, &record.initial_actions))
      goto done;
    if (state_index != 0) {
      record.completion_event_symbol = xfcInternSymbol(
          compiler, completion,
          XFC_SYMBOL_EVENT | XFC_SYMBOL_COMPLETION_EVENT, path);
      if (record.completion_event_symbol == XFC_INDEX_NONE) goto done;
    }
  }
  if (xfcGetOwn(configuration, "entry", &entry)) {
    entry_path = xfcPathProperty(path, "entry");
    if (!entry_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileActions(compiler, entry, entry_path,
                           &record.entry_actions))
      goto done;
  }
  if (xfcGetOwn(configuration, "exit", &exit)) {
    exit_path = xfcPathProperty(path, "exit");
    if (!exit_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileActions(compiler, exit, exit_path, &record.exit_actions))
      goto done;
  }
  if (xfcGetOwn(configuration, "on", &on)) {
    on_path = xfcPathProperty(path, "on");
    if (!on_path) {
      xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
      goto done;
    }
    if (!xfcCompileHandlers(compiler, state_index, on, on_path,
                            &record.handlers))
      goto done;
  }
  if (compiler->phase == XFC_COMPILE_EMIT &&
      !xfcWriteRecord(compiler->writer, XFC_TABLE_STATE, state_index,
                      &record, sizeof(record))) {
    xfcFail(compiler, XFC_DIAG_INTERNAL, path, 0);
    goto done;
  }
  ok = true;

done:
  jsvUnLock(meta);
  jsvUnLock(configuration);
  jsvUnLock(key);
  jsvUnLock(path);
  jsvUnLock(completion);
  jsvUnLock(entry);
  jsvUnLock(exit);
  jsvUnLock(on);
  jsvUnLock(entry_path);
  jsvUnLock(exit_path);
  jsvUnLock(on_path);
  return ok && compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcCompileAllStates(XfcCompiler *compiler) {
  uint16_t index;
  for (index = 0; index < (uint16_t)compiler->counts.states; index++) {
    if (!xfcCompileState(compiler, index)) return false;
  }
  return true;
}

static bool xfcCompileContext(XfcCompiler *compiler, JsVar *config,
                              JsVar *config_path) {
  JsVar *context = 0;
  JsVar *path = 0;
  compiler->context_kind = XFC_CONTEXT_OMITTED;
  compiler->context_slot = XFC_INDEX_NONE;
  if (!xfcGetOwn(config, "context", &context)) return true;
  path = xfcPathProperty(config_path, "context");
  if (!path) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
  } else if (jsvIsFunction(context)) {
    compiler->context_kind = XFC_CONTEXT_FACTORY;
    compiler->context_slot = xfcRetain(compiler, context, path);
  } else if (xfcIsObject(context)) {
    compiler->context_kind = XFC_CONTEXT_LITERAL;
    compiler->context_slot = xfcRetain(compiler, context, path);
  } else {
    xfcFail(compiler, XFC_DIAG_CONFIG_TYPE, path, 0);
  }
  jsvUnLock2(context, path);
  return compiler->diagnostic == XFC_DIAG_NONE;
}

static bool xfcBuildHeader(XfcCompiler *compiler, XfcArenaHeader *header) {
  static const uint16_t sizes[XFC_TABLE_COUNT] = {
      (uint16_t)sizeof(XfcStateRecord),
      (uint16_t)sizeof(XfcSymbolRecord),
      (uint16_t)sizeof(XfcHandlerRecord),
      (uint16_t)sizeof(XfcTransitionRecord),
      (uint16_t)sizeof(XfcGuardRecord),
      (uint16_t)sizeof(XfcActionRecord),
      (uint16_t)sizeof(XfcAssignmentRecord),
      (uint16_t)sizeof(XfcAssignmentEntryRecord)};
  const uint32_t counts[XFC_TABLE_COUNT] = {
      compiler->counts.states,
      compiler->counts.symbols,
      compiler->counts.handlers,
      compiler->counts.transitions,
      compiler->counts.guards,
      compiler->counts.actions,
      compiler->counts.assignments,
      compiler->counts.assignment_entries};
  uint32_t cursor = XFC_ARENA_HEADER_SIZE;
  uint16_t table;
  memset(header, 0, sizeof(*header));
  memcpy(header->magic, "XFCM", 4);
  header->format_version = XFC_ARENA_FORMAT_VERSION;
  header->header_size = XFC_ARENA_HEADER_SIZE;
  header->flags = xfcNativeByteOrderFlag();
  header->root_state = 0;
  header->context_slot = compiler->context_slot;
  header->retained_count =
      (uint16_t)jsvGetArrayLength(compiler->retained);
  header->context_kind = compiler->context_kind;
  for (table = 0; table < XFC_TABLE_COUNT; table++) {
    uint32_t bytes;
    header->tables[table].count = (uint16_t)counts[table];
    header->tables[table].record_size = sizes[table];
    if (counts[table] == 0) continue;
    if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &cursor) ||
        !xfcCheckedMultiplyU32(counts[table], sizes[table], &bytes))
      return false;
    header->tables[table].offset = cursor;
    if (!xfcCheckedAddU32(cursor, bytes, &cursor)) return false;
  }
  if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &cursor)) return false;
  header->string_offset = cursor;
  header->string_size = compiler->counts.string_bytes;
  if (!xfcCheckedAddU32(cursor, header->string_size, &header->arena_size) ||
      header->arena_size > (uint32_t)UINT_MAX)
    return false;
  return true;
}

static bool xfcWriteSymbols(XfcCompiler *compiler, XfcArenaWriter *writer) {
  uint32_t string_cursor = writer->header.string_offset;
  uint16_t index;
  for (index = 0; index < writer->header.tables[XFC_TABLE_SYMBOL].count;
       index++) {
    JsVar *entry = jsvGetArrayItem(compiler->symbols, index);
    JsVar *text = jsvGetArrayItem(entry, 0);
    JsVar *flags = jsvGetArrayItem(entry, 1);
    size_t length = jsvGetStringLength(text);
    XfcSymbolRecord record;
    memset(&record, 0, sizeof(record));
    record.string_offset = string_cursor;
    record.byte_length = (uint16_t)length;
    record.flags = (uint16_t)jsvGetInteger(flags);
    if (jsvGetStringChars(text, 0, (char *)writer->bytes + string_cursor,
                          length) != length) {
      xfcFail(compiler, XFC_DIAG_INTERNAL, 0, 0);
    } else {
      record.hash = xfcFnv1a(writer->bytes + string_cursor, length);
      if (!xfcWriteRecord(writer, XFC_TABLE_SYMBOL, index, &record,
                          sizeof(record)))
        xfcFail(compiler, XFC_DIAG_INTERNAL, 0, 0);
      string_cursor += (uint32_t)length;
    }
    jsvUnLock3(entry, text, flags);
    if (compiler->diagnostic != XFC_DIAG_NONE) return false;
  }
  if (string_cursor != writer->header.arena_size) {
    xfcFail(compiler, XFC_DIAG_INTERNAL, 0, 0);
    return false;
  }
  return true;
}

static bool xfcWriterComplete(const XfcArenaWriter *writer) {
  return writer->handler == writer->header.tables[XFC_TABLE_HANDLER].count &&
         writer->transition ==
             writer->header.tables[XFC_TABLE_TRANSITION].count &&
         writer->guard == writer->header.tables[XFC_TABLE_GUARD].count &&
         writer->action == writer->header.tables[XFC_TABLE_ACTION].count &&
         writer->assignment ==
             writer->header.tables[XFC_TABLE_ASSIGNMENT].count &&
         writer->assignment_entry ==
             writer->header.tables[XFC_TABLE_ASSIGNMENT_ENTRY].count;
}

static JsVar *xfcPublishMachine(XfcCompiler *compiler, JsVar *arena) {
  JsVar *machine = jsvNewObject();
  if (!machine ||
      jsvObjectSetChild(machine, XFC_MACHINE_ARENA_NAME, arena) != arena ||
      jsvObjectSetChild(machine, XFC_MACHINE_RETAINED_NAME,
                        compiler->retained) != compiler->retained) {
    xfcFail(compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    jsvUnLock(machine);
    return 0;
  }
  return machine;
}

JsVar *xfcCompileMachine(JsVar *config, JsVar *options) {
  XfcCompiler compiler;
  XfcArenaWriter writer;
  JsVar *config_path = 0;
  JsVar *options_path = 0;
  JsVar *arena = 0;
  JsVar *machine = 0;
  memset(&compiler, 0, sizeof(compiler));
  memset(&writer, 0, sizeof(writer));
  compiler.phase = XFC_COMPILE_COUNT;
  compiler.context_slot = XFC_INDEX_NONE;
  config_path = jsvNewFromString("config");
  options_path = jsvNewFromString("options");
  compiler.states = jsvNewEmptyArray();
  compiler.symbols = jsvNewEmptyArray();
  compiler.retained = jsvNewEmptyArray();
  if (!config_path || !options_path || !compiler.states || !compiler.symbols ||
      !compiler.retained) {
    xfcFail(&compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  if (!xfcIsObject(config)) {
    xfcFail(&compiler, XFC_DIAG_CONFIG_TYPE, config_path, 0);
    goto done;
  }
  if (!xfcValidateOptions(&compiler, options, options_path) ||
      !xfcEnumerateStates(&compiler, config, config_path) ||
      !xfcCompileContext(&compiler, config, config_path) ||
      !xfcCompileAllStates(&compiler))
    goto done;
  if (!xfcBuildHeader(&compiler, &writer.header)) {
    xfcFail(&compiler, XFC_DIAG_LIMIT_EXCEEDED, config_path, 0);
    goto done;
  }
  arena = jsvNewFlatStringOfLength((unsigned int)writer.header.arena_size);
  if (!arena) {
    xfcFail(&compiler, XFC_DIAG_NO_MEMORY, 0, 0);
    goto done;
  }
  writer.bytes = (uint8_t *)jsvGetFlatStringPointer(arena);
  if (!writer.bytes) {
    xfcFail(&compiler, XFC_DIAG_INTERNAL, config_path, 0);
    goto done;
  }
  memset(writer.bytes, 0, writer.header.arena_size);
  memcpy(writer.bytes, &writer.header, sizeof(writer.header));
  if (!xfcWriteSymbols(&compiler, &writer)) goto done;
  compiler.phase = XFC_COMPILE_EMIT;
  compiler.writer = &writer;
  if (!xfcCompileAllStates(&compiler)) goto done;
  if (!xfcWriterComplete(&writer)) {
    xfcFail(&compiler, XFC_DIAG_INTERNAL, config_path, 0);
    goto done;
  }
  if (xfcValidateArena(writer.bytes, writer.header.arena_size) !=
      XFC_VALIDATION_OK) {
    xfcFail(&compiler, XFC_DIAG_INTERNAL, config_path, 0);
    goto done;
  }
  machine = xfcPublishMachine(&compiler, arena);

done:
  if (!machine && compiler.diagnostic == XFC_DIAG_NONE)
    xfcFail(&compiler, XFC_DIAG_INTERNAL, config_path, 0);
  if (!machine) xfcThrowFailure(&compiler);
  jsvUnLock(config_path);
  jsvUnLock(options_path);
  jsvUnLock(arena);
  jsvUnLock(compiler.states);
  jsvUnLock(compiler.symbols);
  jsvUnLock(compiler.retained);
  jsvUnLock(compiler.actions_map);
  jsvUnLock(compiler.guards_map);
  jsvUnLock(compiler.error_path);
  jsvUnLock(compiler.error_detail);
  return machine;
}

JsVar *xfcCreateAssignmentDescriptor(JsVar *assignment) {
  JsVar *descriptor = 0;
  JsVar *brand = 0;
  bool valid = jsvIsFunction(assignment) || xfcIsObject(assignment);
  if (valid && xfcIsObject(assignment)) {
    JsvObjectIterator iterator;
    jsvObjectIteratorNew(&iterator, assignment);
    while (jsvObjectIteratorHasValue(&iterator)) {
      JsVar *key = jsvObjectIteratorGetKey(&iterator);
      if (!jsvIsInternalObjectKey(key) && jsvIsGetterOrSetter(key))
        valid = false;
      jsvUnLock(key);
      if (!valid) break;
      jsvObjectIteratorNext(&iterator);
    }
    jsvObjectIteratorFree(&iterator);
  }
  if (!valid) {
    jsExceptionHere(JSET_ERROR, "XFC E_CONFIG_TYPE @ assign.assignment");
    return 0;
  }
  descriptor = jsvNewObject();
  brand = jsvNewFromString(XFC_ASSIGN_BRAND);
  if (!descriptor || !brand ||
      jsvObjectSetChild(descriptor, XFC_ASSIGN_BRAND_NAME, brand) != brand ||
      jsvObjectSetChild(descriptor, XFC_ASSIGN_VALUE_NAME, assignment) !=
          assignment) {
    jsvUnLock(descriptor);
    descriptor = 0;
    jsExceptionHere(JSET_ERROR, "XFC E_NO_MEMORY @ assign.assignment");
  }
  jsvUnLock(brand);
  return descriptor;
}
