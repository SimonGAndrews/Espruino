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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ARENA_CAPACITY 1024

typedef union {
  uint32_t alignment;
  uint8_t bytes[TEST_ARENA_CAPACITY];
} TestArena;

static unsigned int tests_run;
static unsigned int tests_failed;

#define CHECK(expression)                                                     \
  do {                                                                        \
    tests_run++;                                                              \
    if (!(expression)) {                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
      tests_failed++;                                                         \
    }                                                                         \
  } while (0)

static XfcRange emptyRange(void) {
  XfcRange range;
  range.first = XFC_INDEX_NONE;
  range.count = 0;
  return range;
}

static void initializeState(XfcStateRecord *state) {
  memset(state, 0, sizeof(*state));
  state->parent = XFC_INDEX_NONE;
  state->initial = XFC_INDEX_NONE;
  state->key_symbol = XFC_INDEX_NONE;
  state->completion_event_symbol = XFC_INDEX_NONE;
  state->handlers = emptyRange();
  state->completion_transitions = emptyRange();
  state->initial_actions = emptyRange();
  state->entry_actions = emptyRange();
  state->exit_actions = emptyRange();
}

static uint32_t addTable(XfcArenaHeader *header, uint16_t table_index,
                         uint16_t count, uint16_t record_size,
                         uint32_t cursor) {
  uint32_t aligned;
  uint32_t bytes;
  uint32_t end;
  if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &aligned) ||
      !xfcCheckedMultiplyU32((uint32_t)count, (uint32_t)record_size,
                             &bytes) ||
      !xfcCheckedAddU32(aligned, bytes, &end))
    abort();
  header->tables[table_index].count = count;
  header->tables[table_index].record_size = record_size;
  header->tables[table_index].offset = count == 0 ? 0 : aligned;
  return count == 0 ? cursor : end;
}

static void writeRecord(TestArena *arena, const XfcArenaHeader *header,
                        uint16_t table_index, uint16_t record_index,
                        const void *record, size_t record_size) {
  uint32_t offset = header->tables[table_index].offset +
                    (uint32_t)record_index *
                        (uint32_t)header->tables[table_index].record_size;
  memcpy(arena->bytes + offset, record, record_size);
}

static void readRecord(const TestArena *arena, const XfcArenaHeader *header,
                       uint16_t table_index, uint16_t record_index,
                       void *record, size_t record_size) {
  uint32_t offset = header->tables[table_index].offset +
                    (uint32_t)record_index *
                        (uint32_t)header->tables[table_index].record_size;
  memcpy(record, arena->bytes + offset, record_size);
}

static size_t buildAtomicRootArena(TestArena *arena) {
  XfcArenaHeader header;
  XfcStateRecord state;
  uint16_t index;
  uint32_t cursor = XFC_ARENA_HEADER_SIZE;
  memset(arena, 0, sizeof(*arena));
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "XFCM", 4);
  header.format_version = XFC_ARENA_FORMAT_VERSION;
  header.header_size = XFC_ARENA_HEADER_SIZE;
  header.flags = xfcNativeByteOrderFlag();
  header.root_state = 0;
  header.context_slot = XFC_INDEX_NONE;
  header.context_kind = XFC_CONTEXT_OMITTED;
  cursor = addTable(&header, XFC_TABLE_STATE, 1,
                    (uint16_t)sizeof(XfcStateRecord), cursor);
  for (index = XFC_TABLE_SYMBOL; index < XFC_TABLE_COUNT; index++) {
    static const uint16_t sizes[XFC_TABLE_COUNT] = {
        (uint16_t)sizeof(XfcStateRecord),
        (uint16_t)sizeof(XfcSymbolRecord),
        (uint16_t)sizeof(XfcHandlerRecord),
        (uint16_t)sizeof(XfcTransitionRecord),
        (uint16_t)sizeof(XfcGuardRecord),
        (uint16_t)sizeof(XfcActionRecord),
        (uint16_t)sizeof(XfcAssignmentRecord),
        (uint16_t)sizeof(XfcAssignmentEntryRecord)};
    cursor = addTable(&header, index, 0, sizes[index], cursor);
  }
  if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &header.string_offset))
    abort();
  header.string_size = 0;
  header.arena_size = header.string_offset;
  initializeState(&state);
  state.flags = XFC_STATE_ROOT | XFC_STATE_ATOMIC;
  writeRecord(arena, &header, XFC_TABLE_STATE, 0, &state, sizeof(state));
  memcpy(arena->bytes, &header, sizeof(header));
  return header.arena_size;
}

