# Makefile at top of application tree
TOP = .

# Bootstrap targets must run before EPICS build configuration is loaded.
ifneq ($(filter release-site-local RELEASE_SITE,$(MAKECMDGOALS)),)
else
include $(TOP)/configure/CONFIG

DIRS += configure
DIRS += managerApp
DIRS += nttableApp
DIRS += commonApp
DIRS += highfiveApp
DIRS += mergerApp
DIRS += writerApp
DIRS += test
DIRS += documentation
#DIRS += test

# All dirs except configure depend on configure
$(foreach dir, $(filter-out configure, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += configure))

# additional dependency rules:
commonApp_DEPEND_DIRS += nttableApp
mergerApp_DEPEND_DIRS += nttableApp
mergerApp_DEPEND_DIRS += commonApp
writerApp_DEPEND_DIRS += commonApp
writerApp_DEPEND_DIRS += highfiveApp

USR_CPPFLAGS += -std-c++11

include $(TOP)/configure/RULES_TOP

UNINSTALL_DIRS += $(wildcard $(INSTALL_LOCATION)/python*)
endif

# ---------------------------------------------------------------------------
# Dynamic RELEASE_SITE.local generation (machine-local override)
# ---------------------------------------------------------------------------
RELEASE_SITE_LOCAL := RELEASE_SITE.local


.PHONY: release-site-local
release-site-local:
	@echo "Generating $(RELEASE_SITE_LOCAL) from environment variables..."
	@if [ -z "$(EPICS_BASE)" ]; then echo "ERROR: EPICS_BASE env var is not set"; exit 1; fi
	@printf "#==============================================================================\n" > $(RELEASE_SITE_LOCAL)
	@printf "# Auto-generated machine-local override from EPICS_BASE\n" >> $(RELEASE_SITE_LOCAL)
	@printf "BASE_MODULE_VERSION=%s\n" "$(notdir $(patsubst %/,%,$(EPICS_BASE)))" >> $(RELEASE_SITE_LOCAL)
	@printf "EPICS_SITE_TOP=%s\n" "$(patsubst %/,%,$(dir $(patsubst %/,%,$(EPICS_BASE))))" >> $(RELEASE_SITE_LOCAL)
	@printf "EPICS_MODULES=%s/modules\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "IOC_SITE_TOP=%s/iocTop\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "EPICS_BASE=%s\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "PACKAGE_SITE_TOP=%s/package\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "MATLAB_PACKAGE_TOP=%s/package/matlab\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "PSPKG_ROOT=%s\n" "$(PSPKG_ROOT)" >> $(RELEASE_SITE_LOCAL)
	@printf "TOOLS_SITE_TOP=%s/tools\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "ALARM_CONFIGS_TOP=%s/tools/AlarmConfigsTop\n" "$(EPICS_BASE)" >> $(RELEASE_SITE_LOCAL)
	@printf "#==============================================================================\n" >> $(RELEASE_SITE_LOCAL)

# Keep tracked RELEASE_SITE unchanged; use release-site-local explicitly.
