% BSAS-SC Module Review
% BSAS-SC
% 

# BSAS-SC Module Review

## Purpose

- Clarify the runtime roles of `mergerApp`, `writerApp`, and `managerApp`
- Answer whether `managerApp` is essential to the data path
- Distinguish core data flow from operational supervision

## Executive Summary

- `mergerApp` merges multiple NTTable PVs into one live table
- `writerApp` subscribes to the merged table and writes HDF5 output
- `managerApp` supervises the other processes, but does not create or record data

## Core Data Path

Source NTTable PVs

`mergerApp`

Merged NTTable PV

`writerApp`

HDF5 output

## `mergerApp`

- Subscribes to the input NTTable PV list
- Collects updates from source PVs
- Aligns and merges incoming data into a single table
- Publishes the merged NTTable for downstream consumers

## `writerApp`

- Subscribes to the merged NTTable PV
- Waits for the input PV to become available
- Writes HDF5 output files
- Organizes output into date-based directories

## `managerApp`

- Starts `mergerApp` and/or `writerApp`
- Watches the PV list file for changes
- Restarts managed processes when required
- Maintains lock and audit handling

## Can `managerApp` Be Avoided?

- Yes, for manual runs, short experiments, or controlled demonstrations
- Yes, if process startup and restart are managed by the operator
- No, if continuous operation and automatic restart behavior are required

## Practical Conclusion

- `mergerApp` is essential for merging source data
- `writerApp` is essential for HDF5 archival
- `managerApp` is operationally useful, but not part of the essential data path

## Takeaway

- Core runtime: `mergerApp` + `writerApp`
- Optional supervisor: `managerApp`
- Short answer: `managerApp` can be avoided only when manual process management is acceptable
