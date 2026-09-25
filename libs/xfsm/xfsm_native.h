/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef XFSM_NATIVE_H
#define XFSM_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XFC_ARENA_FORMAT_VERSION UINT16_C(1)
#define XFC_ARENA_HEADER_SIZE UINT16_C(96)
#define XFC_ACTOR_DATA_SIZE ((size_t)16)
#define XFC_MAX_STATE_DEPTH UINT8_C(32)

typedef uint16_t XfcIndex;

#define XFC_INDEX_NONE UINT16_C(0xFFFF)

typedef struct {
  uint16_t first;
  uint16_t count;
} XfcRange;

typedef struct {
  uint32_t offset;
  uint16_t count;
  uint16_t record_size;
} XfcTableRef;

enum {
  XFC_TABLE_STATE = 0,
  XFC_TABLE_SYMBOL,
  XFC_TABLE_HANDLER,
  XFC_TABLE_TRANSITION,
  XFC_TABLE_GUARD,
  XFC_TABLE_ACTION,
  XFC_TABLE_ASSIGNMENT,
  XFC_TABLE_ASSIGNMENT_ENTRY,
  XFC_TABLE_COUNT
};

enum {
  XFC_ARENA_LITTLE_ENDIAN = UINT32_C(0x00000001),
  XFC_ARENA_BIG_ENDIAN = UINT32_C(0x00000002),
  XFC_ARENA_KNOWN_FLAGS = UINT32_C(0x00000003)
};

enum {
  XFC_CONTEXT_OMITTED = 0,
  XFC_CONTEXT_LITERAL = 1,
  XFC_CONTEXT_FACTORY = 2
};

typedef struct {
  uint8_t magic[4];
  uint16_t format_version;
  uint16_t header_size;
  uint32_t arena_size;
  uint32_t flags;
  uint16_t root_state;
  uint16_t context_slot;
  uint16_t retained_count;
  uint8_t context_kind;
  uint8_t reserved;
  uint32_t string_offset;
  uint32_t string_size;
  XfcTableRef tables[XFC_TABLE_COUNT];
} XfcArenaHeader;

enum {
  XFC_STATE_ATOMIC = UINT16_C(0x0000),
  XFC_STATE_COMPOUND = UINT16_C(0x0001),
  XFC_STATE_FINAL = UINT16_C(0x0002),
  XFC_STATE_TYPE_MASK = UINT16_C(0x0003),
  XFC_STATE_ROOT = UINT16_C(0x0004),
  XFC_STATE_KNOWN_FLAGS = UINT16_C(0x0007)
};

typedef struct {
  uint16_t parent;
  uint16_t initial;
  uint16_t key_symbol;
  uint16_t completion_event_symbol;
  XfcRange handlers;
  XfcRange completion_transitions;
  XfcRange initial_actions;
  XfcRange entry_actions;
  XfcRange exit_actions;
  uint16_t flags;
  uint8_t depth;
  uint8_t reserved;
} XfcStateRecord;

enum {
  XFC_SYMBOL_STATE_KEY = UINT16_C(0x0001),
  XFC_SYMBOL_EVENT = UINT16_C(0x0002),
  XFC_SYMBOL_ACTION = UINT16_C(0x0004),
  XFC_SYMBOL_GUARD = UINT16_C(0x0008),
  XFC_SYMBOL_CONTEXT_KEY = UINT16_C(0x0010),
  XFC_SYMBOL_COMPLETION_EVENT = UINT16_C(0x0020),
  XFC_SYMBOL_KNOWN_FLAGS = UINT16_C(0x003F)
};

typedef struct {
  uint32_t hash;
  uint32_t string_offset;
  uint16_t byte_length;
  uint16_t flags;
} XfcSymbolRecord;

enum {
  XFC_HANDLER_WILDCARD = UINT16_C(0x0001),
  XFC_HANDLER_KNOWN_FLAGS = UINT16_C(0x0001)
};

typedef struct {
  uint16_t event_symbol;
  XfcRange transitions;
  uint16_t flags;
} XfcHandlerRecord;

enum {
  XFC_TRANSITION_REENTER = UINT16_C(0x0001),
  XFC_TRANSITION_KNOWN_FLAGS = UINT16_C(0x0001)
};

typedef struct {
  uint16_t target_state;
  uint16_t guard;
  XfcRange actions;
  uint16_t flags;
  uint16_t domain_state;
} XfcTransitionRecord;

