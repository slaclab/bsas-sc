TOP=../../..

include $(TOP)/configure/CONFIG

USR_INCLUDES += -I$(PVXS)/include
USR_INCLUDES += -I$(TOP)/commonApp
USR_INCLUDES += -I$(TOP)/nttableApp

PROD_IOC = stacker
DBD += stacker.dbd

stacker_DBD += base.dbd
stacker_DBD += pvxsIoc.dbd
stacker_DBD += stackerSup.dbd

stacker_SRCS += stackerMain.cpp stackerSup.cpp
stacker_SRCS += stacker_registerRecordDeviceDriver.cpp

stacker_LIBS += pvxsIoc pvxs
stacker_LIBS += common nttable
stacker_LIBS += qsrv
stacker_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
stacker_LIBS += $(EPICS_BASE_IOC_LIBS)
stacker_SYS_LIBS += event event_pthreads

include $(TOP)/configure/RULES
