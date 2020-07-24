PROG_CXX = follow
SRCS     = follow.cc

CXXFLAGS += -g -O3 -DNDEBUG=1
CXXFLAGS += -std=c++17 -Wall -Wextra

all: $(PROG_CXX)

clean:
	-rm $(PROG_CXX) $(SRCS:.c=.o)

$(PROG_CXX): $(SRCS:.c=.o)
	$(CC) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) $^ $(LDFLAGS)
