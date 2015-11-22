CC := clang
WARNINGS := -Weverything -Wno-missing-prototypes -Wno-unused-function \
	-Wno-reserved-id-macro -Wno-padded -Wno-cast-align -Wno-float-equal
CFLAGS := -std=c11 -O1 -DUNITTEST $(WARNINGS)
INCLUDE := -I$(HOME)/dev/unittestds/include -I$(HOME)/dev/libs/klib
SRC := circbuf.c idxpyr.c miscUnittests.c

utilc_t: utilc_t.c
	@$(CC) $(CFLAGS) $(INCLUDE) $(SRC) $< -o $@

utilc_t.c: $(SRC)
	@gendsu $(SRC) -of$@

clean:
	-@$(RM) $(wildcard *.o *.obj *_t *_t.exe *_t.c)

.PHONY: clean

