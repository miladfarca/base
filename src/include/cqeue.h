// Copyright (c) 2025 miladfarca

// #define MAX_CQ_SIZE	2048
#define DEFAULT_CQ_SIZE 512

struct cqueue {
  // char	buf[MAX_CQ_SIZE];
  char *buf;
  char *bp;
  char *ip;
  char *op;
  char *limit;
  int size;
  int count;
  int toss;
};

struct cqueue *cq_init(struct cqueue *, char *, int);
void cq_add(struct cqueue *, int);
int cq_remove(struct cqueue *);
int cq_count(struct cqueue *);
int cq_space(struct cqueue *qp);
