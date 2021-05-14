In order to buy some performance on the common, uninstrumented, fast path, we replace repeated
checks for both allocation instrumentation and allocator changes by  a single function table
dispatch, and templatized allocation code that can be used to generate either instrumented
or uninstrumented versions of allocation routines.

When we call an allocation routine, we always indirect through a thread-local function table that
either points to instrumented or uninstrumented allocation routines. The instrumented code has a
`kInstrumented` = true template argument (or `kIsInstrumented` in some places), the uninstrumented
code has `kInstrumented` = false.

The function table is thread-local. There appears to be no logical necessity for that; it just
makes it easier to access from compiled Java code.

- The function table is switched out by `InstrumentQuickAllocEntryPoints[Locked]`, and a
corresponding `UninstrumentQuickAlloc`... function.

- These in turn are called by `SetStatsEnabled()`, `SetAllocationListener()`, et al, which
require the mutator lock is not held.

- With a started runtime, `SetEntrypointsInstrumented()` calls `ScopedSupendAll(`) before updating
  the function table.

Mutual exclusion in the dispatch table is thus ensured by the fact that it is only updated while
all other threads are suspended, and is only accessed with the mutator lock logically held,
which inhibits suspension.

To ensure correctness, we thus must:

1. Suspend all threads when swapping out the dispatch table, and
2. Make sure that we hold the mutator lock when accessing it.
3. Not trust kInstrumented once we've given up the mutator lock, since it could have changed in the
    interim.