typedef struct {
  uint16_t retained_slot;
  uint16_t flags;
} XfcGuardRecord;

enum {
  XFC_ACTION_USER = 0,
  XFC_ACTION_ASSIGN = 1
};

typedef struct {
  uint16_t kind;
  uint16_t reference;
  uint16_t flags;
  uint16_t reserved;
} XfcActionRecord;

enum {
  XFC_ASSIGN_PARTIAL = 0,
  XFC_ASSIGN_PROPERTY_MAP = 1
};

typedef struct {
  uint16_t kind;
  uint16_t retained_slot;
  XfcRange entries;
  uint16_t flags;
  uint16_t reserved;
} XfcAssignmentRecord;

enum {
  XFC_ASSIGN_ENTRY_LITERAL = UINT16_C(0x0000),
  XFC_ASSIGN_ENTRY_EXPRESSION = UINT16_C(0x0001),
  XFC_ASSIGN_ENTRY_KNOWN_FLAGS = UINT16_C(0x0001)
};

typedef struct {
  uint16_t key_symbol;
  uint16_t retained_slot;
  uint16_t flags;
  uint16_t reserved;
} XfcAssignmentEntryRecord;

enum {
  XFC_ACTOR_NOT_STARTED = 0,
  XFC_ACTOR_ACTIVE = 1,
  XFC_ACTOR_DONE = 2,
  XFC_ACTOR_STOPPED = 3,
  XFC_ACTOR_ERROR = 4
};

enum {
  XFC_OPERATION_IDLE = 0,
  XFC_OPERATION_START = 1,
  XFC_OPERATION_SEND = 2,
  XFC_OPERATION_STOP = 3,
  XFC_OPERATION_NOTIFY = 4
};

typedef struct {
  uint8_t magic[4];
  uint16_t format_version;
  uint8_t status;
  uint8_t operation;
  uint16_t leaf_state;
  uint16_t microsteps;
  uint16_t flags;
  uint16_t reserved;
} XfcActorData;

typedef enum {
  XFC_VALIDATION_OK = 0,
  XFC_VALIDATION_NULL,
  XFC_VALIDATION_ALIGNMENT,
  XFC_VALIDATION_SIZE,
  XFC_VALIDATION_MAGIC,
  XFC_VALIDATION_VERSION,
  XFC_VALIDATION_ENDIAN,
  XFC_VALIDATION_FLAGS,
  XFC_VALIDATION_RESERVED,
  XFC_VALIDATION_TABLE,
  XFC_VALIDATION_PADDING,
  XFC_VALIDATION_STRING,
  XFC_VALIDATION_RANGE,
  XFC_VALIDATION_INDEX,
  XFC_VALIDATION_ROOT,
  XFC_VALIDATION_STATE,
  XFC_VALIDATION_SYMBOL,
  XFC_VALIDATION_HANDLER,
  XFC_VALIDATION_TRANSITION,
  XFC_VALIDATION_GUARD,
  XFC_VALIDATION_ACTION,
  XFC_VALIDATION_ASSIGNMENT,
  XFC_VALIDATION_ASSIGNMENT_ENTRY,
  XFC_VALIDATION_DOMAIN,
  XFC_VALIDATION_ACTOR
} XfcValidationResult;

bool xfcCheckedAddU32(uint32_t left, uint32_t right, uint32_t *result);
bool xfcCheckedMultiplyU32(uint32_t left, uint32_t right, uint32_t *result);
bool xfcCheckedAlignU32(uint32_t value, uint32_t alignment,
                        uint32_t *result);
bool xfcRangeIsValid(XfcRange range, uint16_t table_count);

uint32_t xfcNativeByteOrderFlag(void);
uint32_t xfcFnv1a(const uint8_t *bytes, size_t byte_length);
bool xfcHashedBytesEqual(uint32_t left_hash, const uint8_t *left,
                         uint16_t left_length, uint32_t right_hash,
                         const uint8_t *right, uint16_t right_length);

XfcValidationResult xfcValidateArena(const void *arena, size_t arena_length);
bool xfcArenaSymbolMatches(const void *arena, size_t arena_length,
                           XfcIndex symbol_index, const uint8_t *bytes,
                           uint16_t byte_length, uint32_t hash);

void xfcActorDataInit(XfcActorData *actor);
XfcValidationResult xfcValidateActorData(const XfcActorData *actor,
                                         uint16_t state_count);

const char *xfcValidationResultName(XfcValidationResult result);

#endif
