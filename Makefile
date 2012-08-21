ALL     := server client
CLEAN   := $(addsuffix -clean,$(ALL))
INSTALL := $(addsuffix -install,$(ALL))

.PHONY: all $(ALL) $(CLEAN) $(INSTALL)

all:     $(ALL)
clean:   $(CLEAN)
install: $(INSTALL)

$(ALL):
	@$(MAKE) --no-print-directory -C $@

$(CLEAN): %-clean :
	@$(MAKE) --no-print-directory -C $* clean

$(INSTALL): %-install :
	@$(MAKE) --no-print-directory -C $* install
