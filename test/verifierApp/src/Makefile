TOP=../../..

include $(TOP)/configure/CONFIG

PROD_HOST = verifier

USR_INCLUDES += $(shell pkg-config --cflags hdf5 2>/dev/null)
USR_INCLUDES += -I$(TOP)/highfiveApp/HighFive/include
USR_INCLUDES += -I$(HDF5_INCLUDE)/hdf5/serial
USR_INCLUDES += -I$(HDF5_INCLUDE)/hdf5
USR_INCLUDES += -I$(HDF5_INCLUDE)
USR_INCLUDES += -I$(ZLIB_INCLUDE)

verifier_LIBS += pvxs Com
verifier_LIBS += common nttable
verifier_SYS_LIBS += hdf5 z
verifier_SYS_LIBS += event event_pthreads

verifier_SRCS += verifierMain.cpp

include $(TOP)/configure/RULES
