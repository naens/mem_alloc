#ifndef MEM_H
#define MEM_H

void mem_init(void);
void mem_finalize(void);
void *mem_alloc(unsigned int x);
void mem_free(void *area);

#endif /* MEM_H */
