# Top-level demos — delegates to phase subdirectories
PHASES = phase0 phase2 phase3

all: $(PHASES)

$(PHASES):
	$(MAKE) -C $@

# Phase 4 has no separate build — uses wow.com directly
phase4:
	@echo "Phase 4 demos use wow.com — run: cd demos/phase4 && ./run_demos.sh"

clean:
	@for p in $(PHASES); do $(MAKE) -C $$p clean; done

.PHONY: all clean $(PHASES) phase4
