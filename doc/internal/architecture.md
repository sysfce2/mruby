<!-- summary: About mruby Architecture -->

# mruby Architecture

This document provides a map of mruby's internals for developers who
want to understand, debug, or contribute to the codebase.

## Overview

mruby's execution pipeline:

```text
Ruby source → Parser → AST → Code Generator → Bytecode (irep)
                                                    ↓
                                                   VM → Result
```

The design priority is **memory > performance > readability**.

## Object Model

All heap-allocated Ruby objects share a common header (`MRB_OBJECT_HEADER`):

```text
struct RBasic (8 bytes on 64-bit)
┌──────────────┬─────┬──────────┬────────┬───────┐
│ RClass *c    │ tt  │ gc_color │ frozen │ flags │
│ (class ptr)  │ 8b  │ 3b       │ 1b     │ 20b   │
└──────────────┴─────┴──────────┴────────┴───────┘
```

All object structs embed this header via `MRB_OBJECT_HEADER`:

| Struct | Ruby Type | Extra Fields |
| ------ | --------- | ------------ |
| `RObject` | Object instances | `iv` (instance variables) |
| `RClass` | Class/Module | `iv`, `mt` (method table), `super` |
| `RString` | String | embedded or heap buffer, length |
| `RArray` | Array | embedded or heap buffer, length |
| `RHash` | Hash | hash table or k-v array |
| `RProc` | Proc/Lambda | `irep` or C function, environment |
| `RData` | C data wrapper | `void *data`, `mrb_data_type` |
| `RFiber` | Fiber | `mrb_context` |
| `RException` | Exception | `iv` |

Immediate values (Integer, Symbol, `true`, `false`, `nil`) are encoded
directly in `mrb_value` without heap allocation. The encoding depends on
the boxing mode (see [boxing.md](boxing.md)).

Objects must fit within 5 words (`mrb_static_assert_object_size`).

## Virtual Machine

### Execution Context

The VM uses two stacks stored in `mrb_context`:

```text
mrb_context
├── stbase..stend     value stack (mrb_value[])
├── cibase..ciend     call info stack (mrb_callinfo[])
├── ci                current call frame (→ cibase[n])
└── status            fiber state
```

Each method call pushes a `mrb_callinfo` frame:

```text
mrb_callinfo
├── mid        method symbol
├── proc       current RProc
├── stack      pointer into value stack
├── pc         program counter (bytecode position)
├── n, nk      argument counts
├── cci        nonzero if called from C
└── u.env / u.target_class
```

The value stack is register-based: local variables and temporaries
occupy fixed register slots (determined at compile time by `nregs`).

### Dispatch Loop

The main loop in `mrb_vm_run()` (`src/vm.c`) decodes and dispatches
opcodes. Each opcode operates on registers:

```text
OP_MOVE     R(a) = R(b)
OP_LOADI    R(a) = integer
OP_ADD      R(a) = R(a) + R(a+1)
OP_SEND     R(a) = call R(a).method(R(a+1)..R(a+n))
OP_RETURN   return R(a)
```

See [opcode.md](opcode.md) for the full instruction set.

### Method Dispatch

When `OP_SEND` executes:

1. Look up method in receiver's class method table (`mt`)
2. Walk superclass chain if not found
3. If method is a C function (`MRB_METHOD_CFUNC_P`), call directly
4. If method is Ruby (irep-based), push new `mrb_callinfo` and jump

Method tables use a hash map (`mrb_mt_tbl`). A per-state method cache
speeds up repeated lookups.

### Exception Handling

mruby uses `setjmp`/`longjmp` for exception unwinding (or C++ exceptions
if `enable_cxx_exception` is configured). The `rescue`/`ensure` entries
are tracked in `mrb_callinfo` entries and unwound when an exception
propagates.

## Garbage Collector

### Tri-Color Mark-and-Sweep

The GC uses tri-color marking with incremental execution:

| Color | Meaning |
| ----- | ------- |
| White | Unmarked — candidate for collection |
| Gray | Marked but children not yet scanned |
| Black | Fully marked (reachable) |
| Red | Static/ROM — never collected |

### GC Phases

```text
GC_STATE_ROOT → GC_STATE_MARK → GC_STATE_SWEEP → GC_STATE_ROOT
```

1. **Root marking**: marks objects directly reachable from the VM
   (stack, globals, arena)
2. **Incremental marking**: processes gray objects from `gray_stack[]`,
   marking their children. Runs in small steps between VM instructions.
3. **Sweep**: iterates heap pages, freeing white objects and flipping
   the white bit for the next cycle

### Write Barriers

When a black object stores a reference to a white object, a write
barrier is required to prevent premature collection:

```c
mrb_field_write_barrier(mrb, parent, child);  /* specific field */
mrb_write_barrier(mrb, obj);                  /* general */
```

The barrier paints the parent gray, adding it back to the scan queue.

### GC Arena

The arena (`gc.arena[]`) protects newly created objects from collection
before they are stored in a reachable location. C extensions must save
and restore the arena index when creating many temporary objects:

