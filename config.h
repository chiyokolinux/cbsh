/**
 * cbsh - a simple UNIX shell
 * Copyright (c) 2021 Emily <elishikawa@jagudev.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#ifndef CONFIG_H
#define CONFIG_H

/* #define MAXLINELEN      1024 */
#define MAXCURDIRLEN    4096

#define DEFAULTPROMPT   "\033[0;95m%1$s\033[0;32m@\033[0;36m%2$s\033[0;32m:\033[0;91m%3$s\033[0;32m$\033[0m "
#define HISTSIZE        1024

#define DEBUG_OUTPUT

#endif /* CONFIG_H */
