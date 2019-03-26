#ifndef MEM_H
#define MEM_H
void mem_init();
void mem_finalize();
void *mem_alloc(int x);
void *mem_free(void *area);
#endif /* MEM_H */
