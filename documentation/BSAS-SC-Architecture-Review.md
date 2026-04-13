# BSAS-SC Architecture Review

**Date:** 2026-04-13  
**Scope:** `mergerApp` and `writerApp`

---

## 1. Overall Context

BSAS-SC is an EPICS-based data acquisition system. It collects time-stamped tabular data from Channel Access / PVAccess PVs (NTTable format), merges streams from multiple sources, and archives them to HDF5 files. The data pipeline is:

```
[EPICS PVs] --> mergerApp --> (merged NTTable PV) --> writerApp --> HDF5 files
```

Two shared libraries underpin both apps:

- **`nttableApp`** — `nt::NTTable`: thin PVXS wrapper that enforces the NormativeTypes `epics:nt/NTTable:1.0` schema (labels + typed array columns).
- **`commonApp`** — `TimeTable` / `TimeTableValue`: higher-level abstraction that adds the mandatory trio of time columns (`secondsPastEpoch`, `nanoseconds`, `pulseId`) and typed column accessors.

---

## 2. mergerApp

### Purpose

`mergerApp` subscribes to a configurable list of NTTable PVs, time-aligns their samples across a sliding window, and publishes a single merged NTTable PV at a fixed period. It is the fan-in stage of the pipeline.

### Key Classes

| Class | File | Role |
|---|---|---|
| `Runnable` | `mergerMain.cpp` | Base thread abstraction wrapping `epicsThreadRunable`; posts itself to a shared `MPMCFIFO<Runnable*>` on natural death |
| `Listener` | `mergerMain.cpp` | One PVXS monitor subscription per input PV; drains events from a MPMCFIFO and pushes raw values into `TimeAlignedTable` |
| `Reactor` | `mergerMain.cpp` | Wakes every `period/5` seconds; checks whether enough data has accumulated and calls `extract()`, then `pv_.post()` |
| `TableBuffer` | `tablebuffer.h/.cpp` | Per-PV FIFO buffer of `TimeTableValue`; tracks earliest/latest timestamp; supports row-level iteration and consumption |
| `TimeAlignedTable` | `taligntable.h/.cpp` | Owns one `TableBuffer` per PV; mutex-protected; merges timestamps across buffers and builds the output `pvxs::Value` |

### Data Flow

1. `Listener::run()` pops `(col_idx, subscription)` pairs from its internal queue and calls `TimeAlignedTable::push(pvname, value)`.
2. `TimeAlignedTable::push()` holds `epicsMutex`, routes the value to the right `TableBuffer`, and lazily initialises the merged type when all buffers have received at least one sample.
3. `Reactor::run()` polls `get_timebounds()`, which aggregates per-buffer `TimeSpan`s into a `TimeBounds` struct. When the shortest available window exceeds `period_`, it calls `extract(start, end)`.
4. `extract()` collects a sorted union of timestamps from all buffers, copies matching rows (or fills zeroes and sets `valid=false` for gaps), and assembles the merged `pvxs::Value`.
5. The result is posted to a `pvxs::server::SharedPV` served by an embedded PVXS server.

### Threading Model

```
main thread
  ├── Listener thread   (epicsThread, medium priority)
  │     reads MPMCFIFO → calls TimeAlignedTable::push()
  └── Reactor thread    (epicsThread, medium priority)
        polls time bounds → calls TimeAlignedTable::extract() → pv_.post()
```

All shared state (`TimeAlignedTable`) is guarded by a single `epicsMutex` via `epicsGuard<epicsMutex>`. The death-queue pattern (`MPMCFIFO<Runnable*>`) lets main cleanly join whichever thread exits first, then shuts down the other.

---

## 3. writerApp

### Purpose

`writerApp` subscribes to a single NTTable PV (typically the output of `mergerApp`), receives batch updates, and appends them to time-partitioned HDF5 files. It is the persistence stage of the pipeline.

### Key Classes

