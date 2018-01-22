/*
 * imap/worker/connect.c - Handles IMAP worker connect actions
 */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/base64.h"
#include "imap/imap.h"
#include "log.h"
#include "urlparse.h"
#include "worker.h"

#ifdef USE_OPENSSL
#include "absocket.h"
#endif

void handle_imap_ready(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args);

void imap_starttls_callback(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args);

void handle_worker_connect(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	worker_post_message(pipe, WORKER_ACK, message, NULL);

	struct uri *uri = malloc(sizeof(struct uri));
	if (!parse_uri(uri, (char *)message->data)) {
		worker_log(L_DEBUG, "Invalid connection string '%s'",
			(char*)message->data);
	}
	// Contains password, clear it out of RAM
	memset(message->data, 0, strlen((char *)message->data));
	strcpy((char *)message->data, "password");

	bool ssl = false;
	if (strcmp(uri->scheme, "imap") == 0) {
		ssl = false;
	} else if (strcmp(uri->scheme, "imaps") == 0) {
		ssl = true;
	} else {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, message,
				"Unsupported protocol");
		return;
	}

	if (!uri->port) {
		uri->port = strdup(ssl ? "993" : "143");
	}

	worker_log(L_DEBUG, "Connecting to IMAP server:");
	worker_log(L_DEBUG, "Protocol: %s%s", uri->scheme, ssl ? " (ssl)" : "");
	worker_log(L_DEBUG, "Username: %s", uri->username);
	worker_log(L_DEBUG, "Password: [hidden, %zd characters]", strlen(uri->password));
	worker_log(L_DEBUG, "Hostname: %s", uri->hostname);
	worker_log(L_DEBUG, "Port: %s", uri->port);

	bool res = imap_connect(imap, uri, ssl, handle_imap_ready, pipe);
	if (res) {
		worker_log(L_DEBUG, "Connected to IMAP server");
		if (ssl) {
			/*
			 * If we're using SSL, we need to wait to start doing IMAP
			 * housekeeping until the main thread approves of the certificate.
			 */
#ifdef USE_OPENSSL
			struct cert_check_message *ccm = calloc(1,
					sizeof(struct cert_check_message));
			ccm->cert = imap->socket->cert;
			worker_post_message(pipe, WORKER_CONNECT_CERT_CHECK, message, ccm);
#endif
		} else {
			imap->mode = RECV_LINE;
		}
	} else {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, message,
				"Error connecting to IMAP server");
	}
	imap->uri = uri;
}

void handle_worker_cert_okay(struct worker_pipe *pipe, struct worker_message *message) {
	struct imap_connection *imap = pipe->data;
	imap->mode = RECV_LINE;
}

void handle_imap_logged_in(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args) {
	struct worker_pipe *pipe = data;
	if (status == STATUS_OK) {
		worker_post_message(pipe, WORKER_CONNECT_DONE, NULL, NULL);
	} else {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, NULL, args ? strdup(args) : NULL);
	}
}

void handle_imap_cap(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args) {
	struct worker_pipe *pipe = data;
	if (status != STATUS_OK) {
		// TODO: Format errors sent to main thread
		worker_log(L_ERROR, "IMAP error: %s", args);
		worker_post_message(pipe, WORKER_CONNECT_ERROR, NULL, NULL);
		return;
	}
	if (!imap->cap->imap4rev1) {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, NULL,
				"IMAP server does not support IMAP4rev1");
		return;
	}
	if (imap->logged_in) return;
	// Attempt to authenticate
	if (status == STATUS_PREAUTH) {
		imap->logged_in = true;
		worker_post_message(pipe, WORKER_CONNECT_DONE, NULL, NULL);
	} else if (imap->cap->auth_plain) {
		if (imap->uri->username && imap->uri->password) {
			if (imap->cap->sasl_ir) {
				int len = snprintf(NULL, 0, "%c%s%c%s",
						'\0', imap->uri->username,
						'\0', imap->uri->password);
				char *buf = malloc(len + 1);
				snprintf(buf, len + 1, "%c%s%c%s",
						'\0', imap->uri->username,
						'\0', imap->uri->password);
				imap->logged_in = true;
				size_t _;
				char *enc = b64_encode(buf, len, &_);
				imap_send(imap, handle_imap_logged_in, pipe,
						"AUTHENTICATE PLAIN %s", enc);
				free(enc);
				free(buf);
				memset(imap->uri->password, 0, strlen(imap->uri->password));
			}
		}
	} else if (imap->cap->auth_login) {
		if (imap->uri->username && imap->uri->password) {
			imap->logged_in = true;
			imap_send(imap, handle_imap_logged_in, pipe, "LOGIN \"%s\" \"%s\"",
					imap->uri->username, imap->uri->password);
			memset(imap->uri->password, 0, strlen(imap->uri->password));
		}
#ifdef USE_OPENSSL
	} else if (imap->cap->starttls) {
		imap_send(imap, imap_starttls_callback, pipe, "STARTTLS");
#endif
	} else {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, NULL,
				"IMAP server and client do not share any supported "
				"authentication mechanisms. Did you provide a username/password?");
	}
}

void handle_imap_ready(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args) {
	struct worker_pipe *pipe = data;
	if (!imap->cap) {
		// Often the server will send us these in a status message during the
		// handshake. Sometimes it won't, though:
		imap_capability(imap, handle_imap_cap, pipe);
		return;
	}
	handle_imap_cap(imap, pipe, STATUS_OK, NULL);
}

#ifdef USE_OPENSSL

void imap_starttls_callback(struct imap_connection *imap, void *data,
		enum imap_status status, const char *args) {
	struct worker_pipe *pipe = data;
	if (!ab_enable_ssl(imap->socket)) {
		worker_post_message(pipe, WORKER_CONNECT_ERROR, NULL, "TLS connection failed.");
		return;
	}
	imap->socket->use_ssl = true;
	imap_send(imap, handle_imap_cap, pipe, "CAPABILITY");
}

#endif