static size_t buildRichArena(TestArena *arena) {
  static const char *const strings[] = {
      "Parent", "Idle", "Final", "Outside", "GO", "done.state.Parent",
      "count"};
  static const uint16_t symbol_flags[] = {
      XFC_SYMBOL_STATE_KEY,
      XFC_SYMBOL_STATE_KEY,
      XFC_SYMBOL_STATE_KEY,
      XFC_SYMBOL_STATE_KEY,
      XFC_SYMBOL_EVENT,
      XFC_SYMBOL_EVENT | XFC_SYMBOL_COMPLETION_EVENT,
      XFC_SYMBOL_CONTEXT_KEY};
  static const uint16_t counts[XFC_TABLE_COUNT] = {5, 7, 1, 2, 1, 2, 1, 1};
  static const uint16_t sizes[XFC_TABLE_COUNT] = {
      (uint16_t)sizeof(XfcStateRecord),
      (uint16_t)sizeof(XfcSymbolRecord),
      (uint16_t)sizeof(XfcHandlerRecord),
      (uint16_t)sizeof(XfcTransitionRecord),
      (uint16_t)sizeof(XfcGuardRecord),
      (uint16_t)sizeof(XfcActionRecord),
      (uint16_t)sizeof(XfcAssignmentRecord),
      (uint16_t)sizeof(XfcAssignmentEntryRecord)};
  XfcArenaHeader header;
  XfcStateRecord states[5];
  XfcSymbolRecord symbols[7];
  XfcHandlerRecord handler;
  XfcTransitionRecord transitions[2];
  XfcGuardRecord guard;
  XfcActionRecord actions[2];
  XfcAssignmentRecord assignment;
  XfcAssignmentEntryRecord entry;
  uint32_t cursor = XFC_ARENA_HEADER_SIZE;
  uint16_t index;

  memset(arena, 0, sizeof(*arena));
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "XFCM", 4);
  header.format_version = XFC_ARENA_FORMAT_VERSION;
  header.header_size = XFC_ARENA_HEADER_SIZE;
  header.flags = xfcNativeByteOrderFlag();
  header.root_state = 0;
  header.context_slot = 3;
  header.retained_count = 4;
  header.context_kind = XFC_CONTEXT_LITERAL;
  for (index = 0; index < XFC_TABLE_COUNT; index++)
    cursor = addTable(&header, index, counts[index], sizes[index], cursor);
  if (!xfcCheckedAlignU32(cursor, UINT32_C(4), &header.string_offset))
    abort();
  cursor = header.string_offset;
  for (index = 0; index < 7; index++) {
    size_t length = strlen(strings[index]);
    if (length > UINT16_MAX) abort();
    symbols[index].hash =
        xfcFnv1a((const uint8_t *)strings[index], length);
    symbols[index].string_offset = cursor;
    symbols[index].byte_length = (uint16_t)length;
    symbols[index].flags = symbol_flags[index];
    memcpy(arena->bytes + cursor, strings[index], length);
    cursor += (uint32_t)length;
  }
  header.string_size = cursor - header.string_offset;
  header.arena_size = cursor;

  for (index = 0; index < 5; index++) initializeState(&states[index]);
  states[0].initial = 1;
  states[0].initial_actions.first = 0;
  states[0].initial_actions.count = 1;
  states[0].flags = XFC_STATE_ROOT | XFC_STATE_COMPOUND;

  states[1].parent = 0;
  states[1].initial = 2;
  states[1].key_symbol = 0;
  states[1].completion_event_symbol = 5;
  states[1].completion_transitions.first = 1;
  states[1].completion_transitions.count = 1;
  states[1].entry_actions.first = 0;
  states[1].entry_actions.count = 1;
  states[1].exit_actions.first = 0;
  states[1].exit_actions.count = 1;
  states[1].flags = XFC_STATE_COMPOUND;
  states[1].depth = 1;

  states[2].parent = 1;
  states[2].key_symbol = 1;
  states[2].handlers.first = 0;
  states[2].handlers.count = 1;
  states[2].entry_actions.first = 0;
  states[2].entry_actions.count = 1;
  states[2].exit_actions.first = 1;
  states[2].exit_actions.count = 1;
  states[2].flags = XFC_STATE_ATOMIC;
  states[2].depth = 2;

  states[3].parent = 1;
  states[3].key_symbol = 2;
  states[3].flags = XFC_STATE_FINAL;
  states[3].depth = 2;

  states[4].parent = 0;
  states[4].key_symbol = 3;
  states[4].flags = XFC_STATE_ATOMIC;
  states[4].depth = 1;

  handler.event_symbol = 4;
  handler.transitions.first = 0;
  handler.transitions.count = 1;
  handler.flags = 0;

  transitions[0].target_state = 3;
  transitions[0].guard = 0;
  transitions[0].actions.first = 0;
  transitions[0].actions.count = 2;
  transitions[0].flags = 0;
  transitions[0].domain_state = 1;

  transitions[1].target_state = 4;
  transitions[1].guard = XFC_INDEX_NONE;
  transitions[1].actions = emptyRange();
  transitions[1].flags = 0;
  transitions[1].domain_state = 0;

  guard.retained_slot = 0;
  guard.flags = 0;

  actions[0].kind = XFC_ACTION_USER;
  actions[0].reference = 1;
  actions[0].flags = 0;
  actions[0].reserved = 0;
  actions[1].kind = XFC_ACTION_ASSIGN;
  actions[1].reference = 0;
  actions[1].flags = 0;
  actions[1].reserved = 0;

  assignment.kind = XFC_ASSIGN_PROPERTY_MAP;
  assignment.retained_slot = XFC_INDEX_NONE;
  assignment.entries.first = 0;
  assignment.entries.count = 1;
  assignment.flags = 0;
  assignment.reserved = 0;

  entry.key_symbol = 6;
  entry.retained_slot = 2;
  entry.flags = XFC_ASSIGN_ENTRY_LITERAL;
  entry.reserved = 0;

  for (index = 0; index < 5; index++)
    writeRecord(arena, &header, XFC_TABLE_STATE, index, &states[index],
                sizeof(states[index]));
  for (index = 0; index < 7; index++)
    writeRecord(arena, &header, XFC_TABLE_SYMBOL, index, &symbols[index],
                sizeof(symbols[index]));
  writeRecord(arena, &header, XFC_TABLE_HANDLER, 0, &handler,
              sizeof(handler));
  for (index = 0; index < 2; index++)
    writeRecord(arena, &header, XFC_TABLE_TRANSITION, index,
                &transitions[index], sizeof(transitions[index]));
  writeRecord(arena, &header, XFC_TABLE_GUARD, 0, &guard, sizeof(guard));
  for (index = 0; index < 2; index++)
    writeRecord(arena, &header, XFC_TABLE_ACTION, index, &actions[index],
                sizeof(actions[index]));
  writeRecord(arena, &header, XFC_TABLE_ASSIGNMENT, 0, &assignment,
              sizeof(assignment));
  writeRecord(arena, &header, XFC_TABLE_ASSIGNMENT_ENTRY, 0, &entry,
              sizeof(entry));
  memcpy(arena->bytes, &header, sizeof(header));
  return header.arena_size;
}

