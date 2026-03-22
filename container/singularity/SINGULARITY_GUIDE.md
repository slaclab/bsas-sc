# Singularity Container Setup for BSAS-SC

This guide explains how to build and use a Singularity container instead of Docker for the BSAS-SC project.

## Prerequisites

You need to have Singularity installed on your system. See [Singularity Installation Documentation](https://docs.sylabs.io/guides/latest/user-guide/quick-start.html).

### Installation on Rocky Linux 9

```bash
# Install Singularity Community Edition (SingularityCE)
dnf install -y golang libseccomp-devel pkg-config squashfs-tools cryptsetup runc
cd /tmp
wget https://github.com/sylabs/singularity/releases/download/v3.11.4/singularity-3.11.4.tar.gz
tar -xzf singularity-3.11.4.tar.gz
cd singularity-3.11.4
./mconfig
make -C builddir
sudo make -C builddir install
singularity version
```

## Building the Singularity Image

From the project root directory, build the container image:

```bash
# Build the image (this will take 10-20 minutes)
singularity build container/singularity/bsas-sc.sif container/singularity/Singularity

# The resulting image is a single file: container/singularity/bsas-sc.sif
```

## Running the Project

### Interactive Shell

Open an interactive shell in the container:

```bash
singularity shell -B "$(pwd):/work" container/singularity/bsas-sc.sif
```

Inside the container, you can run normal build commands:

```bash
cd /work
export EPICS_BASE=/opt/epics/base
make release-site-local    # Generate machine-local overrides
make configure             # Configure the build
make -j                    # Build in parallel
```

### Building Directly

Execute the build without entering the shell:

```bash
# Build the project
singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif bash -c "cd /work && \
  export EPICS_BASE=/opt/epics/base && \
  make release-site-local && \
  make configure && \
  make -j"
```

### Running Test Suite

```bash
singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif bash -c "cd /work/test && \
  export EPICS_BASE=/opt/epics/base && \
  ./run-test.sh"
```

### Running Specific Applications

```bash
# Run merger app
singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif /work/bin/linux-aarch64/merger

# Run writer app
singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif /work/bin/linux-aarch64/writer

# Run Python manager
singularity exec -B "$(pwd):/work" container/singularity/bsas-sc.sif python3 /work/managerApp/bsasManager.py
```

## Binding Directories

By default, Singularity binds your home directory and `/tmp`. To work with the project:

```bash
# If your project is not in a bound directory, bind it explicitly
singularity shell -B /path/to/bsas-sc:/work container/singularity/bsas-sc.sif

# Or bind additional directories for data output
singularity shell -B /data:/data -B /path/to/bsas-sc:/work container/singularity/bsas-sc.sif
```

## Customizing the Image

To modify the container (e.g., add different EPICS modules or dependencies):

1. Edit the `Singularity` definition file
2. Rebuild the image: `singularity build --force container/singularity/bsas-sc.sif container/singularity/Singularity`

**Common customizations:**
- Change the EPICS Base version in the `%post` section
- Add additional packages via `dnf install`
- Install additional EPICS modules
- Modify environment variables in the `%environment` section

## Container Architecture Notes

The definition file builds for `linux-aarch64` (ARM64) as specified in the existing build configuration. If you need a different architecture:

1. Edit the `Singularity` file
2. Change `EPICS_HOST_ARCH` references from `linux-aarch64` to your target (e.g., `linux-x86_64`)
3. Rebuild the image

## Advantages Over Docker

- **Single file**: The entire container is one `.sif` file (easy to distribute)
- **HPC-friendly**: Native support on supercomputers and clusters (Slurm, etc.)
- **Better permission handling**: Runs as unprivileged user by default
- **No daemon required**: No need to run a Singularity daemon service
- **Rootless containers**: Can build and run without root privileges (with proper setup)

## Troubleshooting

### Module builds fail
If EPICS modules fail to build, they may require specific EPICS Base configuration. Check:
- EPICS_BASE is set correctly
- EPICS_HOST_ARCH matches your system
- Module dependencies are installed

### Container build is too slow
Use `singularity build --sandbox` to create an editable directory instead of a `.sif` file during development:

```bash
singularity build --sandbox bsas-sc.sandbox container/singularity/Singularity
singularity shell bsas-sc.sandbox
```

### Permission denied errors
If you get permission errors, either:
1. Run with `sudo singularity` (not recommended)
2. Install Singularity with rootless mode enabled
3. Add your user to the singularity group (if configured)

## Additional Resources

- [Singularity Documentation](https://docs.sylabs.io/)
- [EPICS Base Building Guide](https://docs.epics-controls.org/en/latest/guides/building.html)
- [EPICS Modules Repository](https://github.com/epics-modules/)