```c
int ai = mrb_gc_arena_save(mrb);
/* ... create temporary objects ... */
mrb_gc_arena_restore(mrb, ai);
```

See [../guides/gc-arena-howto.md](../guides/gc-arena-howto.md) for details.

### Heap Structure

Objects are allocated from fixed-size heap pages (`HEAP_PAGE_SIZE`
objects per page). Each page maintains a freelist of available slots.
Dead objects are returned to their page's freelist during sweep.

The `mrb_gc_add_region()` API allows pre-allocating contiguous heap
regions for reduced fragmentation and faster allocation.

### Generational Mode

Optional generational GC (`mrb_gc_generational_mode_set`) treats
objects surviving a full GC as "old generation" and performs minor
collections that only scan young objects.

## Compiler Pipeline

### Stage 1: Parser (`mrbgems/mruby-compiler/core/parse.y`)

The yacc/bison grammar (~16K lines) produces an AST of `mrb_ast_node`
linked structures. The parser tracks lexer state (expression context,
heredocs, string interpolation) and local variable scopes.

### Stage 2: Code Generator (`mrbgems/mruby-compiler/core/codegen.c`)

Walks the AST and emits bytecode into `mrb_irep` structures:

```text
mrb_irep
├── iseq[]    instruction stream (bytecode)
├── pool[]    constant pool (strings, numbers)
├── syms[]    symbol table (method names, variable names)
├── reps[]    child ireps (nested methods, blocks)
├── nlocals   local variable count
└── nregs     register count (locals + temporaries)
```

The code generator:

- Assigns register slots for local variables and temporaries
- Emits instructions for each AST node type
- Builds jump tables for control flow (if/unless/while/for)
- Encodes exception handler ranges for rescue/ensure

### Stage 3: Execution

The irep is wrapped in an `RProc` and executed by the VM. Alternative
loading paths:

- `mrb_load_string()` — compile from source and execute
- `mrb_load_irep()` — load precompiled `.mrb` bytecode
- `mrbc` tool — ahead-of-time compilation to `.mrb` or C array

## Source File Map

### Core (`src/`)

| File | Responsibility |
| ---- | -------------- |
| `vm.c` | Bytecode dispatch loop, method invocation |
| `state.c` | `mrb_state` init/close, irep management |
| `gc.c` | Garbage collector (mark-sweep, incremental) |
| `class.c` | Class/module definition, method tables |
| `object.c` | Core object operations |
| `variable.c` | Instance/class/global variables, object shapes |
| `proc.c` | Proc/Lambda/closure handling |
| `array.c` | Array implementation |
| `string.c` | String implementation (embedded, shared, heap) |
| `hash.c` | Hash implementation (open addressing) |
| `numeric.c` | Integer/Float arithmetic |
| `symbol.c` | Symbol table and interning |
| `range.c` | Range implementation |
| `error.c` | Exception creation, raise, backtrace |
| `kernel.c` | Kernel module methods |
| `load.c` | `.mrb` bytecode loading |
| `dump.c` | Bytecode serialization (write `.mrb`) |
| `print.c` | Print/puts/p output |
| `backtrace.c` | Stack trace generation |

### Compiler (`mrbgems/mruby-compiler/core/`)

| File | Responsibility |
| ---- | -------------- |
| `parse.y` | Yacc grammar → AST |
| `y.tab.c` | Generated parser (from parse.y) |
| `codegen.c` | AST → bytecode (irep) |
| `node.h` | AST node type definitions |

### Key Headers (`include/mruby/`)

| Header | Contents |
| ------ | -------- |
| `mruby.h` | `mrb_state`, core API declarations |
| `value.h` | `mrb_value`, type enums, value macros |
| `object.h` | `RBasic`, `RObject`, object header |
| `class.h` | `RClass`, method table types |
| `string.h` | `RString`, string macros |
| `array.h` | `RArray`, array macros |
| `hash.h` | `RHash`, hash API |
| `data.h` | `RData`, C data wrapping |
| `irep.h` | `mrb_irep`, bytecode structures |
| `compile.h` | Compiler context, `mrb_load_string` |
| `boxing_*.h` | Value boxing implementations |

## mrbgems System

Gems are the module system for mruby. Each gem lives in
`mrbgems/mruby-*/` and contains:

```text
mruby-example/
├── mrbgem.rake       gem specification (name, deps, bins)
├── src/              C source files
├── mrblib/           Ruby source files (compiled to bytecode)
├── include/          C headers
├── test/             mrbtest test files
└── bintest/          binary test files (CRuby)
```

At build time, gem Ruby files are compiled with `mrbc` and linked into
`libmruby.a`. Gem initialization runs in dependency order via
`gem_init.c` (auto-generated).

GemBoxes (`mrbgems/*.gembox`) define named collections of gems
(e.g., `default.gembox` includes `stdlib`, `stdlib-ext`, `stdlib-io`,
`math`, `metaprog`, and binary tools).