static void copyArena(TestArena *destination, const TestArena *source) {
  memcpy(destination, source, sizeof(*destination));
}

static void readHeader(const TestArena *arena, XfcArenaHeader *header) {
  memcpy(header, arena->bytes, sizeof(*header));
}

static void writeHeader(TestArena *arena, const XfcArenaHeader *header) {
  memcpy(arena->bytes, header, sizeof(*header));
}

static void testArithmeticAndRanges(void) {
  uint32_t result = 0;
  XfcRange range;
  CHECK(xfcCheckedAddU32(1, 2, &result) && result == 3);
  CHECK(!xfcCheckedAddU32(UINT32_MAX, 1, &result));
  CHECK(xfcCheckedMultiplyU32(65535, 8, &result) && result == 524280);
  CHECK(!xfcCheckedMultiplyU32(UINT32_MAX, 2, &result));
  CHECK(xfcCheckedAlignU32(97, 4, &result) && result == 100);
  CHECK(!xfcCheckedAlignU32(UINT32_MAX, 4, &result));
  CHECK(!xfcCheckedAlignU32(1, 0, &result));

  range.first = XFC_INDEX_NONE;
  range.count = 0;
  CHECK(xfcRangeIsValid(range, 0));
  range.first = 0;
  range.count = UINT16_MAX;
  CHECK(xfcRangeIsValid(range, UINT16_MAX));
  range.first = 1;
  CHECK(!xfcRangeIsValid(range, UINT16_MAX));
  range.first = XFC_INDEX_NONE;
  range.count = 1;
  CHECK(!xfcRangeIsValid(range, UINT16_MAX));
}

