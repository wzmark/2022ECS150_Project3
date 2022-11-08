# Target library
lib := libfs.a

all: $(lib)

objs:= disk.o fs.o



CC	= gcc
CFLAGS	:= -Wall -Wextra -MMD -Werror
CFLAGS += -g
PANDOC := pandoc


ifneq ($(V),1)
Q = @
endif

all: $(lib)
#Dep tracking
deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)

$(lib): $(objs)
	@echo "CC $@"
	$(Q)ar rcs -o $@ $^


%.o: %.c
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<


clean:
	@echo "clean"
	$(Q)rm -f  $(objs) $(lib) $(deps)
