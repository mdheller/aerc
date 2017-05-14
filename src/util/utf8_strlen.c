#include <stdint.h>
#include "util/unicode.h"

size_t utf8_strlen(const char *str) {
	size_t len;
	while (*str) {
		++len;
		str += utf8_size(str);
	}
	return len;
}
