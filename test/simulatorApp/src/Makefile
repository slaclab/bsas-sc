TOP=../../..

include $(TOP)/configure/CONFIG

PROD_IOC = simulator
DBD += simulator.dbd

simulator_DBD += base.dbd
simulator_DBD += menuScan.dbd
simulator_DBD += simTableSup.dbd

simulator_SRCS += simulatorMain.cpp
simulator_SRCS += simTableSup.cpp
simulator_SRCS += simulator_registerRecordDeviceDriver.cpp

simulator_LIBS += pvxsIoc pvxs
simulator_LIBS += common nttable
simulator_LIBS += qsrv
simulator_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
simulator_LIBS += $(EPICS_BASE_IOC_LIBS)
simulator_SYS_LIBS += event event_pthreads

include $(TOP)/configure/RULES
