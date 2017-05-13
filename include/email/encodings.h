#ifndef _EMAIL_ENCODINGS_H
#define _EMAIL_ENCODINGS_H

int iso_8859_1_to_utf8(unsigned char **data, int len);
int quoted_printable_decode(char *data, int len);

#endif
