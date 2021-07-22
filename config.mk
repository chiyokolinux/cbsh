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

VERSION = 0.4
NAME	= cbsh

PREFIX =
MANPREFIX = /usr/share/man

CC = gcc
LD = $(CC)
CPPFLAGS =
CFLAGS   = -Wextra -Wall -Os -g
LDFLAGS  = -s
LDLIBS   = 
