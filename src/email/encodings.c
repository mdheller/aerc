#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "email/encodings.h"

int quoted_printable_decode(char *data, int len) {
	if (!data) {
		return -1;
	}
	for (int i = 0; i < len; ++i) {
		if (!*data) {
			break;
		}
		if (*data == '=') {
			int c;
			if (data[1] == '\r') {
				len -= 3;
				memmove(data, data + 3, len - i);
			} else if (data[1] == '\n') {
				len -= 2;
				memmove(data, data + 2, len - i);
			} else if (isxdigit(data[1]) && isxdigit(data[2]) && sscanf(&data[1], "%2x", &c)) {
				*data = c;
				len -= 2;
				memmove(data + 1, data + 3, len - i);
			}
		}
		++data;
	}
	return len;
}
