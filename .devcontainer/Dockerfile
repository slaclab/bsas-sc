ARG BASE_OS_IMAGE=rockylinux/rockylinux:9.3
FROM ${BASE_OS_IMAGE}

# Allow specifying the default compiler: gcc or llvm
ARG COMPILER=gcc
# EPICS Base release to build
ARG EPICS_VERSION=R7.0.8.1
# PVXS version to build
ARG PVXS_VERSION=1.4.1

# Install common dependencies and compiler-specific packages
RUN dnf update -y && \
    dnf groupinstall -y "Development Tools" && \
    dnf install -y dnf-plugins-core epel-release && \
    dnf config-manager --set-enabled crb && \
    dnf install -y rocky-release-hpc && \
    dnf install -y doxygen cmake git tcl ninja-build protobuf-compiler grpc-plugins grpc-devel gdb openssl-devel cyrus-sasl-devel libcurl-devel libicu-devel jq file sudo clang libevent-devel ca-certificates perl perl-Pod-Html perl-podlators hdf5-devel tmux && \
    (dnf install -y go-toolset lcov valgrind clang-tools-extra lldb lld || echo "Warning: optional packages unavailable on this Rocky target; continuing" >&2) && \
    if [ ! -x /usr/bin/go ]; then \
      for p in /usr/lib/golang/bin/go /opt/rh/go-toolset/root/usr/bin/go /usr/local/go/bin/go; do \
        if [ -x "$p" ]; then ln -s "$p" /usr/bin/go; break; fi; \
      done; \
    fi && \
    dnf clean all && \
    rm -rf /var/cache/dnf

# Install NWA (License Header Management)
RUN GO_BIN="$(command -v go || true)" && \
    if [ -z "$GO_BIN" ]; then \
      for p in /usr/lib/golang/bin/go /opt/rh/go-toolset/root/usr/bin/go /usr/local/go/bin/go; do \
        if [ -x "$p" ]; then GO_BIN="$p"; break; fi; \
      done; \
    fi && \
    if [ -n "$GO_BIN" ]; then \
      GOBIN=/usr/local/bin "$GO_BIN" install github.com/B1NARY-GR0UP/nwa@latest || \
      echo "Warning: nwa install failed; continuing without nwa" >&2; \
    else \
      echo "Warning: go binary not found after installing golang; continuing without nwa" >&2; \
      find /usr -path '*/bin/go' -type f 2>/dev/null | head -20 >&2 || true; \
    fi

# Set default compiler based on the COMPILER argument BEFORE building EPICS
RUN set -eux; \
    if [ "$COMPILER" = "llvm" ]; then \
        CC_BIN="$(command -v clang || true)"; \
        CXX_BIN="$(command -v clang++ || true)"; \
    else \
        CC_BIN="$(command -v gcc || true)"; \
        CXX_BIN="$(command -v g++ || true)"; \
    fi; \
    if [ -z "$CC_BIN" ] || [ -z "$CXX_BIN" ]; then \
        echo "Compiler binaries not found for COMPILER=$COMPILER" >&2; \
        command -v clang clang++ gcc g++ cc c++ || true; \
        exit 127; \
    fi; \
    ln -sf "$CC_BIN" /usr/bin/cc; \
    ln -sf "$CXX_BIN" /usr/bin/c++

# Verify the compiler selection
RUN echo "Building with:" && cc --version

RUN git clone --branch ${EPICS_VERSION} --depth 1 --recursive https://github.com/epics-base/epics-base.git /opt/epics
WORKDIR /opt/epics
# Configure EPICS to use the selected compiler
RUN if [ "$COMPILER" = "llvm" ]; then \
        echo "GNU_DIR=" > configure/os/CONFIG_SITE.Common.linux-aarch64 && \
        echo "CC = clang" >> configure/os/CONFIG_SITE.Common.linux-aarch64 && \
        echo "CCC = clang++" >> configure/os/CONFIG_SITE.Common.linux-aarch64 && \
        echo "CMPLR_CLASS = clang" >> configure/os/CONFIG_SITE.Common.linux-aarch64 && \
        echo "GNU_DIR=" > configure/os/CONFIG_SITE.Common.linux-x86_64 && \
        echo "CC = clang" >> configure/os/CONFIG_SITE.Common.linux-x86_64 && \
        echo "CCC = clang++" >> configure/os/CONFIG_SITE.Common.linux-x86_64 && \
        echo "CMPLR_CLASS = clang" >> configure/os/CONFIG_SITE.Common.linux-x86_64 && \
        mkdir -p include/compiler/clang; \
    fi
