# BSAS-SC Module Review

This note summarizes the role of the main runtime components in BSAS-SC and highlights whether each one is part of the essential data path or only operational support.

The three components reviewed here are:

- `managerApp`
- `mergerApp`
- `writerApp`

## Executive Summary

BSAS-SC is built around a simple pipeline:

1. `mergerApp` acquires multiple NTTable PVs and publishes one merged NTTable.
2. `writerApp` subscribes to that merged table and writes HDF5 output.
3. `managerApp` supervises the lifecycle of the other two processes.

The most important conclusion is that `managerApp` is not part of the data acquisition or data recording logic. It is an orchestration layer. That means it can be avoided in some deployments, but only if the operator is willing to manage process startup, restart, and PV-list changes manually.

## Component Roles

### `mergerApp`

`mergerApp` is the upstream data aggregation service.

What it does:

- Subscribes to the input NTTable PV list.
- Collects updates from the source PVs.
- Aligns and merges the incoming data into a single table.
- Publishes the merged NTTable for downstream consumers.

Functional value:

- This is a core runtime component.
- It performs the actual acquisition and consolidation step.
- Without it, the system does not produce the merged table that the writer expects.

Operational notes:

- The merger is sensitive to the health of the source PVs, but the implementation is designed to tolerate missing or disconnected inputs rather than fail the whole system immediately.
- It is the logical boundary between raw source data and the final recordable stream.

### `writerApp`

`writerApp` is the downstream archival service.

What it does:

- Subscribes to the merged NTTable PV.
- Waits for the input PV to become available.
- Writes output files in HDF5 format.
- Organizes files into date-based directory structures.

Functional value:

- This is also a core runtime component.
- It performs the data persistence step.
- It is the component that turns live merged data into offline analysis files.

Operational notes:

- The writer depends on a compatible merged PV being present.
- In the current architecture, that PV is normally produced by `mergerApp`, but the writer only cares that the PV exists and follows the expected structure.

### `managerApp`

`managerApp` is the supervision and restart layer.

What it does:

- Starts `mergerApp` and/or `writerApp`.
- Watches the PV list file for changes.
- Restarts the merger when the input list changes.
- Restarts the writer when the configured workflow requires it.
- Maintains lock and audit handling for the managed processes.

Functional value:

- `managerApp` is not responsible for acquiring data.
- `managerApp` is not responsible for writing data.
- Its purpose is operational stability.

This makes `managerApp` very useful in long-running deployments, but not strictly required for every use case.

## Can `managerApp` Be Avoided?

Yes, in some cases.

You can avoid `managerApp` when:

- you want to run `mergerApp` and `writerApp` manually,
- you do not need automatic restart behavior,
- you do not need automatic response to PV-list edits,
- you are doing a test run, a short-lived experiment, or a controlled demonstration.

You should keep `managerApp` when:

- the system is expected to run continuously,
- process restart must happen automatically,
- the PV list can change over time,
- the deployment needs a single controller for one host or one beamline role.

Practical conclusion:

- `managerApp` is optional from a pure functionality point of view.
- `managerApp` is important from an operations and reliability point of view.

## Architecture View

```text
Source NTTable PVs
        |
        v
   mergerApp
        |
        v
Merged NTTable PV
        |
        v
   writerApp
        |
        v
   HDF5 output

managerApp supervises mergerApp and writerApp
```

## Functional Review

### What is essential

- `mergerApp` is essential when multiple PVs must be combined into one coherent table.
- `writerApp` is essential when the goal is HDF5 archival.
- Together, they implement the main BSAS-SC data path.

### What is support infrastructure

- `managerApp` improves uptime and simplifies operations.
- It does not add new scientific data content.
- It does not replace the merger or the writer.

### Best-fit usage model

- Use `mergerApp` and `writerApp` as the runtime core.
- Use `managerApp` when the deployment needs supervision, restart logic, or easy management of long-lived services.

## Presentation Takeaway

The cleanest summary for a presentation is:

- `mergerApp` creates the merged live table.
- `writerApp` records that table to HDF5.
- `managerApp` keeps the workflow running, but it is not the data path itself.

So the answer to “can `managerApp` be avoided?” is:

- **Yes**, if you accept manual process management.
- **No**, if you need automated supervision and restart behavior.

