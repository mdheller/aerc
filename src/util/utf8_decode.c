#include <stdint.h>
#include <stddef.h>
#include "util/unicode.h"

uint8_t masks[] = {
	0x7F,
	0x1F,
	0x0F,
	0x07,
	0x03,
	0x01
};

uint32_t utf8_decode(const char **s) {
	uint32_t cp = 0;
	if (**s >= 0) {
		// shortcut
		cp = **s;
		++*s;
		return cp;
	}
	int size = utf8_size(*s);
	if (size == -1) {
		++*s;
		return UTF8_INVALID;
	}
	uint8_t mask = masks[size - 1];
	cp = (uint8_t)**s & mask;
	++*s;
	while (--size) {
		cp <<= 6;
		cp |= **s & 0x3f;
		++*s;
	}
	return cp;
}
