/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef SLIRP_MAIN_H
#define SLIRP_MAIN_H

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#define TOWRITEMAX 512

/*
 * Get the difference in 2 times from updtim()
 * Allow for wraparound times, "just in case"
 * x is the greater of the 2 (current time) and y is
 * what it's being compared against.
 */
#define TIME_DIFF(x,y) (x)-(y) < 0 ? ~0-(y)+(x) : (x)-(y)

extern u_int curtime;
extern struct in_addr loopback_addr;
extern unsigned long loopback_mask;

#define PROTO_SLIP 0x1
#ifdef USE_PPP
#define PROTO_PPP 0x2
#endif

int if_encap(Slirp *slirp, struct mbuf *ifm);
ssize_t slirp_send(struct socket *so, const void *buf, size_t len, int flags);

#endif
