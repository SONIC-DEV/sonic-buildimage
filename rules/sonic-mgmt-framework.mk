# SONiC mgmt-framework package

ifeq ($(INCLUDE_MGMT_FRAMEWORK), y)

SONIC_MGMT_FRAMEWORK = sonic-mgmt-framework_1.0-01_amd64.deb
$(SONIC_MGMT_FRAMEWORK)_SRC_PATH = $(SRC_PATH)/sonic-mgmt-framework
$(SONIC_MGMT_FRAMEWORK)_DEPENDS = $(LIBYANG_DEV) $(LIBYANG)
$(SONIC_MGMT_FRAMEWORK)_RDEPENDS = $(LIBYANG)
SONIC_DPKG_DEBS += $(SONIC_MGMT_FRAMEWORK)

SONIC_MGMT_FRAMEWORK_DBG = sonic-mgmt-framework-dbg_1.0-01_amd64.deb
$(SONIC_MGMT_FRAMEWORK_DBG)_DEPENDS += $(SONIC_MGMT_FRAMEWORK)
$(SONIC_MGMT_FRAMEWORK_DBG)_RDEPENDS += $(SONIC_MGMT_FRAMEWORK)
$(eval $(call add_derived_package,$(SONIC_MGMT_FRAMEWORK),$(SONIC_MGMT_FRAMEWORK_DBG)))

endif
