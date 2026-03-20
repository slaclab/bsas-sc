![logo](./documentation/SLAC-lab-hires.png)

# Beam Synchronous Acquisition Service
    
Doug Murray, SLAC, May 2022<br />
Revision 1.0, 04-May-2022, Initial Version.

BSAS is the **Beam Synchronous Acquisition Service**, which acquires data synchronized with each beam pulse from devices in SLAC's LCLS Accelerator, and records it in files using the Hierarchical Data Format (HDF).

BSAS-SC refers to the system associated with the superconducting Linac (*LCLS-II*), while BSAS-NC is associated with the normal conducting Linac (*LCLS-I*).

## Installing

This software is typically installed on a supported Linux system at SLAC, such as a Redhat Enterprise Linux server, version 7 or higher.

> **NOTE:**
> This software will not currently build on Windows or standard MacOS systems having case-insensitive file systems.

### Development

Download the files to start development work.  Use **git** to retrieve the source code and documentation:

```shell
git clone --recursive https://github.com/drm-slac/bsas-sc.git
```

This will retrieve the software with standard EPICS directories already configured for use at SLAC.  The *recursive* flag is required because the HDF template library is included as a submodule.
The resulting directory is named *bsas-sc* by default and contains several entries:

<center><caption>*Table 1*.  **Files and Directories**</caption></center>

| File or Directory Name | Purpose |
|---:|:---|
| **commonApp**/     | Template and Code libraries used in more than one program |
| **configure**/     | Files used to build software in the EPICS environment |
| **documentation**/ | Documents in their raw format, used to build final documents in the *doc* directory, created when the **make** command runs |
| **highfiveApp**/   | The submodule suporting the HDF5 format for generated data files |
| Makefile           | Used by the **make** tool to build the software |
| **managerApp**/    | Software to manage the running processes which acquire or record the BSAS data |
| **mergerApp**/     | Software which acquires data from the EPICS IOCs and makes it available in a single, merged table |
| **nttableApp**/    | Software which support the NTTable type in EPICS v7, and provides a template for the merged output table's format |
| README.md          | This file |
| RELEASE_SITE       | Tracked site baseline for production/shared defaults. |
| RELEASE_SITE.local.template | Template for machine-local EPICS overrides (copy to `RELEASE_SITE.local`, which is ignored). |
| **test**/          | Contains several programs capable of testing various aspects of the EPICS environment |
| **writerApp**/     | Software which reads the merged NTTable from the **mergerApp**, either locally or across a network, and records that data in HDF5 format |

### Building

The project uses a tracked production baseline plus optional local override files.

Configuration precedence (lowest to highest):

1. `RELEASE_SITE` (tracked baseline for production/shared defaults)
2. `RELEASE_SITE.local` (local root override, ignored by git)
3. `configure/RELEASE.local` (local EPICS/module override, ignored by git)

Recommended local workflow (dev container and developer machines):

```shell
# 1) Set EPICS Base for this shell
export EPICS_BASE=/opt/local

# 2) Generate machine-local root override from environment
make release-site-local

# 3) (Optional one-time) create configure-local override file
cp configure/RELEASE.local.template configure/RELEASE.local

# 4) Build
make configure
make -j
```

When `EPICS_BASE` changes:

```shell
export EPICS_BASE=/new/epics/base/path
make release-site-local
make configure
make -j
```

Clean rebuild:

```shell
make clean
make configure
make -j
```

Build output is installed under architecture-specific subdirectories in `bin/` and `lib/`. Documentation is generated under `doc/`.

### Running Test IOCs

The helper script `test/run-test-ioc.sh` starts each test IOC in its own `tmux` session so processes stay alive after the launching shell exits.

Start all test IOCs:

```shell
bash test/run-test-ioc.sh
```

List running IOC sessions:

```shell
tmux ls
```

Attach to one IOC console:

```shell
tmux attach -t ioc-simulator-table-stat
```

