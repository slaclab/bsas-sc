TOP=../..

include $(TOP)/configure/CONFIG

# =====================================================
# Path to "NON EPICS" External PACKAGES: USER INCLUDES
# =====================================================
USR_INCLUDES += $(shell pkg-config --cflags hdf5 2>/dev/null)
USR_INCLUDES += -I$(TOP)/highfiveApp/HighFive/include
USR_INCLUDES += -I$(HDF5_INCLUDE)/hdf5/serial
USR_INCLUDES += -I$(HDF5_INCLUDE)/hdf5
USR_INCLUDES += -I$(HDF5_INCLUDE)
USR_INCLUDES += -I$(ZLIB_INCLUDE)

# ======================================================
# Link against the system-provided HDF5 and zlib libraries. The build
# environment for this project does not provide static package archives.
# ======================================================
writer_SYS_LIBS += hdf5 z


# ======================================================
# Host Application
# ======================================================
# Build this as a single host executable. Defining both PROD_HOST and PROD
# for the same basename causes EPICS to generate duplicate install rules.
PROD_HOST = writer

writer_LIBS += pvxs Com
writer_LIBS += common nttable

# pvxs may be installed as a static library, which requires explicit
# linkage to libevent on some environments.
writer_SYS_LIBS += event event_pthreads

writer_SRCS += writerMain.cpp writer.cpp

# HDF5 dependency (a bit hacky)
# HDF5 can be installed with headers outside the package tree, so we keep
# pkg-config in the include search path as a fallback for dynamically
# configured environments.

include $(TOP)/configure/RULES
