include config.mk

PROGBIN = $(NAME)

PROGOBJ = ccsh.o

OBJECTS = $(PROGOBJ)
HEADERS = config.h

all: $(PROGBIN)

$(PROGBIN): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(PROGOBJ): $(HEADERS)

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROGBIN) $(DESTDIR)$(PREFIX)/bin

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGBIN)

dist: clean
	mkdir -p $(NAME)-$(VERSION)
	cp -f Makefile README config.mk *.c *.h $(NAME)-$(VERSION)
	tar -cf $(NAME)-$(VERSION).tar $(NAME)-$(VERSION)
	gzip $(NAME)-$(VERSION).tar
	rm -rf $(NAME)-$(VERSION)

clean:
	rm -f $(PROGBIN) *.o $(NAME)-$(VERSION).tar.gz

.SUFFIXES: .def.h

.def.h.h:
	cp $< $@

.PHONY:
	all install uninstall dist clean
