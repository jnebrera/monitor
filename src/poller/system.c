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

#include "system.h"

#include <librd/rdlog.h>

#include <ctype.h>
#include <stdlib.h>

static char *trim_end(char *buf) {
	char *end = buf + strlen(buf) - 1;
	while (end >= buf && isspace(*end)) {
		end--;
	}
	*(end + 1) = '\0';
	return buf;
}

bool system_solve_response(char *buff,
			   size_t buff_size,
			   double *number,
			   void *unused,
			   const char *command) {
	(void)unused;

	bool ret = false;
	FILE *fp = popen(command, "r");
	if (NULL == fp) {
		rdlog(LOG_ERR, "Cannot get system command.");
		return false;
	}

	if (NULL == fgets(buff, buff_size, fp)) {
		rdlog(LOG_ERR, "Cannot get buffer information");
		goto err;
	}

	rdlog(LOG_DEBUG, "System response: %s", buff);
	trim_end(buff);
	char *endPtr;
	*number = strtod(buff, &endPtr);
	if (buff != endPtr) {
		ret = true;
	}

err:
	fclose(fp);
	return ret;
}