static void testHashAndCollision(void) {
  static const uint8_t hello[] = "hello";
  static const uint8_t costarring[] = "costarring";
  static const uint8_t liquid[] = "liquid";
  uint32_t collision_hash = UINT32_C(0x5E4DAA9D);
  CHECK(xfcFnv1a(NULL, 0) == UINT32_C(0x811C9DC5));
  CHECK(xfcFnv1a(hello, 5) == UINT32_C(0x4F9F2CAB));
  CHECK(xfcFnv1a(costarring, 10) == collision_hash);
  CHECK(xfcFnv1a(liquid, 6) == collision_hash);
  CHECK(!xfcHashedBytesEqual(collision_hash, costarring, 10,
                             collision_hash, liquid, 6));
  CHECK(xfcHashedBytesEqual(collision_hash, costarring, 10,
                            collision_hash, costarring, 10));
}

static void testValidArenas(void) {
  TestArena atomic_arena;
  TestArena rich_arena;
  TestArena factory_arena;
  XfcArenaHeader header;
  size_t atomic_size = buildAtomicRootArena(&atomic_arena);
  size_t rich_size = buildRichArena(&rich_arena);
  CHECK(atomic_size == 128);
  CHECK(xfcValidateArena(atomic_arena.bytes, atomic_size) ==
        XFC_VALIDATION_OK);
  CHECK(rich_size == 458);
  CHECK(xfcValidateArena(rich_arena.bytes, rich_size) ==
        XFC_VALIDATION_OK);
  readHeader(&rich_arena, &header);
  CHECK(header.tables[XFC_TABLE_STATE].offset == 96);
  CHECK(header.tables[XFC_TABLE_SYMBOL].offset == 256);
  CHECK(header.tables[XFC_TABLE_HANDLER].offset == 340);
  CHECK(header.tables[XFC_TABLE_TRANSITION].offset == 348);
  CHECK(header.tables[XFC_TABLE_GUARD].offset == 372);
  CHECK(header.tables[XFC_TABLE_ACTION].offset == 376);
  CHECK(header.tables[XFC_TABLE_ASSIGNMENT].offset == 392);
  CHECK(header.tables[XFC_TABLE_ASSIGNMENT_ENTRY].offset == 404);
  CHECK(header.string_offset == 412 && header.string_size == 46);
  CHECK(xfcArenaSymbolMatches(rich_arena.bytes, rich_size, 4,
                              (const uint8_t *)"GO", 2,
                              xfcFnv1a((const uint8_t *)"GO", 2)));
  CHECK(!xfcArenaSymbolMatches(rich_arena.bytes, rich_size, 4,
                               (const uint8_t *)"NO", 2,
                               xfcFnv1a((const uint8_t *)"NO", 2)));
  copyArena(&factory_arena, &rich_arena);
  readHeader(&factory_arena, &header);
  header.context_kind = XFC_CONTEXT_FACTORY;
  writeHeader(&factory_arena, &header);
  CHECK(xfcValidateArena(factory_arena.bytes, rich_size) ==
        XFC_VALIDATION_OK);
  printf("GOLDEN arena=458 states=5 symbols=7 handlers=1 transitions=2 "
         "guards=1 actions=2 assignments=1 entries=1 strings=46\n");
}

