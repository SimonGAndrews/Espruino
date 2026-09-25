/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2026 Simon Andrews
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "xfsm_native.h"

#include <limits.h>
#include <string.h>

#define XFC_JOIN_INNER(left, right) left##right
#define XFC_JOIN(left, right) XFC_JOIN_INNER(left, right)
#define XFC_STATIC_ASSERT(expression) \
  typedef char XFC_JOIN(xfc_static_assert_, __LINE__)[(expression) ? 1 : -1]

XFC_STATIC_ASSERT(CHAR_BIT == 8);
XFC_STATIC_ASSERT(UINT8_MAX == UINT8_C(0xFF));
XFC_STATIC_ASSERT(UINT16_MAX == UINT16_C(0xFFFF));
XFC_STATIC_ASSERT(UINT32_MAX == UINT32_C(0xFFFFFFFF));
XFC_STATIC_ASSERT(sizeof(XfcRange) == 4);
XFC_STATIC_ASSERT(sizeof(XfcTableRef) == 8);
XFC_STATIC_ASSERT(sizeof(XfcArenaHeader) == 96);
XFC_STATIC_ASSERT(sizeof(XfcStateRecord) == 32);
XFC_STATIC_ASSERT(sizeof(XfcSymbolRecord) == 12);
XFC_STATIC_ASSERT(sizeof(XfcHandlerRecord) == 8);
XFC_STATIC_ASSERT(sizeof(XfcTransitionRecord) == 12);
XFC_STATIC_ASSERT(sizeof(XfcGuardRecord) == 4);
XFC_STATIC_ASSERT(sizeof(XfcActionRecord) == 8);
XFC_STATIC_ASSERT(sizeof(XfcAssignmentRecord) == 12);
XFC_STATIC_ASSERT(sizeof(XfcAssignmentEntryRecord) == 8);
XFC_STATIC_ASSERT(sizeof(XfcActorData) == 16);
XFC_STATIC_ASSERT(offsetof(XfcArenaHeader, tables) == 32);

static const uint8_t xfcArenaMagic[4] = {'X', 'F', 'C', 'M'};
static const uint8_t xfcActorMagic[4] = {'X', 'F', 'C', 'A'};

static const uint16_t xfcTableRecordSizes[XFC_TABLE_COUNT] = {
    (uint16_t)sizeof(XfcStateRecord),
    (uint16_t)sizeof(XfcSymbolRecord),
    (uint16_t)sizeof(XfcHandlerRecord),
    (uint16_t)sizeof(XfcTransitionRecord),
    (uint16_t)sizeof(XfcGuardRecord),
    (uint16_t)sizeof(XfcActionRecord),
    (uint16_t)sizeof(XfcAssignmentRecord),
    (uint16_t)sizeof(XfcAssignmentEntryRecord)};

typedef struct {
  const uint8_t *bytes;
  size_t length;
  XfcArenaHeader header;
} XfcArenaView;

bool xfcCheckedAddU32(uint32_t left, uint32_t right, uint32_t *result) {
  if (!result || right > UINT32_MAX - left) return false;
  *result = left + right;
  return true;
}

bool xfcCheckedMultiplyU32(uint32_t left, uint32_t right, uint32_t *result) {
  if (!result || (left != 0 && right > UINT32_MAX / left)) return false;
  *result = left * right;
  return true;
}

bool xfcCheckedAlignU32(uint32_t value, uint32_t alignment,
                        uint32_t *result) {
  uint32_t remainder;
  uint32_t addition;
  if (!result || alignment == 0) return false;
  remainder = value % alignment;
  addition = remainder == 0 ? 0 : alignment - remainder;
  return xfcCheckedAddU32(value, addition, result);
}

bool xfcRangeIsValid(XfcRange range, uint16_t table_count) {
  uint32_t end;
  if (range.count == 0)
    return range.first == XFC_INDEX_NONE;
  if (range.first == XFC_INDEX_NONE) return false;
  end = (uint32_t)range.first + (uint32_t)range.count;
  return end <= (uint32_t)table_count;
}

uint32_t xfcNativeByteOrderFlag(void) {
  const uint32_t value = UINT32_C(0x01020304);
  const uint8_t *bytes = (const uint8_t *)&value;
  if (bytes[0] == UINT8_C(0x04) && bytes[3] == UINT8_C(0x01))
    return XFC_ARENA_LITTLE_ENDIAN;
  if (bytes[0] == UINT8_C(0x01) && bytes[3] == UINT8_C(0x04))
    return XFC_ARENA_BIG_ENDIAN;
  return 0;
}

