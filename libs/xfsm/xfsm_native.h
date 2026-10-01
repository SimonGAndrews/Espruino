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

/*
 * Physical format shared by the compiler, runtime, and sanitizer tests.
 *
 * The arena is the compiled machine data block. It is one Espruino-owned flat
 * string containing a header, record tables, and stored strings. Records refer
 * to each other with 16-bit indexes; XFC_INDEX_NONE means no index. The data
 * uses the processor's byte order and must match the firmware format version.
 * A retained slot is an index into the JavaScript values stored on the machine.
 */
#define XFC_ARENA_FORMAT_VERSION UINT16_C(1)
#define XFC_ARENA_HEADER_SIZE UINT16_C(96) /* Bytes in XfcArenaHeader. */
#define XFC_ACTOR_DATA_SIZE ((size_t)16) /* Bytes in XfcActorData. */
#define XFC_MAX_STATE_DEPTH UINT8_C(32) /* Root depth is zero. */

typedef uint16_t XfcIndex;

/* All valid table indexes are below this value. */
#define XFC_INDEX_NONE UINT16_C(0xFFFF)

/* Empty ranges use { XFC_INDEX_NONE, 0 }; non-empty ranges are contiguous. */
typedef struct {
  uint16_t first; /* First table index, or XFC_INDEX_NONE when empty. */
  uint16_t count; /* Number of consecutive records. */
} XfcRange;

/* Offset is measured from the first arena byte; record_size is in bytes. */
typedef struct {
  uint32_t offset;     /* Arena byte offset, or zero for an empty table. */
  uint16_t count;      /* Number of records in this table. */
  uint16_t record_size; /* Firmware-expected record size in bytes. */
} XfcTableRef;

/* Physical table order is part of arena format version 1. */
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

/* Exactly one native-byte-order bit must be present. */
enum {
  XFC_ARENA_LITTLE_ENDIAN = UINT32_C(0x00000001),
  XFC_ARENA_BIG_ENDIAN = UINT32_C(0x00000002),
  XFC_ARENA_KNOWN_FLAGS = UINT32_C(0x00000003)
};

/* Determines how actor.start() obtains its initial context. */
enum {
  XFC_CONTEXT_OMITTED = 0,
  XFC_CONTEXT_LITERAL = 1,
  XFC_CONTEXT_FACTORY = 2
};

/* Header at the start of the arena describing all compiled machine data. */
typedef struct {
  uint8_t magic[4];       /* "XFCM" identifies a compiled machine. */
  uint16_t format_version; /* XFC_ARENA_FORMAT_VERSION. */
  uint16_t header_size;   /* Enables rejection before table access. */
  uint32_t arena_size;    /* Complete flat-string length in bytes. */
  uint32_t flags;         /* Native byte order; all other bits are zero. */
  uint16_t root_state;    /* State-table index, currently always zero. */
  uint16_t context_slot;  /* Retained slot, or XFC_INDEX_NONE. */
  uint16_t retained_count; /* Bounds every retained-value reference. */
  uint8_t context_kind;   /* One of XFC_CONTEXT_*. */
  uint8_t reserved;       /* Must be zero in format version 1. */
  uint32_t string_offset; /* First byte of the stored string area. */
  uint32_t string_size;   /* String-pool length in bytes. */
  XfcTableRef tables[XFC_TABLE_COUNT]; /* Indexed by XFC_TABLE_*. */
} XfcArenaHeader;

/* State type occupies XFC_STATE_TYPE_MASK; remaining bits are properties. */
enum {
  XFC_STATE_ATOMIC = UINT16_C(0x0000),
  XFC_STATE_COMPOUND = UINT16_C(0x0001),
  XFC_STATE_FINAL = UINT16_C(0x0002),
  XFC_STATE_TYPE_MASK = UINT16_C(0x0003),
  XFC_STATE_ROOT = UINT16_C(0x0004),
  XFC_STATE_KNOWN_FLAGS = UINT16_C(0x0007)
};

