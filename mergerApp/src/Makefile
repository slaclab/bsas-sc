TOP=../..

include $(TOP)/configure/CONFIG

USR_INCLUDES += -I$(PVXS)/include
USR_INCLUDES += -I$(TOP)/commonApp

PROD = merger

merger_LIBS += pvxs Com
merger_LIBS += common nttable

# pvxs may be installed as a static library, which requires explicit
# linkage to libevent on some environments.
merger_SYS_LIBS += event event_pthreads

merger_SRCS += mergerMain.cpp tablebuffer.cpp taligntable.cpp

include $(TOP)/configure/RULES