uint32_t xfcFnv1a(const uint8_t *bytes, size_t byte_length) {
  uint32_t hash = UINT32_C(2166136261);
  size_t index;
  if (!bytes && byte_length != 0) return 0;
  for (index = 0; index < byte_length; index++) {
    hash ^= (uint32_t)bytes[index];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

bool xfcHashedBytesEqual(uint32_t left_hash, const uint8_t *left,
                         uint16_t left_length, uint32_t right_hash,
                         const uint8_t *right, uint16_t right_length) {
  if (left_hash != right_hash || left_length != right_length) return false;
  if (left_length == 0) return true;
  if (!left || !right) return false;
  return memcmp(left, right, left_length) == 0;
}

static bool xfcBytesAreZero(const uint8_t *bytes, uint32_t first,
                            uint32_t end) {
  uint32_t index;
  for (index = first; index < end; index++) {
    if (bytes[index] != 0) return false;
  }
  return true;
}

static bool xfcReadTableRecord(const XfcArenaView *view, uint16_t table_index,
                               uint16_t record_index, void *record,
                               size_t record_size) {
  XfcTableRef table;
  uint32_t relative_offset;
  uint32_t absolute_offset;
  uint32_t record_end;
  if (!view || !record || table_index >= XFC_TABLE_COUNT) return false;
  table = view->header.tables[table_index];
  if (record_size != table.record_size || record_index >= table.count)
    return false;
  if (!xfcCheckedMultiplyU32((uint32_t)record_index,
                             (uint32_t)table.record_size,
                             &relative_offset) ||
      !xfcCheckedAddU32(table.offset, relative_offset, &absolute_offset) ||
      !xfcCheckedAddU32(absolute_offset, (uint32_t)record_size,
                        &record_end) ||
      record_end > view->header.arena_size || record_end > view->length)
    return false;
  memcpy(record, view->bytes + absolute_offset, record_size);
  return true;
}

static bool xfcReadState(const XfcArenaView *view, uint16_t index,
                         XfcStateRecord *record) {
  return xfcReadTableRecord(view, XFC_TABLE_STATE, index, record,
                            sizeof(*record));
}

static bool xfcReadSymbol(const XfcArenaView *view, uint16_t index,
                          XfcSymbolRecord *record) {
  return xfcReadTableRecord(view, XFC_TABLE_SYMBOL, index, record,
                            sizeof(*record));
}

static bool xfcReadHandler(const XfcArenaView *view, uint16_t index,
                           XfcHandlerRecord *record) {
  return xfcReadTableRecord(view, XFC_TABLE_HANDLER, index, record,
                            sizeof(*record));
}

static bool xfcReadTransition(const XfcArenaView *view, uint16_t index,
                              XfcTransitionRecord *record) {
  return xfcReadTableRecord(view, XFC_TABLE_TRANSITION, index, record,
                            sizeof(*record));
}

static bool xfcSymbolHasRole(const XfcArenaView *view, uint16_t symbol_index,
                             uint16_t role) {
  XfcSymbolRecord symbol;
  return symbol_index != XFC_INDEX_NONE &&
         xfcReadSymbol(view, symbol_index, &symbol) &&
         (symbol.flags & role) != 0;
}

static bool xfcRetainedSlotIsValid(const XfcArenaView *view,
                                   uint16_t retained_slot) {
  return retained_slot != XFC_INDEX_NONE &&
         retained_slot < view->header.retained_count;
}

static bool xfcStateIsDescendantOrSelf(const XfcArenaView *view,
                                       uint16_t state_index,
                                       uint16_t ancestor_index) {
  XfcStateRecord state;
  uint16_t steps = 0;
  while (state_index != XFC_INDEX_NONE &&
         steps <= (uint16_t)XFC_MAX_STATE_DEPTH) {
    if (state_index == ancestor_index) return true;
    if (!xfcReadState(view, state_index, &state)) return false;
    state_index = state.parent;
    steps++;
  }
  return false;
}

static uint16_t xfcExpectedDomain(const XfcArenaView *view,
                                  uint16_t source_state,
                                  const XfcTransitionRecord *transition) {
  XfcStateRecord source;
  uint16_t candidate;
  uint16_t steps = 0;
  bool reenter = (transition->flags & XFC_TRANSITION_REENTER) != 0;
  if (transition->target_state == XFC_INDEX_NONE) return XFC_INDEX_NONE;
  if (!reenter && xfcStateIsDescendantOrSelf(
                      view, transition->target_state, source_state))
    return source_state;
  if (!xfcReadState(view, source_state, &source)) return XFC_INDEX_NONE;
  candidate = source.parent;
  while (candidate != XFC_INDEX_NONE &&
         steps <= (uint16_t)XFC_MAX_STATE_DEPTH) {
    if (candidate != transition->target_state &&
        xfcStateIsDescendantOrSelf(view, transition->target_state, candidate))
      return candidate;
    if (!xfcReadState(view, candidate, &source)) return XFC_INDEX_NONE;
    candidate = source.parent;
    steps++;
  }
  return XFC_INDEX_NONE;
}

static XfcValidationResult xfcValidateTransitionForSource(
    const XfcArenaView *view, uint16_t source_state,
    uint16_t transition_index) {
  XfcTransitionRecord transition;
  if (!xfcReadTransition(view, transition_index, &transition))
    return XFC_VALIDATION_INDEX;
  if (transition.target_state == XFC_INDEX_NONE)
    return transition.domain_state == XFC_INDEX_NONE ? XFC_VALIDATION_OK
                                                      : XFC_VALIDATION_DOMAIN;
  return transition.domain_state ==
                 xfcExpectedDomain(view, source_state, &transition)
             ? XFC_VALIDATION_OK
             : XFC_VALIDATION_DOMAIN;
}

static XfcValidationResult xfcValidateHeaderAndTables(XfcArenaView *view,
                                                       const void *arena,
                                                       size_t arena_length) {
  uint32_t cursor = XFC_ARENA_HEADER_SIZE;
  uint32_t aligned_cursor;
  uint32_t table_bytes;
  uint32_t table_end;
  uint32_t string_end;
  uint16_t table_index;
  if (!view || !arena) return XFC_VALIDATION_NULL;
  if (((uintptr_t)arena % (uintptr_t)4) != 0)
    return XFC_VALIDATION_ALIGNMENT;
  if (arena_length < sizeof(XfcArenaHeader) || arena_length > UINT32_MAX)
    return XFC_VALIDATION_SIZE;
  view->bytes = (const uint8_t *)arena;
  view->length = arena_length;
  memcpy(&view->header, arena, sizeof(view->header));
  if (memcmp(view->header.magic, xfcArenaMagic, sizeof(xfcArenaMagic)) != 0)
    return XFC_VALIDATION_MAGIC;
  if (view->header.format_version != XFC_ARENA_FORMAT_VERSION ||
      view->header.header_size != XFC_ARENA_HEADER_SIZE)
    return XFC_VALIDATION_VERSION;
  if (view->header.arena_size != (uint32_t)arena_length)
    return XFC_VALIDATION_SIZE;
  if ((view->header.flags & (uint32_t)~XFC_ARENA_KNOWN_FLAGS) != 0)
    return XFC_VALIDATION_FLAGS;
  if (view->header.flags != xfcNativeByteOrderFlag())
    return XFC_VALIDATION_ENDIAN;
  if (view->header.reserved != 0) return XFC_VALIDATION_RESERVED;

  for (table_index = 0; table_index < XFC_TABLE_COUNT; table_index++) {
    XfcTableRef table = view->header.tables[table_index];
    if (table.record_size != xfcTableRecordSizes[table_index])
      return XFC_VALIDATION_TABLE;
    if (table.count == 0) {
      if (table.offset != 0) return XFC_VALIDATION_TABLE;
      continue;
    }
    if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &aligned_cursor))
      return XFC_VALIDATION_SIZE;
    if (aligned_cursor > view->header.arena_size)
      return XFC_VALIDATION_TABLE;
    if (!xfcBytesAreZero(view->bytes, cursor, aligned_cursor))
      return XFC_VALIDATION_PADDING;
    if (table.offset != aligned_cursor || (table.offset % UINT32_C(4)) != 0)
      return XFC_VALIDATION_TABLE;
    if (!xfcCheckedMultiplyU32((uint32_t)table.count,
                               (uint32_t)table.record_size, &table_bytes) ||
        !xfcCheckedAddU32(table.offset, table_bytes, &table_end) ||
        table_end > view->header.arena_size)
      return XFC_VALIDATION_TABLE;
    cursor = table_end;
  }

  if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &aligned_cursor))
    return XFC_VALIDATION_SIZE;
  if (aligned_cursor > view->header.arena_size)
    return XFC_VALIDATION_STRING;
  if (!xfcBytesAreZero(view->bytes, cursor, aligned_cursor))
    return XFC_VALIDATION_PADDING;
  if (view->header.string_offset != aligned_cursor ||
      !xfcCheckedAddU32(view->header.string_offset,
                        view->header.string_size, &string_end) ||
      string_end != view->header.arena_size)
    return XFC_VALIDATION_STRING;
  if (view->header.tables[XFC_TABLE_STATE].count == 0 ||
      view->header.root_state >=
          view->header.tables[XFC_TABLE_STATE].count)
    return XFC_VALIDATION_ROOT;
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateSymbols(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_SYMBOL].count;
  uint16_t index;
  uint32_t pool_end;
  if (!xfcCheckedAddU32(view->header.string_offset,
                        view->header.string_size, &pool_end))
    return XFC_VALIDATION_STRING;
  for (index = 0; index < count; index++) {
    XfcSymbolRecord symbol;
    uint32_t end;
    if (!xfcReadSymbol(view, index, &symbol)) return XFC_VALIDATION_SYMBOL;
    if (symbol.flags == 0 ||
        (symbol.flags & ~XFC_SYMBOL_KNOWN_FLAGS) != 0)
      return XFC_VALIDATION_FLAGS;
    if (symbol.string_offset < view->header.string_offset ||
        !xfcCheckedAddU32(symbol.string_offset,
                          (uint32_t)symbol.byte_length, &end) ||
        end > pool_end)
      return XFC_VALIDATION_STRING;
    if (xfcFnv1a(view->bytes + symbol.string_offset,
                 symbol.byte_length) != symbol.hash)
      return XFC_VALIDATION_SYMBOL;
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateGuards(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_GUARD].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcGuardRecord guard;
    if (!xfcReadTableRecord(view, XFC_TABLE_GUARD, index, &guard,
                            sizeof(guard)))
      return XFC_VALIDATION_GUARD;
    if (guard.flags != 0) return XFC_VALIDATION_FLAGS;
    if (!xfcRetainedSlotIsValid(view, guard.retained_slot))
      return XFC_VALIDATION_INDEX;
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateAssignmentEntries(
    const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_ASSIGNMENT_ENTRY].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcAssignmentEntryRecord entry;
    if (!xfcReadTableRecord(view, XFC_TABLE_ASSIGNMENT_ENTRY, index, &entry,
                            sizeof(entry)))
      return XFC_VALIDATION_ASSIGNMENT_ENTRY;
    if ((entry.flags & ~XFC_ASSIGN_ENTRY_KNOWN_FLAGS) != 0)
      return XFC_VALIDATION_FLAGS;
    if (entry.reserved != 0) return XFC_VALIDATION_RESERVED;
    if (!xfcSymbolHasRole(view, entry.key_symbol, XFC_SYMBOL_CONTEXT_KEY) ||
        !xfcRetainedSlotIsValid(view, entry.retained_slot))
      return XFC_VALIDATION_INDEX;
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateAssignments(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_ASSIGNMENT].count;
  uint16_t entry_count =
      view->header.tables[XFC_TABLE_ASSIGNMENT_ENTRY].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcAssignmentRecord assignment;
    if (!xfcReadTableRecord(view, XFC_TABLE_ASSIGNMENT, index, &assignment,
                            sizeof(assignment)))
      return XFC_VALIDATION_ASSIGNMENT;
    if (assignment.flags != 0) return XFC_VALIDATION_FLAGS;
    if (assignment.reserved != 0) return XFC_VALIDATION_RESERVED;
    if (!xfcRangeIsValid(assignment.entries, entry_count))
      return XFC_VALIDATION_RANGE;
    if (assignment.kind == XFC_ASSIGN_PARTIAL) {
      if (!xfcRetainedSlotIsValid(view, assignment.retained_slot) ||
          assignment.entries.count != 0)
        return XFC_VALIDATION_ASSIGNMENT;
    } else if (assignment.kind == XFC_ASSIGN_PROPERTY_MAP) {
      if (assignment.retained_slot != XFC_INDEX_NONE)
        return XFC_VALIDATION_ASSIGNMENT;
    } else {
      return XFC_VALIDATION_ASSIGNMENT;
    }
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateActions(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_ACTION].count;
  uint16_t assignment_count =
      view->header.tables[XFC_TABLE_ASSIGNMENT].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcActionRecord action;
    if (!xfcReadTableRecord(view, XFC_TABLE_ACTION, index, &action,
                            sizeof(action)))
      return XFC_VALIDATION_ACTION;
    if (action.flags != 0) return XFC_VALIDATION_FLAGS;
    if (action.reserved != 0) return XFC_VALIDATION_RESERVED;
    if (action.kind == XFC_ACTION_USER) {
      if (!xfcRetainedSlotIsValid(view, action.reference))
        return XFC_VALIDATION_INDEX;
    } else if (action.kind == XFC_ACTION_ASSIGN) {
      if (action.reference >= assignment_count)
        return XFC_VALIDATION_INDEX;
    } else {
      return XFC_VALIDATION_ACTION;
    }
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateTransitions(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_TRANSITION].count;
  uint16_t state_count = view->header.tables[XFC_TABLE_STATE].count;
  uint16_t guard_count = view->header.tables[XFC_TABLE_GUARD].count;
  uint16_t action_count = view->header.tables[XFC_TABLE_ACTION].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcTransitionRecord transition;
    if (!xfcReadTransition(view, index, &transition))
      return XFC_VALIDATION_TRANSITION;
    if ((transition.flags & ~XFC_TRANSITION_KNOWN_FLAGS) != 0)
      return XFC_VALIDATION_FLAGS;
    if (transition.target_state != XFC_INDEX_NONE &&
        transition.target_state >= state_count)
      return XFC_VALIDATION_INDEX;
    if (transition.guard != XFC_INDEX_NONE &&
        transition.guard >= guard_count)
      return XFC_VALIDATION_INDEX;
    if (!xfcRangeIsValid(transition.actions, action_count))
      return XFC_VALIDATION_RANGE;
    if (transition.target_state == XFC_INDEX_NONE &&
        transition.domain_state != XFC_INDEX_NONE)
      return XFC_VALIDATION_DOMAIN;
    if (transition.domain_state != XFC_INDEX_NONE &&
        transition.domain_state >= state_count)
      return XFC_VALIDATION_INDEX;
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateHandlers(const XfcArenaView *view) {
  uint16_t count = view->header.tables[XFC_TABLE_HANDLER].count;
  uint16_t transition_count =
      view->header.tables[XFC_TABLE_TRANSITION].count;
  uint16_t index;
  for (index = 0; index < count; index++) {
    XfcHandlerRecord handler;
    bool wildcard;
    if (!xfcReadHandler(view, index, &handler))
      return XFC_VALIDATION_HANDLER;
    if ((handler.flags & ~XFC_HANDLER_KNOWN_FLAGS) != 0)
      return XFC_VALIDATION_FLAGS;
    if (!xfcRangeIsValid(handler.transitions, transition_count) ||
        handler.transitions.count == 0)
      return XFC_VALIDATION_RANGE;
    wildcard = (handler.flags & XFC_HANDLER_WILDCARD) != 0;
    if (wildcard) {
      if (handler.event_symbol != XFC_INDEX_NONE)
        return XFC_VALIDATION_HANDLER;
    } else if (!xfcSymbolHasRole(view, handler.event_symbol,
                                 XFC_SYMBOL_EVENT)) {
      return XFC_VALIDATION_INDEX;
    }
  }
  return XFC_VALIDATION_OK;
}

static XfcValidationResult xfcValidateStates(const XfcArenaView *view) {
  uint16_t state_count = view->header.tables[XFC_TABLE_STATE].count;
  uint16_t handler_count = view->header.tables[XFC_TABLE_HANDLER].count;
  uint16_t transition_count =
      view->header.tables[XFC_TABLE_TRANSITION].count;
  uint16_t action_count = view->header.tables[XFC_TABLE_ACTION].count;
  uint16_t root_count = 0;
  uint16_t index;
  for (index = 0; index < state_count; index++) {
    XfcStateRecord state;
    uint16_t type;
    bool root;
    uint32_t range_index;
    if (!xfcReadState(view, index, &state)) return XFC_VALIDATION_STATE;
    if ((state.flags & ~XFC_STATE_KNOWN_FLAGS) != 0)
      return XFC_VALIDATION_FLAGS;
    if (state.reserved != 0) return XFC_VALIDATION_RESERVED;
    type = state.flags & XFC_STATE_TYPE_MASK;
    if (type == XFC_STATE_TYPE_MASK) return XFC_VALIDATION_STATE;
    root = (state.flags & XFC_STATE_ROOT) != 0;
    if (root) {
      root_count++;
      if (index != view->header.root_state ||
          state.parent != XFC_INDEX_NONE ||
          state.key_symbol != XFC_INDEX_NONE || state.depth != 0 ||
          type == XFC_STATE_FINAL ||
          state.completion_event_symbol != XFC_INDEX_NONE ||
          state.completion_transitions.count != 0)
        return XFC_VALIDATION_ROOT;
    } else {
      XfcStateRecord parent;
      if (index == view->header.root_state || state.parent >= state_count ||
          state.parent == index || state.depth == 0 ||
          state.depth > XFC_MAX_STATE_DEPTH ||
          !xfcReadState(view, state.parent, &parent) ||
          state.depth != (uint8_t)(parent.depth + UINT8_C(1)) ||
          (parent.flags & XFC_STATE_TYPE_MASK) != XFC_STATE_COMPOUND ||
          !xfcSymbolHasRole(view, state.key_symbol,
                            XFC_SYMBOL_STATE_KEY))
        return XFC_VALIDATION_STATE;
    }
    if (!xfcRangeIsValid(state.handlers, handler_count) ||
        !xfcRangeIsValid(state.completion_transitions, transition_count) ||
        !xfcRangeIsValid(state.initial_actions, action_count) ||
        !xfcRangeIsValid(state.entry_actions, action_count) ||
        !xfcRangeIsValid(state.exit_actions, action_count))
      return XFC_VALIDATION_RANGE;
    if (type == XFC_STATE_COMPOUND) {
      XfcStateRecord initial;
      if (state.initial >= state_count ||
          !xfcReadState(view, state.initial, &initial) ||
          initial.parent != index)
        return XFC_VALIDATION_STATE;
      if (!root &&
          !xfcSymbolHasRole(view, state.completion_event_symbol,
                            XFC_SYMBOL_COMPLETION_EVENT))
        return XFC_VALIDATION_STATE;
    } else {
      if (state.initial != XFC_INDEX_NONE ||
          state.initial_actions.count != 0 ||
          state.completion_event_symbol != XFC_INDEX_NONE ||
          state.completion_transitions.count != 0)
        return XFC_VALIDATION_STATE;
      if (type == XFC_STATE_FINAL && state.handlers.count != 0)
        return XFC_VALIDATION_STATE;
    }
    for (range_index = 0; range_index < state.handlers.count; range_index++) {
      XfcHandlerRecord handler;
      uint16_t handler_index =
          (uint16_t)((uint32_t)state.handlers.first + range_index);
      uint32_t transition_offset;
      if (!xfcReadHandler(view, handler_index, &handler))
        return XFC_VALIDATION_HANDLER;
      for (transition_offset = 0;
           transition_offset < handler.transitions.count;
           transition_offset++) {
        uint16_t transition_index = (uint16_t)(
            (uint32_t)handler.transitions.first + transition_offset);
        XfcValidationResult result = xfcValidateTransitionForSource(
            view, index, transition_index);
        if (result != XFC_VALIDATION_OK) return result;
      }
    }
    for (range_index = 0;
         range_index < state.completion_transitions.count; range_index++) {
      uint16_t transition_index = (uint16_t)(
          (uint32_t)state.completion_transitions.first + range_index);
      XfcValidationResult result = xfcValidateTransitionForSource(
          view, index, transition_index);
      if (result != XFC_VALIDATION_OK) return result;
    }
  }
  return root_count == 1 ? XFC_VALIDATION_OK : XFC_VALIDATION_ROOT;
}

XfcValidationResult xfcValidateArena(const void *arena, size_t arena_length) {
  XfcArenaView view;
  XfcValidationResult result =
      xfcValidateHeaderAndTables(&view, arena, arena_length);
  if (result != XFC_VALIDATION_OK) return result;
  if (view.header.context_kind == XFC_CONTEXT_OMITTED) {
    if (view.header.context_slot != XFC_INDEX_NONE)
      return XFC_VALIDATION_INDEX;
  } else if (view.header.context_kind == XFC_CONTEXT_LITERAL ||
             view.header.context_kind == XFC_CONTEXT_FACTORY) {
    if (!xfcRetainedSlotIsValid(&view, view.header.context_slot))
      return XFC_VALIDATION_INDEX;
  } else {
    return XFC_VALIDATION_FLAGS;
  }
  result = xfcValidateSymbols(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateGuards(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateAssignmentEntries(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateAssignments(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateActions(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateTransitions(&view);
  if (result != XFC_VALIDATION_OK) return result;
  result = xfcValidateHandlers(&view);
  if (result != XFC_VALIDATION_OK) return result;
  return xfcValidateStates(&view);
}

bool xfcArenaSymbolMatches(const void *arena, size_t arena_length,
                           XfcIndex symbol_index, const uint8_t *bytes,
                           uint16_t byte_length, uint32_t hash) {
  XfcArenaView view;
  XfcSymbolRecord symbol;
  uint32_t symbol_end;
  uint32_t pool_end;
  if (xfcValidateHeaderAndTables(&view, arena, arena_length) !=
          XFC_VALIDATION_OK ||
      !xfcReadSymbol(&view, symbol_index, &symbol) ||
      !xfcCheckedAddU32(view.header.string_offset,
                        view.header.string_size, &pool_end) ||
      symbol.string_offset < view.header.string_offset ||
      !xfcCheckedAddU32(symbol.string_offset,
                        (uint32_t)symbol.byte_length, &symbol_end) ||
      symbol_end > pool_end)
    return false;
  return xfcHashedBytesEqual(
      symbol.hash, view.bytes + symbol.string_offset, symbol.byte_length,
      hash, bytes, byte_length);
}

void xfcActorDataInit(XfcActorData *actor) {
  if (!actor) return;
  memset(actor, 0, sizeof(*actor));
  memcpy(actor->magic, xfcActorMagic, sizeof(xfcActorMagic));
  actor->format_version = XFC_ARENA_FORMAT_VERSION;
  actor->status = XFC_ACTOR_NOT_STARTED;
  actor->operation = XFC_OPERATION_IDLE;
  actor->leaf_state = XFC_INDEX_NONE;
}

XfcValidationResult xfcValidateActorData(const XfcActorData *actor,
                                         uint16_t state_count) {
  if (!actor) return XFC_VALIDATION_NULL;
  if (memcmp(actor->magic, xfcActorMagic, sizeof(xfcActorMagic)) != 0)
    return XFC_VALIDATION_MAGIC;
  if (actor->format_version != XFC_ARENA_FORMAT_VERSION)
    return XFC_VALIDATION_VERSION;
  if (actor->status > XFC_ACTOR_ERROR ||
      actor->operation > XFC_OPERATION_NOTIFY)
    return XFC_VALIDATION_ACTOR;
  if (actor->flags != 0) return XFC_VALIDATION_FLAGS;
  if (actor->reserved != 0) return XFC_VALIDATION_RESERVED;
  if (actor->leaf_state != XFC_INDEX_NONE &&
      actor->leaf_state >= state_count)
    return XFC_VALIDATION_INDEX;
  if (actor->status == XFC_ACTOR_NOT_STARTED &&
      actor->leaf_state != XFC_INDEX_NONE)
    return XFC_VALIDATION_ACTOR;
  if ((actor->status == XFC_ACTOR_ACTIVE ||
       actor->status == XFC_ACTOR_DONE) &&
      actor->leaf_state == XFC_INDEX_NONE)
    return XFC_VALIDATION_ACTOR;
  if (actor->operation == XFC_OPERATION_IDLE && actor->microsteps != 0)
    return XFC_VALIDATION_ACTOR;
  return XFC_VALIDATION_OK;
}

const char *xfcValidationResultName(XfcValidationResult result) {
  static const char *const names[] = {
      "ok",          "null",       "alignment",  "size",
      "magic",       "version",    "endian",     "flags",
      "reserved",    "table",      "padding",    "string",
      "range",       "index",      "root",       "state",
      "symbol",      "handler",    "transition", "guard",
      "action",      "assignment", "assignment-entry",
      "domain",      "actor"};
  size_t count = sizeof(names) / sizeof(names[0]);
  return (size_t)result < count ? names[result] : "unknown";
}
