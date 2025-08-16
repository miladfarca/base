/*
 * Forked from https://github.com/trebisky/stm32f103/blob/master/usb/kyulib.c
 * Changes by: miladfarca, 2025
 *
 * Copyright (C) 2016  Tom Trebisky  <tom@mmto.org>
 */

#include "cqeue.h"

/* This is a different API than in Kyu.
 * The caller should static allocate the structure.
 * This initializes it, and returns a pointer,
 * which is sort of silly, but sort of compatible
 * with the original API
 */
// struct cqueue * cq_init ( struct cqueue *qp )
struct cqueue *cq_init(struct cqueue *qp, char *buf, int size) {
  qp->buf = buf;
  qp->size = size;
  qp->count = 0;
  qp->ip = qp->op = qp->bp = qp->buf;
  qp->limit = &qp->bp[qp->size];
  qp->toss = 0;

  return qp;
}

/* At first, was just going to let folks examine
 * the count element of the structure, but
 * perhaps someday I will eliminate it and be
 * thankful for this accessor function.
 *
 * (Right now, I am keeping the count element,
 * it actually makes checks faster at interrupt
 * level unless I get a bit more clever and
 * sacrifice one element of storage.)
 * The key assertion is that *ip always points
 * to a valid place to dump a character.
 */
int cq_count(struct cqueue *qp) { return qp->count; }

/* return amount of available space in queue
 */
int cq_space(struct cqueue *qp) { return qp->size - qp->count; }

/* Almost certainly gets called from interrupt level.
 *	(must not block.)
 * Should surely have locks wrapped around it, in the
 * usual producer/consumer situation it is intended for.
 * (This will usually be called in an interrupt routine,
 *  where locking will be implicit.)
 */
void cq_add(struct cqueue *qp, int ch) {
  if (qp->count < qp->size) {
    qp->count++;
    *(qp->ip)++ = ch;
    if (qp->ip >= qp->limit)
      qp->ip = qp->bp;
  } else {
    qp->toss++;
  }
}

int cq_toss(struct cqueue *qp) { return qp->toss; }

/* Once upon a time, I had locking in here,
 * but now the onus is on the caller to do
 * any locking external to this facility.
 */
int cq_remove(struct cqueue *qp) {
  int ch;

  if (qp->count < 1)
    return -1;
  else {
    ch = *(qp->op)++;
    if (qp->op >= qp->limit)
      qp->op = qp->bp;
    qp->count--;
  }
  return ch;
}
