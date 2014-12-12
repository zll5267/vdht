#ifndef __VMISC_H__
#define __VMISC_H__

#include <netinet/in.h>
/*
 * for host address.
 */
int vhostaddr_get_first(char*, int);
int vhostaddr_get_next (char*, int);

/*
 * for sockaddr
 */

void vsockaddr_copy      (struct sockaddr_in*, struct sockaddr_in*);
int  vsockaddr_equal     (struct sockaddr_in*, struct sockaddr_in*);
int  vsockaddr_convert   (const char*, uint16_t, struct sockaddr_in*);
int  vsockaddr_convert2  (uint32_t, uint16_t, struct sockaddr_in*);
int  vsockaddr_unconvert (struct sockaddr_in*, char*, int, uint16_t*);
int  vsockaddr_unconvert2(struct sockaddr_in*, uint32_t*, uint16_t*);
int  vsockaddr_strlize   (struct sockaddr_in*, char*, int );
int  vsockaddr_unstrlize (const char*, struct sockaddr_in*);
int  vsockaddr_combine   (struct sockaddr_in*, struct sockaddr_in*, struct sockaddr_in*);
int  vsockaddr_dump      (struct sockaddr_in*);

#endif

