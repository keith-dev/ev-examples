SUBDIR = echod

.PHONY: $(SUBDIR)

all: $(SUBDIR)

clean:
	for P in $(SUBDIR); do $(MAKE) --directory=$$P clean; done

$(SUBDIR):
	$(MAKE) -C $@
