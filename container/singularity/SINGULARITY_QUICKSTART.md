# Quick Start: BSAS-SC with Singularity

This document provides a quick-start guide for building and running BSAS-SC in a Singularity container instead of Docker.

## Installation

### 1. Install Singularity

For Rocky Linux 9:

```bash
sudo dnf install -y golang libseccomp-devel pkg-config squashfs-tools cryptsetup runc
cd /tmp
wget https://github.com/sylabs/singularity/releases/download/v3.11.4/singularity-3.11.4.tar.gz
tar -xzf singularity-3.11.4.tar.gz
cd singularity-3.11.4
./mconfig
make -C builddir
sudo make -C builddir install
singularity version
```

### 2. Build the Container

From the project root:

```bash
# Using the helper script (recommended)
./container/singularity/singularity.sh build

# Or using singularity directly
singularity build container/singularity/bsas-sc.sif container/singularity/Singularity
```

This creates a single file `container/singularity/bsas-sc.sif` (~2GB) containing the complete environment.

## Build the Project

### Using Helper Script

```bash
# Recommended - one command
./container/singularity/singularity.sh build-project
```

### Using singularity directly

```bash
singularity exec -B $(pwd):/work container/singularity/bsas-sc.sif bash -c \
  "cd /work && \
   export EPICS_BASE=/opt/epics/base && \
   make release-site-local && \
   make configure && \
   make -j"
```

## Running Applications

### Interactive Terminal

```bash
./container/singularity/singularity.sh shell
# Now you're inside the container
cd /work
export EPICS_BASE=/opt/epics/base
./bin/${EPICS_HOST_ARCH}/merger
```

### Run Commands Directly

```bash
# Merger application
./container/singularity/singularity.sh exec /work/bin/${EPICS_HOST_ARCH}/merger

# Writer application
./container/singularity/singularity.sh exec /work/bin/${EPICS_HOST_ARCH}/writer

# Manager application
./container/singularity/singularity.sh exec python3 /work/managerApp/bsasManager.py
```

## Run Tests

```bash
./container/singularity/singularity.sh test
```

## Available Helper Commands

```bash
./container/singularity/singularity.sh help

# Quick reference:
./container/singularity/singularity.sh build              # Build the image
./container/singularity/singularity.sh shell              # Interactive shell
./container/singularity/singularity.sh exec <command>     # Run a single command
./container/singularity/singularity.sh build-project      # Build BSAS-SC
./container/singularity/singularity.sh test               # Run tests
./container/singularity/singularity.sh clean              # Delete the image
```

## Advantages

| Feature | Singularity | Docker |
|---------|-------------|--------|
| Single portable file | ✅ | ❌ |
| HPC/Slurm compatible | ✅ | ❌ |
| Rootless by default | ✅ | ❌ (requires config) |
| Daemon required | ❌ (stateless) | ✅ (must run) |
| Permission handling | ✅ (preserves uid/gid) | ❌ (root issues) |
| Container size | ~2GB | ~2GB |

## Troubleshooting

### Build takes too long?

Use a sandbox for faster development:

```bash
singularity build --sandbox bsas-sc.sandbox container/singularity/Singularity
singularity shell bsas-sc.sandbox
```

### Container not found?

```bash
./container/singularity/singularity.sh build
```

### Permission issues?

Ensure you have proper permissions:

```bash
# Check Singularity installation
singularity version
whoami  # Should show your username, not root
```

## Using with Slurm (HPC)

If running on an HPC cluster with Slurm:

```bash
#!/bin/bash
#SBATCH --job-name=bsas-build
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=01:00:00

singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif bash -c \
  "cd /work && \
   export EPICS_BASE=/opt/epics/base && \
   make -j ${SLURM_CPUS_PER_TASK}"
```

## Files Created

- **container/singularity/Singularity** — Container definition file
- **container/singularity/bsas-sc.sif** — Built container image (created after build)
- **container/singularity/singularity.sh** — Helper script for easy management
- **container/singularity/SINGULARITY_GUIDE.md** — Detailed documentation

## Next Steps

- Read [SINGULARITY_GUIDE.md](./SINGULARITY_GUIDE.md) for advanced usage
- Check [README.md](../../README.md) for project documentation
- Run `./container/singularity/singularity.sh build-project` to build everything

---

**Need help?**

```bash
./container/singularity/singularity.sh help
```

Or see the full documentation in [SINGULARITY_GUIDE.md](./SINGULARITY_GUIDE.md)
