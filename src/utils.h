/*
  Copyright (C) 2016 Eneo Tecnologia S.L.
  Copyright (C) 2017 Eugenio Perez
  Author: Eugenio Perez <eupm90@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <string.h>

/// [Un]likely failure of a malloc. It can be used to not compile some unused
/// branch if system have memory overcommit.
#define alloc_likely(x) likely(x)
#define alloc_unlikely(x) unlikely(x)

/** Homogenize GNU-specific and XSI-compliant strerror function. Returns a
    thread local buffer, so there is no need for free returned buffer. However,
    the function is not reentrant; you need to use strerror_r for that.
    @param t_errno errno
    @return Buffer with error
    */
static __attribute__((unused)) const char *gnu_strerror_r(int t_errno) {
	static __thread char buffer[512];
#if !defined(_POSIX_C_SOURCE) || ((_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE)
	strerror_r(t_errno, buffer, sizeof(buffer));
	return buffer;
#else
	return strerror_r(t_errno, buffer, sizeof(buffer));
#endif
}
