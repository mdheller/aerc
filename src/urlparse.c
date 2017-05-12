/*
 * urlparse.c - parser for RFC 3986 URI strings
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "urlparse.h"

bool percent_decode(char *s) {
	/* RFC 1738 section 2.2 */
	if (!s) return true;
	while (*s) {
		if (*s == '+') {
			*s = ' ';
		} else if (*s == '%') {
			int c;
			if (!isxdigit(*(s + 1)) || !isxdigit(*(s + 2)) || !sscanf(s + 1, "%2x", &c)) {
				return false;
			} else {
				*s = c;
				memmove(s + 1, s + 3, strlen(s) - 1);
			}
		}
		++s;
	}
	return true;
}
 
bool parse_uri(struct uri *res, const char *src) {
	/*
	 * Basically this:
	 *
	 * protocol:[//][user[:password]@]hostname[:port][/path][?query][#fragment]
	 *
	 * This function should probably be a state machine, but isn't. Too bad for
	 * you reading it!
	 */
	memset(res, 0, sizeof(struct uri));
	// Parse scheme
	const char *cur = src, *start = src;
	while (*cur && *cur != ':') {
		++cur;
	}
	if (!*cur) return false;
	res->scheme = malloc(cur - start + 1);
	strncpy(res->scheme, start, cur - start);
	res->scheme[cur - start] = '\0';
	++cur;
	// Fun fact: the // is optional in URIs
	if (*cur == '/') cur++;
	if (*cur == '/') cur++;
	// user/pass/domain/port
	if (!*cur) return false;
	char *stop = "/?#\0";
	start = cur;
	while (*cur && !strchr(stop, *cur)) {
		++cur;
	}
	// Note: hostname temporarily contains all the other fields too
	res->hostname = malloc(cur - start + 1);
	strncpy(res->hostname, start, cur - start);
	res->hostname[cur - start] = '\0';
	if (strchr(res->hostname, '@')) {
		// username
		char *at = strchr(res->hostname, '@');
		*at = '\0';
		res->username = res->hostname;
		res->hostname = strdup(at + 1);
		if (strchr(res->username, ':')) {
			// password
			at = strchr(res->username, ':');
			res->password = strdup(at + 1);
			memset(at, 0, strlen(at));
		}
	}
	if (strchr(res->hostname, ':')) {
		// port
		char *at = strchr(res->hostname, ':');
		*at = '\0';
		res->port = strdup(at + 1);
	}
	if (!percent_decode(res->hostname)) return false;
	if (!percent_decode(res->username)) return false;
	if (!percent_decode(res->password)) return false;
	if (!percent_decode(res->port)) return false;
	if (!*cur) return true;

	return false;
}

void uri_free(struct uri *uri) {
	free(uri->scheme);
	free(uri->username);
	free(uri->password);
	free(uri->hostname);
	free(uri->port);
	free(uri->path);
	free(uri->query);
	free(uri->fragment);
}
