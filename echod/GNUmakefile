PROG = echod
SRCS = echod.cc

PORT     ?= 1

CXXFLAGS += -g -std=c++17 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter
LDFLAGS  += -lev
ifneq ($(findstring $(shell uname -m),armv7l),)
    # support my arm32 phone
    LDFLAGS += -latomic
endif

.PHONY: all clean run valgrind

all: $(PROG)

clean:
	-rm $(PROG) $(SRCS:.c=.o)

run: $(PROG)
	./$(PROG) $(PORT)

valgrind: $(PROG)
	valgrind --leak-check=full ./$(PROG) $(PORT)

$(PROG): $(SRCS:.c=.o)
	$(CXX) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) $^ $(LDFLAGS)
