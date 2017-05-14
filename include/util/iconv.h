#ifndef _UTIL_ICONV_H
#define _UTIL_ICONV_H

/**
 * Convert a string to utf-8
 *
 * NOTE: current implementation tries to continue reading the text,
 * if an invalid byte sequence occurs (by skipping it). An alternative
 * would be to bail out and return NULL instead (if it causes issues).
 *
 */
unsigned char *iconv_convert(const char *str, const char *from);
unsigned char *iconv_convert3(const char *str, const char *from, size_t inlen);
unsigned char *iconv_convert4(const char *str, const char *from, size_t inlen, size_t *outlen);

#endif
