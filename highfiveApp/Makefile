TOP=..

# Bootstrap-only targets must be runnable even when EPICS is not configured.
BOOTSTRAP_GOALS := ensure_highfive_submodule
NON_BOOTSTRAP_GOALS := $(filter-out $(BOOTSTRAP_GOALS),$(MAKECMDGOALS))
BOOTSTRAP_ONLY := $(if $(MAKECMDGOALS),$(if $(NON_BOOTSTRAP_GOALS),,yes),)

HIGHFIVE_SUBMODULE_PATH := $(TOP)/highfiveApp/HighFive
HIGHFIVE_SENTINEL := $(HIGHFIVE_SUBMODULE_PATH)/include/highfive/H5File.hpp

# Ensure HighFive is present before RULES expands header copy rules.
# This allows `make -C highfiveApp` to work from non-recursive clones.
ifneq ($(filter clean distclean realclean help,$(MAKECMDGOALS)),)
else
ifneq ($(wildcard $(HIGHFIVE_SENTINEL)),)
else
$(info HighFive submodule not found. Initializing non-recursively...)
HIGHFIVE_SUBMODULE_BOOTSTRAP := $(shell git -C "$(TOP)" submodule update --init highfiveApp/HighFive >/dev/null 2>&1)
endif
endif

ifneq ($(BOOTSTRAP_ONLY),yes)
include $(TOP)/configure/CONFIG

# Keep synced with upstream/fork header changes without hand-maintaining lists.
INC += $(wildcard HighFive/include/highfive/*.hpp)
INC += $(wildcard HighFive/include/highfive/bits/*.hpp)
INC += $(wildcard HighFive/include/highfive/h5easy_bits/*.hpp)
INC += $(wildcard HighFive/include/highfive/experimental/*.hpp)

include $(TOP)/configure/RULES

build: ensure_highfive_submodule
inc: ensure_highfive_submodule
install: ensure_highfive_submodule

$(INC): ensure_highfive_submodule
endif

.PHONY: ensure_highfive_submodule
ensure_highfive_submodule:
	@if [ ! -f "$(HIGHFIVE_SENTINEL)" ]; then \
		echo "HighFive submodule not found. Initializing non-recursively (Catch2 is not fetched)..."; \
		git -C "$(TOP)" submodule update --init highfiveApp/HighFive; \
	fi

