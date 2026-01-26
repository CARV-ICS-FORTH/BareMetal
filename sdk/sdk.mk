# BareMetal SDK Build System
# Builds libplatform_<target>.a libraries with LTO metadata for each target

# Include common build configuration (provides absolute paths)
include build.mk

# Discover all targets from subdirectories under targets/
ALL_TARGETS := $(notdir $(wildcard $(SDK_TARGETS_DIR)/*))

# Object directories
OBJ_DIR = $(BUILD_DIR)/.obj
YALIBC_OBJ_DIR = $(OBJ_DIR)/yalibc
PLATFORM_OBJ_DIR = $(OBJ_DIR)/platform

# SDK CFLAGS (SDK includes already in CFLAGS from build.mk)
SDK_CFLAGS = $(CFLAGS) -DDEBUG

# Source files
YALIBC_SOURCES = $(wildcard yalibc/src/*.c)
PLATFORM_C_SOURCES = $(wildcard platform/src/*.c)
PLATFORM_S_SOURCES = $(wildcard platform/src/*.S)
TESTSUITE_SOURCES = $(wildcard testsuite/*.c testsuite/yalibc/*.c testsuite/platform/*.c)

# Object directories for testsuite
TESTSUITE_OBJ_DIR = $(OBJ_DIR)/testsuite

# Object files - yalibc is platform-independent, only platform is target-specific
YALIBC_OBJS = $(patsubst yalibc/src/%.c,$(YALIBC_OBJ_DIR)/%.o,$(YALIBC_SOURCES))

# Generate per-target object lists
define PLATFORM_OBJS
PLATFORM_C_OBJS_$(1) = $$(patsubst platform/src/%.c,$$(PLATFORM_OBJ_DIR)/%.$(1).o,$$(PLATFORM_C_SOURCES))
PLATFORM_S_OBJS_$(1) = $$(patsubst platform/src/%.S,$$(PLATFORM_OBJ_DIR)/%.$(1).S.o,$$(PLATFORM_S_SOURCES))
ALL_OBJS_$(1) = $$(YALIBC_OBJS) $$(PLATFORM_C_OBJS_$(1)) $$(PLATFORM_S_OBJS_$(1))
endef

$(foreach target,$(ALL_TARGETS),$(eval $(call PLATFORM_OBJS,$(target))))

# Linker script template (SDK internal)
LDSCRIPT_TEMPLATE = $(SDK_PLATFORM_INCLUDE)/templates/bmbase.ld.tmpl

# Generate library and linker script names for all targets
LIBS = $(foreach target,$(ALL_TARGETS),$(BUILD_DIR)/libplatform_$(target).a)
LDSCRIPTS = $(foreach target,$(ALL_TARGETS),$(LDSCRIPT_DIR)/bmmap.$(target).ld)
TESTSUITE_BINS = $(foreach target,$(ALL_TARGETS),$(BUILD_DIR)/bm_testsuite.$(target))

# Testsuite linker script
TESTSUITE_LDSCRIPT = testsuite/test_sections.ld

.PHONY: all clean libs ldscripts testsuite test dtb help

all: ldscripts libs testsuite

help:
	@echo "BareMetal Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build SDK (libraries, linker scripts, testsuite) for all targets"
	@echo "  sdk              - Same as 'all'"
	@echo "  clean            - Clean all build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Target-specific operations:"
	@echo "  make TARGET=<target> test     - Run testsuite (e.g., TARGET=qemu or TARGET=riser)"
	@echo "  make TARGET=<target> dtb      - Dump device tree to current directory"
	@echo ""
	@echo "Environment variables:"
	@echo "  TFTPROOT=<path>  - Enable TFTP support in QEMU (optional)"
	@echo "  DTB_PATH=<path>  - Directory to dump device tree (default: current directory)"
	@echo "  V=1              - Verbose build output"
	@echo ""
	@echo "Available hardware targets: $(ALL_TARGETS)"

ldscripts: $(LDSCRIPTS)

libs: $(LIBS)

testsuite: $(TESTSUITE_BINS)

# Test target - requires TARGET variable to be set
test:
ifndef TARGET
	@echo "Error: TARGET not specified. Usage: make TARGET=<target> test"
	@echo "Available targets: $(ALL_TARGETS)"
	@exit 1
endif
	@if [ ! -f $(SDK_TARGETS_DIR)/$(TARGET)/run.sh ]; then \
		echo "Error: No run.sh script found for target $(TARGET)"; \
		exit 1; \
	fi
	@if [ ! -f $(BUILD_DIR)/bm_testsuite.$(TARGET).bin ]; then \
		echo "Error: Testsuite binary not found. Run 'make' first."; \
		exit 1; \
	fi
	@echo "Running testsuite for target: $(TARGET)"
	@ORIGINAL_PWD=$(ORIGINAL_PWD) bash $(SDK_TARGETS_DIR)/$(TARGET)/run.sh $(BUILD_DIR)/bm_testsuite.$(TARGET).bin

# DTB dump target - requires TARGET variable to be set
dtb:
ifndef TARGET
	@echo "Error: TARGET not specified. Usage: make TARGET=<target> dtb"
	@echo "Available targets: $(ALL_TARGETS)"
	@exit 1
endif
	@DTB_PATH=$${DTB_PATH:-$(ORIGINAL_PWD)} ORIGINAL_PWD=$(ORIGINAL_PWD) bash $(SDK_TARGETS_DIR)/$(TARGET)/run.sh

# Create directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(LDSCRIPT_DIR): | $(BUILD_DIR)
	@mkdir -p $(LDSCRIPT_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)

$(YALIBC_OBJ_DIR): | $(OBJ_DIR)
	@mkdir -p $(YALIBC_OBJ_DIR)

$(PLATFORM_OBJ_DIR): | $(OBJ_DIR)
	@mkdir -p $(PLATFORM_OBJ_DIR)

$(TESTSUITE_OBJ_DIR): | $(OBJ_DIR)
	@mkdir -p $(TESTSUITE_OBJ_DIR)

# Generate linker scripts - pattern rule for any target
$(LDSCRIPT_DIR)/bmmap.%.ld: $(LDSCRIPT_TEMPLATE) | $(LDSCRIPT_DIR)
	$(MSG) "  [CPP]  $@"
	$(Q)$(CPP) -I $(SDK_PLATFORM_INCLUDE) -I $(SDK_TARGETS_DIR)/$* -P -o $@ $<

# Build yalibc objects (platform-independent)
$(YALIBC_OBJ_DIR)/%.o: yalibc/src/%.c | $(YALIBC_OBJ_DIR) $(BUILD_DIR)
	$(MSG) "  [CC]   $@"
	$(Q)$(CC) $(SDK_CFLAGS) -c $< -o $@

# Build testsuite objects (common for all targets, but need to be built per-target for includes)
define TESTSUITE_OBJ_RULES
TESTSUITE_OBJS_$(1) = $$(patsubst %.c,$$(TESTSUITE_OBJ_DIR)/%.$(1).o,$$(notdir $$(TESTSUITE_SOURCES)))

$$(TESTSUITE_OBJ_DIR)/%.$(1).o: testsuite/%.c | $$(TESTSUITE_OBJ_DIR) $$(BUILD_DIR)
	$$(MSG) "  [CC]   $$@"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) -I testsuite/include -DDEBUG -c $$< -o $$@

$$(TESTSUITE_OBJ_DIR)/%.$(1).o: testsuite/yalibc/%.c | $$(TESTSUITE_OBJ_DIR) $$(BUILD_DIR)
	$$(MSG) "  [CC]   $$@"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) -I testsuite/include -DDEBUG -c $$< -o $$@

$$(TESTSUITE_OBJ_DIR)/%.$(1).o: testsuite/platform/%.c | $$(TESTSUITE_OBJ_DIR) $$(BUILD_DIR)
	$$(MSG) "  [CC]   $$@"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) -I testsuite/include -DDEBUG -c $$< -o $$@
endef

# Define a function to create build rules for each target
define TARGET_RULES
# Build platform C objects for $(1)
$$(PLATFORM_OBJ_DIR)/%.$(1).o: platform/src/%.c | $$(PLATFORM_OBJ_DIR) $$(BUILD_DIR)
	$$(MSG) "  [CC]   $$@"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) -c $$< -o $$@

# Build platform assembly objects for $(1)
$$(PLATFORM_OBJ_DIR)/%.$(1).S.o: platform/src/%.S | $$(PLATFORM_OBJ_DIR) $$(BUILD_DIR)
	$$(MSG) "  [AS]   $$@"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) -c $$< -o $$@

# Create library for $(1)
$$(BUILD_DIR)/libplatform_$(1).a: $$(ALL_OBJS_$(1))
	$$(MSG) "  [AR]   $$@"
	$$(Q)$$(AR) rcs $$@ $$^

# Build testsuite binary for $(1)
$$(BUILD_DIR)/bm_testsuite.$(1): $$(TESTSUITE_OBJS_$(1)) $$(BUILD_DIR)/libplatform_$(1).a $$(LDSCRIPT_DIR)/bmmap.$(1).ld
	$$(MSG) "  [LD]   $$@.elf"
	$$(Q)$$(CC) $$(SDK_CFLAGS) -I $$(SDK_TARGETS_DIR)/$(1) $$(TESTSUITE_OBJS_$(1)) $$(call PLATFORM_LIB,$(1)) -o $$@.elf $$(LOPTS) -T $$(LDSCRIPT_DIR)/bmmap.$(1).ld -T $$(TESTSUITE_LDSCRIPT)
	$$(MSG) "  [BIN]  $$@.bin"
	$$(Q)$$(OBJCOPY) $$(CPOPS) $$@.elf $$@.bin
endef

# Generate testsuite object rules for all targets
$(foreach target,$(ALL_TARGETS),$(eval $(call TESTSUITE_OBJ_RULES,$(target))))

# Generate rules for all targets
$(foreach target,$(ALL_TARGETS),$(eval $(call TARGET_RULES,$(target))))

clean:
	@echo "Cleaning SDK build artifacts..."
	rm -rf $(OBJ_DIR)
	rm -rf $(LDSCRIPT_DIR)
	rm -f $(BUILD_DIR)/libplatform_*.a
	rm -rf $(BUILD_DIR)/bm_testsuite.*

.DEFAULT_GOAL := all
