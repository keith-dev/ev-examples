# ---------------------------------------------------------------------------
#	Copyright (c) 2013-19 Keith Williams.
#	All rights reserved.
#
#	Redistribution and use in source and binary forms are permitted
#	provided that the above copyright notice and this paragraph are
#	duplicated in all such forms and that any documentation,
#	advertising materials, and other materials related to such
#	distribution and use acknowledge that the software was developed
#	by Webbusy Ltd.
# ---------------------------------------------------------------------------

PROG_CXX = follow
SRCS     = follow.cc

CXXFLAGS += -g
CXXFLAGS += -std=c++17 -Wall -Wextra

all: $(PROG_CXX)

clean:
	-rm $(PROG_CXX) $(SRCS:.c=.o)

$(PROG_CXX): $(SRCS:.c=.o)
	$(CC) $(CFLAGS) $(TARGET_ARCH) $(OUTPUT_OPTION) $^ $(LDFLAGS)
