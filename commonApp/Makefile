TOP=..

include $(TOP)/configure/CONFIG

USR_INCLUDES += -I$(PVXS)/include
USR_INCLUDES += -I$(TOP)/nttableApp
USR_INCLUDES += -I$(TOP)/commonApp

INC += clipp.h
INC += tab/timetable.h

LIBRARY = common
common_SRCS += timetable.cpp

include $(TOP)/configure/RULES
