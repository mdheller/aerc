#include <assert.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "log.h"
#include "util/iconv.h"

unsigned char *iconv_convert(const char *str, const char *from) {
	size_t outlen = 0;
	return iconv_convert4(str, from, strlen(str), &outlen);
}

unsigned char *iconv_convert3(const char *str, const char *from, size_t inlen) {
	size_t outlen = 0;
	return iconv_convert4(str, from, inlen, &outlen);
}

static inline size_t min(size_t l, size_t r) { return (l < r) ? l : r; }

unsigned char *iconv_convert4(const char *str, const char *from, size_t inlen, size_t *outlen) {
	assert(from);
	unsigned char *out, *_out, *rv;
	const unsigned char *in;
	size_t o, _o, len, res, written = 0;
	int delta;
	iconv_t ic = iconv_open("UTF-8", from);
	if (ic == (iconv_t) -1) {
		worker_log(L_ERROR, "Failed to convert '%s' to utf-8: unknown encoding, please report", from);
		return NULL;
	}
	len = o = 2 * inlen;
	out = rv = calloc(1, len + 1);
	in = (const unsigned char *)str;
	while (inlen > 0) {
		_out = out;
		_o = o;

		errno = 0;
		res = iconv(ic, (char **)&in, &inlen, (char **)&_out, &_o);
		written += delta = o - _o;
		// success, or incomplete sequence at the end (discard it)
		if (res != (size_t) -1 || errno == EINVAL) {
			out = _out, o = _o;
			break;
		}
		// invalid byte sequence: skip a byte and try to continue
		if (errno == EILSEQ) {
			out = _out, o = _o;
			in++, inlen--;
			continue;
		}

		// grow by inlen * 2
		len += inlen * 2;
		rv = realloc(rv, len + 1);
		out = rv + written;
		o = len - written;
	}

	*out = 0;
	*outlen = written;

	iconv_close(ic);
	return rv;
}
