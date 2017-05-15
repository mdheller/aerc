#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "email/encodings.h"
#include "email/headers.h"
#include "log.h"
#include "util/list.h"
#include "util/base64.h"
#include "util/iconv.h"

static void strapp(char **stringp, char *str, size_t n) {
	if (!str || !*str || !stringp)
		return;
	size_t m = *stringp ? strlen(*stringp) : 0;
	*stringp = realloc(*stringp, m + n + 1);
	memcpy(*stringp + m, str, n);
	(*stringp)[m + n] = 0;
}

static char *decode_rfc1342(char *input) {
	char *res = NULL, *p, *cur;
	for (cur = input; *cur;) {
		p = strstr(cur, "=?");
		if (!p) {
			strapp(&res, cur, strlen(cur));
			break;
		} else if (p == cur) {
			char *start = cur;
			char *charset = start + 2;
			char *encoding = strchr(charset, '?');
			if (!encoding) {
				strapp(&res, cur, charset - cur);
				cur = charset;
				continue;
			}
			encoding++;
			if (encoding[1] != '?') {
				strapp(&res, cur, encoding - cur);
				cur = encoding;
				continue;
			}
			char *data = encoding + 2;
			char *end = strstr(data, "?=");
			if (!end) {
				strapp(&res, cur, strlen(cur));
				break;
			}
			char *buf;
			size_t len;
			if (tolower(encoding[0]) == 'b') {
				buf = (char *)b64_decode(data, end - data, &len);
			} else if (tolower(encoding[0]) == 'q') {
				buf = calloc(1, end - data + 1);
				memcpy(buf, data, end - data);
				len = quoted_printable_decode(buf, end - data);
				buf[len] = 0;
			} else {
				strapp(&res, cur, end + 2 - cur);
				cur = end + 2;
				continue;
			}
			*(encoding - 1) = 0;
			if (!strcasecmp(charset, "utf-8") || !strcasecmp(charset, "us-ascii")) {
				// everything's fine
			} else {
				char *new;
				if (!(new = (char *)iconv_convert(buf, charset))) {
					*(encoding - 1) = '?';
					// leave the header as is, if an unknown encoding is encountered
					free(buf);
					strapp(&res, cur, end + 2 - cur);
					cur = end + 2;
					continue;
				}
				buf = new;
				len = strlen(buf);
			}
			*(encoding - 1) = '?';
			strapp(&res, buf, len);
			cur = end + 2;
		} else {
			strapp(&res, cur, p - cur);
			cur = p;
		}
	}
	return res;
}

int parse_headers(const char *headers, list_t *output) {
	while (*headers && strstr(headers, "\r\n") == headers) {
		headers += 2;
	}
	while (*headers) {
		char *crlf = strstr(headers, "\r\n");
		char *null = strchr(headers, '\0');
		char *eol = crlf == NULL ? null : crlf;
		int eol_i = eol - headers;

		if (isspace(*headers)) {
			while (isspace(*++headers));
			if (output->length != 0) {
				/* Concat with previous line */
				struct email_header *h = output->items[output->length - 1];
				char *prev = h->value;
				char *new = malloc(strlen(prev) + eol_i + 2);
				strcpy(new, prev);
				strcat(new, " ");
				strncat(new, headers, eol_i);
				new[eol_i + strlen(prev)] = '\0';
				h->value = new;
				free(prev);
				headers = eol;
				if (*headers) headers += 2;
				continue;
			}
		}

		char *colon = strchr(headers, ':');
		if (!colon) {
			headers = eol;
			if (*headers) headers += 2;
		}
		int colon_i = colon - headers;
		char *key = malloc(colon_i + 1);
		strncpy(key, headers, colon_i);
		key[colon_i] = '\0';
		if (strstr(colon + 1, "\r\n") != colon + 1) {
			colon_i += 2;
		}
		char *value = malloc(eol_i - colon_i + 1);
		strncpy(value, headers + colon_i, eol_i - colon_i);
		value[eol_i - colon_i] = '\0';
		headers = eol;
		if (*headers) headers += 2;
		struct email_header *header = calloc(1, sizeof(struct email_header));
		header->key = key;
		header->value = value;
		list_add(output, header);
	}
	// Extra decoding here if necessary
	for (size_t i = 0; i < output->length; ++i) {
		struct email_header *header = output->items[i];
		char *new, *old = header->value;
		if ((new = decode_rfc1342(header->value))) {
			header->value = new;
			free(old);
		}
		worker_log(L_DEBUG, "Parsed header: %s: %s", header->key, header->value);
	}
	return 0;
}

void free_headers(list_t *headers) {
	if (!headers) return;
	for (size_t i = 0; i < headers->length; ++i) {
		struct email_header *header = headers->items[i];
		if (header) {
			free(header->key);
			free(header->value);
			free(header);
		}
	}
	list_free(headers);
}
