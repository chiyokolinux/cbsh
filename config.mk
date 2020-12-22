VERSION = 0.1
NAME	= cbsh

PREFIX =
MANPREFIX = $(PREFIX)/share/man

CC = gcc
LD = $(CC)
CPPFLAGS =
CFLAGS   = -Wextra -Wall -Os
LDFLAGS  = -s
LDLIBS   = -lreadline