static void testHandlersAndTransitionDomains(void) {
  TestArena valid;
  TestArena changed;
  XfcArenaHeader header;
  XfcStateRecord state;
  XfcHandlerRecord handler;
  XfcTransitionRecord transition;
  size_t length = buildRichArena(&valid);
  readHeader(&valid, &header);

  copyArena(&changed, &valid);
  readRecord(&changed, &header, XFC_TABLE_HANDLER, 0, &handler,
             sizeof(handler));
  handler.transitions.count = 2;
  writeRecord(&changed, &header, XFC_TABLE_HANDLER, 0, &handler,
              sizeof(handler));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  handler.event_symbol = XFC_INDEX_NONE;
  handler.flags = XFC_HANDLER_WILDCARD;
  writeRecord(&changed, &header, XFC_TABLE_HANDLER, 0, &handler,
              sizeof(handler));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  copyArena(&changed, &valid);
  readRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
             sizeof(transition));
  transition.target_state = 2;
  transition.flags = 0;
  transition.domain_state = 2;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.flags = XFC_TRANSITION_REENTER;
  transition.domain_state = 1;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.target_state = 1;
  transition.flags = 0;
  transition.domain_state = 0;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.target_state = 4;
  transition.domain_state = 0;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.target_state = XFC_INDEX_NONE;
  transition.flags = XFC_TRANSITION_REENTER;
  transition.domain_state = XFC_INDEX_NONE;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  copyArena(&changed, &valid);
  readRecord(&changed, &header, XFC_TABLE_TRANSITION, 1, &transition,
             sizeof(transition));
  transition.target_state = 2;
  transition.flags = 0;
  transition.domain_state = 1;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 1, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.flags = XFC_TRANSITION_REENTER;
  transition.domain_state = 0;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 1, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  copyArena(&changed, &valid);
  readRecord(&changed, &header, XFC_TABLE_STATE, 2, &state, sizeof(state));
  state.handlers = emptyRange();
  writeRecord(&changed, &header, XFC_TABLE_STATE, 2, &state, sizeof(state));
  readRecord(&changed, &header, XFC_TABLE_STATE, 0, &state, sizeof(state));
  state.handlers.first = 0;
  state.handlers.count = 1;
  writeRecord(&changed, &header, XFC_TABLE_STATE, 0, &state, sizeof(state));
  readRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
             sizeof(transition));
  transition.target_state = 0;
  transition.flags = XFC_TRANSITION_REENTER;
  transition.domain_state = XFC_INDEX_NONE;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);

  transition.flags = 0;
  transition.domain_state = 0;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_OK);
}