Detach without stopping the IOC: press `Ctrl+b`, then `d`.

Protocol notes for reading PVs:

1. PVA table PVs (published by PVXS) must be read with `pvget`, for example:

```shell
pvget SIM:STAT:0
pvget SIM:STAT:1
pvget SIM:TABLE:0
```

2. Classic CA records are read with `caget`, for example:

```shell
caget SIM:STAT_ASUB
caget SIM:SCALAR:0
```

If `caget` times out for `SIM:STAT:0` / `SIM:STAT:1`, this is expected because those are PVA-only PVs.

### Machine-local overrides (optional)

Use local override files to avoid editing tracked production files when switching machines.

```shell
# One-time setup on a machine
cp RELEASE_SITE.local.template RELEASE_SITE.local
cp configure/RELEASE.local.template configure/RELEASE.local

# Edit values for this machine
# Example:
#   EPICS_BASE = /opt/local
```

The `RELEASE_SITE.local` and `configure/RELEASE.local` files are git-ignored and already included by `configure/RELEASE`.
The template files `RELEASE_SITE.local.template` and `configure/RELEASE.local.template` are tracked in git and can be updated normally.

After creating or editing local overrides, build with:

```shell
make configure
make -j
```

Use `make release-site-local` when you want to regenerate `RELEASE_SITE.local` from the current shell environment. This does not modify tracked `RELEASE_SITE`.

If your site/module paths also change across machines, you can add additional overrides to `configure/RELEASE.local`, for example:

```make
EPICS_MODULES = /opt/local/modules
IOCADMIN = $(EPICS_MODULES)/iocAdmin/R3.1.16-1.3.2
AUTOSAVE = $(EPICS_MODULES)/autosave/R5.8-2.1.0
CAPUTLOG = $(EPICS_MODULES)/caPutLog/R3.5-1.0.0
PVXS = $(EPICS_MODULES)/pvxs/R1.2.2-0.2.0
```

### Deploying / Publishing

The **mergerApp** and **writerApp** are primary components which are started and restarted as necessary by the **managerApp**.  When working on different host computers, as they typically are, the **mergerApp** and **writerApp** each have their own instance of the **managerApp** monitoring them.  If the merger and writer are working on the same host computer, for testing scenarios for example, a single manager instance can monitor them both.jj

## Features

The system can acquire data from several IOCs, gathered to one server then transmit that data to another computer to record the data is a specific format.

## Configuration

The **mergerApp**, **writerApp** and **managerApp** can be configured by editing a simple ascii file.
More details can be found in the **BSAS-SC Users Manual** located in the *doc* directory.

## Writer HDF5 File Structure

The writer stores each output file with two top-level groups:

- `/data` contains time-series datasets and grouped signal data.
- `/meta` contains lookup arrays describing PV names, labels, columns, and data types.

Inside `/data`, the writer creates an additional group named by the `--root-group` argument.
In test runs this is often `data`, which gives the path `/data/data`.

Example structure:

```text
/
|-- data/
|   `-- <root-group>/
|       |-- secondsPastEpoch
|       |-- nanoseconds
|       |-- pulseId
|       |-- tbl0/
|       |-- tbl0_pv0/
|       |-- tbl0_pv1/
|       |-- tbl1/
|       |-- tbl1_pv0/
|       `-- ...
`-- meta/
	|-- pvnames
	|-- column_prefixes
	|-- columns
	|-- labels
	`-- pvxs_types
```

Notes:

- `secondsPastEpoch`, `nanoseconds`, and `pulseId` are extendable datasets aligned row-by-row with all signal data.
- Group names like `tbl0`, `tbl1`, `tbl0_pv0`, `tbl1_pv1`, etc. depend on incoming column prefixes from the merged NTTable.
- `meta/pvnames` and `meta/column_prefixes` map PV names to column-prefix groups.
- `meta/columns`, `meta/labels`, and `meta/pvxs_types` describe each written column in order.
