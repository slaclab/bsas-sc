# Makefile at top of application tree
TOP = .

# Bootstrap targets must run before EPICS build configuration is loaded.
# Keep `configure` here so `make configure` can generate RELEASE_SITE.local
# before loading EPICS build rules.
BOOTSTRAP_GOALS := release-site-local RELEASE_SITE configure
NON_BOOTSTRAP_GOALS := $(filter-out $(BOOTSTRAP_GOALS),$(MAKECMDGOALS))

# Skip EPICS config only when the user explicitly requested bootstrap goals
# and nothing else.  An empty MAKECMDGOALS means the default (full) build.
BOOTSTRAP_ONLY := $(if $(MAKECMDGOALS),$(if $(NON_BOOTSTRAP_GOALS),,yes),)

ifneq ($(BOOTSTRAP_ONLY),yes)
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
RELEASE_LOCAL := configure/RELEASE.local


.PHONY: configure
configure: release-site-local
	$(MAKE) -C configure


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
	@pvxs_path="$(PVXS)"; \
	if [ -n "$$pvxs_path" ] && [ ! -f "$$pvxs_path/dbd/pvxsIoc.dbd" ]; then pvxs_path=""; fi; \
	if [ -z "$$pvxs_path" ] && [ -f /opt/pvxs/dbd/pvxsIoc.dbd ]; then pvxs_path="/opt/pvxs"; fi; \
	if [ -z "$$pvxs_path" ] && [ -n "$(PVXS_BASE)" ] && [ -f "$(PVXS_BASE)/dbd/pvxsIoc.dbd" ]; then pvxs_path="$(PVXS_BASE)"; fi; \
	if [ -n "$$pvxs_path" ]; then \
	  printf "PVXS=%s\n" "$$pvxs_path" >> $(RELEASE_SITE_LOCAL); \
	fi
	@printf "#==============================================================================\n" >> $(RELEASE_SITE_LOCAL)
	@echo "Generating $(RELEASE_LOCAL) PVXS override..."
	@pvxs_path="$(PVXS)"; \
	if [ -n "$$pvxs_path" ] && [ ! -f "$$pvxs_path/dbd/pvxsIoc.dbd" ]; then pvxs_path=""; fi; \
	if [ -z "$$pvxs_path" ] && [ -f /opt/pvxs/dbd/pvxsIoc.dbd ]; then pvxs_path="/opt/pvxs"; fi; \
	if [ -z "$$pvxs_path" ] && [ -n "$(PVXS_BASE)" ] && [ -f "$(PVXS_BASE)/dbd/pvxsIoc.dbd" ]; then pvxs_path="$(PVXS_BASE)"; fi; \
	printf "# Auto-generated local overrides\n" > $(RELEASE_LOCAL); \
	if [ -n "$$pvxs_path" ]; then \
	  printf "PVXS=%s\n" "$$pvxs_path" >> $(RELEASE_LOCAL); \
	else \
	  printf "# PVXS=/path/to/pvxs\n" >> $(RELEASE_LOCAL); \
	fi

# Keep tracked RELEASE_SITE unchanged; use release-site-local explicitly.

# ---------------------------------------------------------------------------
# Convenience debug build targets
# ---------------------------------------------------------------------------
.PHONY: debug debug-clean

# Build with debug-friendly flags and no optimization from EPICS defaults.
debug:
	$(MAKE) HOST_OPT=NO CROSS_OPT=NO USR_CFLAGS='-O0 -g3' USR_CXXFLAGS='-O0 -g3'

# Clean + rebuild in debug mode.
debug-clean:
	$(MAKE) clean
	$(MAKE) debug