static void testArenaCorruption(void) {
  TestArena valid;
  TestArena changed;
  XfcArenaHeader header;
  XfcStateRecord state;
  XfcSymbolRecord symbol;
  XfcTransitionRecord transition;
  XfcActionRecord action;
  XfcAssignmentEntryRecord entry;
  size_t length = buildRichArena(&valid);

  copyArena(&changed, &valid);
  changed.bytes[0] = 'B';
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_MAGIC);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.format_version++;
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_VERSION);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.flags = header.flags == XFC_ARENA_LITTLE_ENDIAN
                     ? XFC_ARENA_BIG_ENDIAN
                     : XFC_ARENA_LITTLE_ENDIAN;
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_ENDIAN);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.flags |= UINT32_C(0x80000000);
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_FLAGS);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.reserved = 1;
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_RESERVED);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.arena_size--;
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_SIZE);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  header.tables[XFC_TABLE_STATE].offset += 4;
  writeHeader(&changed, &header);
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_TABLE);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&symbol,
         changed.bytes + header.tables[XFC_TABLE_SYMBOL].offset,
         sizeof(symbol));
  symbol.hash++;
  writeRecord(&changed, &header, XFC_TABLE_SYMBOL, 0, &symbol,
              sizeof(symbol));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_SYMBOL);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&symbol,
         changed.bytes + header.tables[XFC_TABLE_SYMBOL].offset,
         sizeof(symbol));
  symbol.string_offset = header.arena_size;
  writeRecord(&changed, &header, XFC_TABLE_SYMBOL, 0, &symbol,
              sizeof(symbol));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_STRING);
  CHECK(!xfcArenaSymbolMatches(changed.bytes, length, 0,
                               (const uint8_t *)"Parent", 6,
                               xfcFnv1a((const uint8_t *)"Parent", 6)));

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&state, changed.bytes + header.tables[XFC_TABLE_STATE].offset +
                     2 * sizeof(state),
         sizeof(state));
  state.handlers.first = XFC_INDEX_NONE;
  writeRecord(&changed, &header, XFC_TABLE_STATE, 2, &state, sizeof(state));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_RANGE);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&transition,
         changed.bytes + header.tables[XFC_TABLE_TRANSITION].offset,
         sizeof(transition));
  transition.domain_state = 0;
  writeRecord(&changed, &header, XFC_TABLE_TRANSITION, 0, &transition,
              sizeof(transition));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_DOMAIN);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&action, changed.bytes + header.tables[XFC_TABLE_ACTION].offset,
         sizeof(action));
  action.flags = 1;
  writeRecord(&changed, &header, XFC_TABLE_ACTION, 0, &action,
              sizeof(action));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_FLAGS);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&entry,
         changed.bytes + header.tables[XFC_TABLE_ASSIGNMENT_ENTRY].offset,
         sizeof(entry));
  entry.reserved = 1;
  writeRecord(&changed, &header, XFC_TABLE_ASSIGNMENT_ENTRY, 0, &entry,
              sizeof(entry));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_RESERVED);

  copyArena(&changed, &valid);
  readHeader(&changed, &header);
  memcpy(&state, changed.bytes + header.tables[XFC_TABLE_STATE].offset +
                     4 * sizeof(state),
         sizeof(state));
  state.depth = 33;
  writeRecord(&changed, &header, XFC_TABLE_STATE, 4, &state, sizeof(state));
  CHECK(xfcValidateArena(changed.bytes, length) == XFC_VALIDATION_STATE);

  copyArena(&changed, &valid);
  memmove(changed.bytes + 1, changed.bytes, length);
  CHECK(xfcValidateArena(changed.bytes + 1, length) ==
        XFC_VALIDATION_ALIGNMENT);
}

static void testActorData(void) {
  XfcActorData actor;
  xfcActorDataInit(&actor);
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_OK);
  actor.status = XFC_ACTOR_ACTIVE;
  actor.leaf_state = 2;
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_OK);
  actor.leaf_state = 5;
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_INDEX);
  xfcActorDataInit(&actor);
  actor.microsteps = 1;
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_ACTOR);
  xfcActorDataInit(&actor);
  actor.reserved = 1;
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_RESERVED);
  xfcActorDataInit(&actor);
  actor.magic[0] = 'B';
  CHECK(xfcValidateActorData(&actor, 5) == XFC_VALIDATION_MAGIC);
}

int main(void) {
  testArithmeticAndRanges();
  testHashAndCollision();
  testValidArenas();
  testHandlersAndTransitionDomains();
  testArenaCorruption();
  testActorData();
  if (tests_failed != 0) {
    fprintf(stderr, "%u of %u XFSM native tests failed\n", tests_failed,
            tests_run);
    return EXIT_FAILURE;
  }
  printf("PASS %u XFSM native checks\n", tests_run);
  return EXIT_SUCCESS;
}