RUN make -j 4 INSTALL_LOCATION=/opt/local && \
    make install INSTALL_LOCATION=/opt/local && \
    mkdir -p /opt/local/include/compiler/gcc /opt/local/include/compiler/clang && \
    if [ -f /opt/local/include/compiler/gcc/compilerSpecific.h ] && [ ! -e /opt/local/include/compiler/clang/compilerSpecific.h ]; then \
        ln -s ../gcc/compilerSpecific.h /opt/local/include/compiler/clang/compilerSpecific.h; \
    fi && \
    if [ -f /opt/local/include/compiler/clang/compilerSpecific.h ] && [ ! -e /opt/local/include/compiler/gcc/compilerSpecific.h ]; then \
        ln -s ../clang/compilerSpecific.h /opt/local/include/compiler/gcc/compilerSpecific.h; \
    fi

# Set EPICS environment variables
ENV EPICS_BASE=/opt/local

# Determine EPICS host architecture and ensure toolchain paths exist
RUN EPICS_ARCH=$(/opt/epics/startup/EpicsHostArch) && \
    if [ -z "$EPICS_ARCH" ]; then \
        arch=$(uname -m) && \
        case "$arch" in \
            x86_64) EPICS_ARCH="linux-x86_64" ;; \
            aarch64|arm64) EPICS_ARCH="linux-aarch64" ;; \
            *) EPICS_ARCH="linux-$arch" ;; \
        esac; \
    fi && \
    echo "$EPICS_ARCH" > /etc/epics_host_arch && \
    printf 'export EPICS_HOST_ARCH=%s\n' "$EPICS_ARCH" >> /etc/profile.d/epics.sh && \
    printf 'export PATH=/opt/local/bin/%s:$PATH\n' "$EPICS_ARCH" >> /etc/profile.d/epics.sh && \
    printf 'export EPICS_HOST_ARCH=%s\n' "$EPICS_ARCH" >> /etc/bash.bashrc && \
    printf 'export PATH=/opt/local/bin/%s:$PATH\n' "$EPICS_ARCH" >> /etc/bash.bashrc && \
    printf 'EPICS_HOST_ARCH=%s\n' "$EPICS_ARCH" >> /etc/environment && \
    if [ ! -d /opt/epics/bin/${EPICS_ARCH} ] && [ -d /opt/local/bin/${EPICS_ARCH} ]; then \
        mkdir -p /opt/epics/bin && \
        ln -s /opt/local/bin/${EPICS_ARCH} /opt/epics/bin/${EPICS_ARCH}; \
    fi && \
    if [ ! -d /opt/epics/lib/${EPICS_ARCH} ] && [ -d /opt/local/lib/${EPICS_ARCH} ]; then \
        mkdir -p /opt/epics/lib && \
        ln -s /opt/local/lib/${EPICS_ARCH} /opt/epics/lib/${EPICS_ARCH}; \
    fi

# Build PVXS
RUN git clone --branch ${PVXS_VERSION} --depth 1 https://github.com/epics-base/pvxs.git /opt/pvxs
WORKDIR /opt/pvxs
RUN arch=$(cat /etc/epics_host_arch) && \
    echo "EPICS_BASE=/opt/local" > configure/RELEASE.local && \
    echo "EPICS_HOST_ARCH=$arch" >> configure/RELEASE.local && \
    echo "INSTALL_LOCATION=/opt/local" >> configure/RELEASE.local && \
    EPICS_HOST_ARCH=$arch make -j 4 && \
    EPICS_HOST_ARCH=$arch make install && \
    if ! ls /opt/local/lib/${arch}/libpvxs* >/dev/null 2>&1; then \
        mkdir -p /opt/local/lib/${arch} && \
        cp -a /opt/pvxs/lib/${arch}/. /opt/local/lib/${arch}/; \
    fi && \
    if ! ls /opt/local/include/pvxs/version.h >/dev/null 2>&1; then \
        mkdir -p /opt/local/include && \
        cp -a /opt/pvxs/include/. /opt/local/include/; \
    fi

# Set PVXS environment variable
ENV PVXS_BASE=/opt/local

# Set the working directory
WORKDIR /workspace

# Define a default command to run when the container starts (optional)
CMD ["bash"]
