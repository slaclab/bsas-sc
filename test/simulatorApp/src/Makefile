TOP=../..

include $(TOP)/configure/CONFIG

PROD_HOST = simulator
DBD += simulator.dbd

simulator_DBD += base.dbd
simulator_DBD += menuScan.dbd
simulator_DBD += PVAServerRegister.dbd
simulator_DBD += qsrv.dbd
simulator_DBD += pvxsIoc.dbd
simulator_DBD += simTableSup.dbd

simulator_SRCS += simulator_registerRecordDeviceDriver.cpp
simulator_SRCS += simulatorMain.cpp
simulator_SRCS += simTableSup.cpp

simulator_LIBS += pvxsIoc pvxs
simulator_LIBS += common nttable
simulator_LIBS += qsrv
simulator_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
simulator_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
