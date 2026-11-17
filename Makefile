TARGET := usb-pd-ufp
BUILD := build
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
CFLAGS := -O2 -g -Wall -Wextra -Werror -std=gnu11 -ffunction-sections -fdata-sections -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -Iinclude -I$(DEVKITPRO)/libnx/include -D__SWITCH__
LDFLAGS := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -specs=$(DEVKITPRO)/libnx/switch.specs -g -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections -L$(DEVKITPRO)/libnx/lib
OBJECTS := $(addprefix $(BUILD)/,main.o policy.o config.o transport.o profile.o)
.PHONY: all test package
all: test package
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: source/%.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -lnx -o $@
$(BUILD)/main.nso: $(BUILD)/$(TARGET).elf
	elf2nso $< $@
$(BUILD)/main.npdm: module.json | $(BUILD)
	npdmtool $< $@
test: | $(BUILD)
	gcc -std=c11 -Wall -Wextra -Werror -Iinclude source/policy.c source/profile.c tests/test_policy.c -o $(BUILD)/test-policy.exe
	./$(BUILD)/test-policy.exe
	gcc -std=c11 -Wall -Wextra -Werror -Itests/mock -Iinclude source/transport.c source/policy.c tests/test_transport.c -o $(BUILD)/test-transport.exe
	./$(BUILD)/test-transport.exe
package: $(BUILD)/main.nso $(BUILD)/main.npdm
	python tools/package.py
-include $(OBJECTS:.o=.d)
