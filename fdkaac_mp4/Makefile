CXX = m68k-atari-mint-g++

SRC_DIR := ./
OBJ_DIR := ./build
BIN_DIR := ../plugins

SRC := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)

BIN := $(BIN_DIR)/playmp4_fdkaac.PRG

OBJ := $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

CXXFLAGS   := -I./ -m68020-60 -fomit-frame-pointer -fno-strict-aliasing -O2

# CXXFLAGS += -Wl,--stack,10485760

LDFLAGS  :=

LDLIBS   := -lfdk-aac -lmp4v2

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
	
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $(@D)

clean:
	@$(RM) -rv $(BIN) $(OBJ_DIR)

-include $(OBJ:.o=.d)