| Class | File | Role |
|---|---|---|
| `Writer` | `writer.h/.cpp` | Owns one open HDF5 file; builds its group/dataset layout on the first update; appends chunks on every subsequent `write()` call |
| `main` | `writerMain.cpp` | CLI entry point; owns the PVXS subscription, the `epicsEvent` for wakeups, and the file-rotation outer loop |

### Data Flow

1. A `pvxs::client::Subscription` fires a lambda that signals an `epicsEvent`.
2. `main` wakes, drains the subscription queue with `pop()`, and passes each `pvxs::Value` to `writer->write()`.
3. On the very first call, `Writer` extracts the `TimeTable` type from the value and calls `build_file_structure()`:
   - Creates `/meta` (PV names, column names, labels, PVXS type codes) and `/data/<root_group>` HDF5 groups.
   - Creates one unlimited, chunked `H5::DataSet` per column, with HDF5 attributes for label and column name.
   - Per-PV sub-groups are inferred by splitting column names on a configurable separator (`_` default).
4. Subsequent calls resize each dataset by the number of new rows and write raw array data via `H5::DataSet::select().write_raw()`.
5. The outer loop in `main` handles file rotation by size (`--max-size-mb`) and duration (`--max-duration-sec`), creating a fresh `Writer` (and thus a new HDF5 file) each time a limit is hit. File paths follow a `YYYY/MM/DD/<prefix>_YYYYMMDD_hhmmss.h5` hierarchy.

### C++ Architecture Notes

- **Single-class writer**: `Writer` is a straightforward non-polymorphic class. There is no interface abstraction for the file backend, which is fine given the single responsibility.
- **`unique_ptr` ownership**: `file_` and `type_` are `std::unique_ptr`, cleanly expressing sole ownership and automatic RAII cleanup.
- **Lazy initialisation**: the `TimeTable` type and HDF5 structure are built on the first valid update, avoiding a schema negotiation step at startup.
- **Type dispatch via X-macro**: both `pvxs_to_h5_type()` and the `write()` switch use a `#define CASE / #undef CASE` pattern to keep the type mapping table compact and consistent. This is a common EPICS idiom.
- **`throw "string literal"`**: one error path in `pvxs_to_h5_type()` throws a raw `const char*` instead of a `std::exception` subclass. The main catch block in `writerMain.cpp` handles this explicitly, but it is fragile — a `std::runtime_error` would be preferable.
- **File size check via `stat()`**: the size limit check polls the filesystem after every write batch. This is simple and reliable but introduces a syscall per batch. Using HDF5's in-memory size tracking (`H5Fget_filesize`) would avoid the syscall.
- **No back-pressure on the subscription queue**: `queueSize=8` and `ackAny=1` are set, but if `write()` is slow the queue can drop updates silently. There is no warning logged for dropped samples on the writer side.
- **Single-threaded by design**: the entire write path runs on the main thread. No worker thread or ring buffer is used. This keeps the code simple but means a slow HDF5 flush will delay acknowledgement of the next PV update.

---

## 4. Cross-Cutting Observations

- **PVXS logging** (`DEFINE_LOGGER` / `log_*_printf`) is used consistently across both apps, which is good for runtime tuning via environment variables.
- **`clipp`** is used for CLI parsing in both apps, giving a consistent and self-documenting argument interface.
- **`epicsTime` / `epicsThread` / `epicsMutex`** primitives are used throughout rather than `std::thread` / `std::mutex`, which keeps the code portable across EPICS target architectures (including RTEMS/VxWorks where `<thread>` may be unavailable).
- **No unit tests** are present for `Writer`, `TableBuffer`, or `TimeAlignedTable` in the main source tree (only integration-level test IOCs under `test/`). The core alignment and copy logic in `taligntable.cpp` would benefit from targeted unit tests given its complexity.
- **Commented-out `dump()` body** in `taligntable.cpp` suggests the class went through a significant refactor and the debug utility was never updated. It should either be implemented or removed.
