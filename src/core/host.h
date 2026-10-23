/* host.h - see host.c */
#ifndef HOST_H
#define HOST_H
/* pin the calling thread to a physical core of its own: which 0 and 1 get two different ones; 1 when done */
int host_pin (int which);
#endif
