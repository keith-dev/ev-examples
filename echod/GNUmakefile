PROG = echod
SRCS = echod.cc

PORT     ?= 2222

CXXFLAGS += -g -std=c++17 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter
ifneq ($(findstring $(shell uname),FreeBSD),)
    CXXFLAGS += -I/usr/local/include
    LDFLAGS += -L/usr/local/lib
endif
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

$(PROG): $(SRCS:.cc=.o)
	$(CXX) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) $^ $(LDFLAGS)
