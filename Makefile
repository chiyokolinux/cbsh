# this file is part of cbsh
# Copyright (c) 2021 Emily <elishikawa@jagudev.net>
#
# cbsh is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# cbsh is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with cbsh.  If not, see <https://www.gnu.org/licenses/>.

include config.mk

PROGBIN = $(NAME)

PROGOBJ = cbsh.o
LINEOBJ = linenoise.o
UTF8OBJ = utf8.o

OBJECTS = $(LINEOBJ) $(UTF8OBJ) $(PROGOBJ)
HEADERS = config.h linenoise/linenoise.h linenoise/encodings/utf8.h

all: $(PROGBIN)

$(PROGBIN): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(PROGOBJ): $(HEADERS)

$(LINEOBJ): linenoise/linenoise.c linenoise/linenoise.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(UTF8OBJ): linenoise/encodings/utf8.c linenoise/encodings/utf8.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROGBIN) $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/1.0/$(VERSION)/g" < $(NAME).1 > $(DESTDIR)$(MANPREFIX)/man1/$(NAME).1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGBIN)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(NAME).1

dist: clean
	mkdir -p $(NAME)-$(VERSION)
	cp -f Makefile README config.mk *.c *.h cbsh.1 $(NAME)-$(VERSION)
	cp -f linenoise/linenoise.c linenoise/linenoise.h linenoise/LICENSE $(NAME)-$(VERSION)
	cp -f linenoise/encodings/utf8.c linenoise/encodings/utf8.h $(NAME)-$(VERSION)
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
