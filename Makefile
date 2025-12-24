CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

YAMLCPP_CFLAGS ?= $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
YAMLCPP_LIBS ?= $(shell pkg-config --libs yaml-cpp 2>/dev/null)

ifeq ($(strip $(YAMLCPP_CFLAGS)),)
	YAMLCPP_CFLAGS = -I$(shell brew --prefix yaml-cpp 2>/dev/null)/include
endif
ifeq ($(strip $(YAMLCPP_LIBS)),)
	YAMLCPP_LIBS = -L$(shell brew --prefix yaml-cpp 2>/dev/null)/lib -lyaml-cpp
endif

TARGET := game

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $(YAMLCPP_CFLAGS) -o $(TARGET) main.cpp $(YAMLCPP_LIBS)

run: $(TARGET) 
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
