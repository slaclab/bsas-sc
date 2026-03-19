# Makefile at top of application tree
TOP = ..
include $(TOP)/configure/CONFIG

# Directories to build, any order
# The test subtree does not need its own configure recursion; build the
# actual app and boot directories directly.
DIRS += $(wildcard *Sup)
DIRS += $(wildcard *App)
DIRS += $(wildcard *Top)


# Any *App dirs depend on all *Sup dirs
$(foreach dir, $(filter %App, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup, $(DIRS))))

# Any *Top dirs depend on all *Sup and *App dirs
$(foreach dir, $(filter %Top, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup %App, $(DIRS))))

# iocBoot depends on all *App dirs
iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))


USR_CPPFLAGS += -std-c++11

include $(TOP)/configure/RULES_TOP
