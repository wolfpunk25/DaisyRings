TARGET = daisy_rings
CPP_SOURCES = main.cpp \
	dsp/fm_voice.cpp \
	dsp/part.cpp \
	dsp/resonator.cpp \
	dsp/string.cpp \
	dsp/string_synth_part.cpp \
	src/resources.cpp
APP_TYPE = BOOT_QSPI
LIBDAISY_DIR ?= ../deps/libDaisy
STMLIB_DIR = third_party/stmlib
CPP_STANDARD = -std=gnu++17
OPT = -O2

CC_SOURCES = $(STMLIB_DIR)/dsp/units.cc $(STMLIB_DIR)/utils/random.cc

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
C_INCLUDES += -I. -I./src -Ithird_party -I../display/RingsPanel -Wno-unused-local-typedefs

CC_OBJ = $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))

$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(CC_OBJ) $(LIBDAISY_DIR)/build/libdaisy.a
	$(CXX) $(CC_OBJ) $(OBJECTS) $(LDFLAGS) -o $@
