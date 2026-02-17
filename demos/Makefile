# Top-level demos — delegates to phase subdirectories
PHASES = phase0 phase2

all: $(PHASES)

$(PHASES):
	$(MAKE) -C $@

clean:
	@for p in $(PHASES); do $(MAKE) -C $$p clean; done

.PHONY: all clean $(PHASES)