/* One state node; all relationships and action lists are arena indexes. */
typedef struct {
  uint16_t parent; /* Parent state, or XFC_INDEX_NONE for the root. */
  uint16_t initial; /* Initial child, or XFC_INDEX_NONE if not compound. */
  uint16_t key_symbol; /* Local state-key symbol; absent on the root. */
  uint16_t completion_event_symbol; /* Compound done event, if applicable. */
  XfcRange handlers; /* Event handlers declared by this state. */
  XfcRange completion_transitions; /* Normalized onDone candidates. */
  XfcRange initial_actions; /* Actions on the initial transition. */
  XfcRange entry_actions;
  XfcRange exit_actions;
  uint16_t flags; /* XFC_STATE_* type and root flags. */
  uint8_t depth;  /* Root is zero; bounded by XFC_MAX_STATE_DEPTH. */
  uint8_t reserved; /* Must be zero. */
} XfcStateRecord;

/* A string stored once may serve several roles in the machine. */
enum {
  XFC_SYMBOL_STATE_KEY = UINT16_C(0x0001),
  XFC_SYMBOL_EVENT = UINT16_C(0x0002),
  XFC_SYMBOL_ACTION = UINT16_C(0x0004),
  XFC_SYMBOL_GUARD = UINT16_C(0x0008),
  XFC_SYMBOL_CONTEXT_KEY = UINT16_C(0x0010),
  XFC_SYMBOL_COMPLETION_EVENT = UINT16_C(0x0020),
  XFC_SYMBOL_KNOWN_FLAGS = UINT16_C(0x003F)
};

/* One UTF-8 string stored in the arena and its uses within the machine. */
typedef struct {
  uint32_t hash;          /* FNV-1a over the exact pooled bytes. */
  uint32_t string_offset; /* Arena byte offset into the string pool. */
  uint16_t byte_length;   /* Byte length; no terminating NUL is stored. */
  uint16_t flags;         /* One or more XFC_SYMBOL_* roles. */
} XfcSymbolRecord;

/* Handler flags distinguish the wildcard entry from symbol-backed events. */
enum {
  XFC_HANDLER_WILDCARD = UINT16_C(0x0001),
  XFC_HANDLER_KNOWN_FLAGS = UINT16_C(0x0001)
};

/* Event handler and transition candidates occupy contiguous table ranges. */
typedef struct {
  uint16_t event_symbol; /* Event symbol, or NONE for a wildcard. */
  XfcRange transitions;  /* Ordered candidates for this handler. */
  uint16_t flags;        /* XFC_HANDLER_* flags. */
} XfcHandlerRecord;

/* The re-entry choice is normalized while compiling v4 and v5 syntax. */
enum {
  XFC_TRANSITION_REENTER = UINT16_C(0x0001),
  XFC_TRANSITION_KNOWN_FLAGS = UINT16_C(0x0001)
};

/* domain_state is the compile-time exclusive exit and entry boundary. */
typedef struct {
  uint16_t target_state; /* Resolved state, or NONE when targetless. */
  uint16_t guard;        /* Guard-table index, or NONE. */
  XfcRange actions;      /* Ordered transition actions. */
  uint16_t flags;        /* Stored XFC_TRANSITION_* flags. */
  uint16_t domain_state; /* Exclusive boundary, or outside-root NONE. */
} XfcTransitionRecord;

/* Guard callback slot in the machine's GC-owned retained-value array. */
typedef struct {
  uint16_t retained_slot; /* JavaScript guard function. */
  uint16_t flags;         /* Reserved; must be zero. */
} XfcGuardRecord;

/* Action records either call user JavaScript or apply a compiled assignment. */
enum {
  XFC_ACTION_USER = 0,
  XFC_ACTION_ASSIGN = 1
};

/* User actions reference retained slots; assign actions reference records. */
typedef struct {
  uint16_t kind;      /* XFC_ACTION_USER or XFC_ACTION_ASSIGN. */
  uint16_t reference; /* Retained slot or assignment-table index. */
  uint16_t flags;     /* Reserved; must be zero. */
  uint16_t reserved;  /* Must be zero. */
} XfcActionRecord;

/* Assignment records distinguish function and property-map forms. */
enum {
  XFC_ASSIGN_PARTIAL = 0,
  XFC_ASSIGN_PROPERTY_MAP = 1
};

