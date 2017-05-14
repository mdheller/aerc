#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "email/encodings.h"
#include "util/unicode.h"

int iso_8859_1_to_utf8(unsigned char **data, int len) {
	size_t new_len = 0, new_size = len;
	char *new_str = malloc(new_size);
	for (int i = 0; i < len; ++i) {
		if (new_len + utf8_chsize((*data)[i]) >= new_size) {
			new_size += 16;
			char *_ = realloc(new_str, new_size);
			if (!_) {
				return len;
			}
			new_str = _;
		}
		new_len += utf8_encode(&new_str[new_len], (*data)[i]);
	}
	free(*data);
	*data = (unsigned char *)new_str;
	return new_len;
}

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
				data--, i--;
			} else if (data[1] == '\n') {
				len -= 2;
				memmove(data, data + 2, len - i);
				data--, i--;
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
