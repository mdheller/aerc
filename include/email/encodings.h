#ifndef _EMAIL_ENCODINGS_H
#define _EMAIL_ENCODINGS_H

enum qp_flavor { QP_BODY, QP_HEADERS };

int iso_8859_1_to_utf8(unsigned char **data, int len);
int quoted_printable_decode(char *data, int len, int qp_flavor);

#endif