/* An assign function or a consecutive range of property assignments. */
typedef struct {
  uint16_t kind; /* Assignment function or property map. */
  uint16_t retained_slot; /* Function slot, or NONE for a property map. */
  XfcRange entries;       /* Property entries, empty for a function. */
  uint16_t flags;         /* Reserved; must be zero. */
  uint16_t reserved;      /* Must be zero. */
} XfcAssignmentRecord;

enum {
  XFC_ASSIGN_ENTRY_LITERAL = UINT16_C(0x0000),
  XFC_ASSIGN_ENTRY_EXPRESSION = UINT16_C(0x0001),
  XFC_ASSIGN_ENTRY_KNOWN_FLAGS = UINT16_C(0x0001)
};

/* Property key plus a retained literal or expression function. */
typedef struct {
  uint16_t key_symbol;    /* Context-property-name symbol. */
  uint16_t retained_slot; /* Literal value or expression function. */
  uint16_t flags;         /* Literal or XFC_ASSIGN_ENTRY_EXPRESSION. */
  uint16_t reserved;      /* Must be zero. */
} XfcAssignmentEntryRecord;

/* Persisted actor lifecycle states exposed through snapshot.status. */
enum {
  XFC_ACTOR_NOT_STARTED = 0,
  XFC_ACTOR_ACTIVE = 1,
  XFC_ACTOR_DONE = 2,
  XFC_ACTOR_STOPPED = 3,
  XFC_ACTOR_ERROR = 4
};

/* Non-idle values reject reentrant public actor operations. */
enum {
  XFC_OPERATION_IDLE = 0,
  XFC_OPERATION_START = 1,
  XFC_OPERATION_SEND = 2,
  XFC_OPERATION_STOP = 3,
  XFC_OPERATION_NOTIFY = 4
};

/* Mutable execution state stored in the actor's GC-owned flat string. */
typedef struct {
  uint8_t magic[4];       /* "XFCA" identifies actor runtime data. */
  uint16_t format_version; /* Must match the machine format version. */
  uint8_t status;         /* XFC_ACTOR_* lifecycle state. */
  uint8_t operation;      /* XFC_OPERATION_* reentrancy guard. */
  uint16_t leaf_state;    /* Active leaf, or NONE before start. */
  uint16_t microsteps;    /* Current operation budget consumption. */
  uint16_t flags;         /* Reserved; must be zero. */
  uint16_t reserved;      /* Must be zero. */
} XfcActorData;

/* Reasons returned when compiled machine or actor data fails a check. */
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

/* Checked layout helpers used before any pointer arithmetic. */
bool xfcCheckedAddU32(uint32_t left, uint32_t right, uint32_t *result);
bool xfcCheckedMultiplyU32(uint32_t left, uint32_t right, uint32_t *result);
bool xfcCheckedAlignU32(uint32_t value, uint32_t alignment,
                        uint32_t *result);
bool xfcRangeIsValid(XfcRange range, uint16_t table_count);

uint32_t xfcNativeByteOrderFlag(void);
/* Hashes reduce string comparisons but never replace the final byte check. */
uint32_t xfcFnv1a(const uint8_t *bytes, size_t byte_length);
bool xfcHashedBytesEqual(uint32_t left_hash, const uint8_t *left,
                         uint16_t left_length, uint32_t right_hash,
                         const uint8_t *right, uint16_t right_length);

/* Check compiled or restored machine bytes before reading their records. */
XfcValidationResult xfcValidateArena(const void *arena, size_t arena_length);
/* Compare supplied event/key bytes with one checked symbol record. */
bool xfcArenaSymbolMatches(const void *arena, size_t arena_length,
                           XfcIndex symbol_index, const uint8_t *bytes,
                           uint16_t byte_length, uint32_t hash);

/* Actor data is a separate mutable block referring to an immutable arena. */
void xfcActorDataInit(XfcActorData *actor);
XfcValidationResult xfcValidateActorData(const XfcActorData *actor,
                                         uint16_t state_count);

/* Short stable name used by sanitizer tests and diagnostic output. */
const char *xfcValidationResultName(XfcValidationResult result);

#endif
