#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <stdint.h>

#include "mem.h"


// constants for random test
#define ARRAY_SIZE 800
#define NUMBER_OF_ALLOCATIONS 1000
#define MAXIMUM_ALLOC_SIZE 50000



void
test_1()
{
    void *a;
    void *b;
    a = mem_alloc(100);
    b = mem_alloc(40);
    mem_free(a);
    mem_free(b);
}


void
test_2()
{
    void *a, *b, *c;
    a = mem_alloc(1000);
    b = mem_alloc(1000);
    c = mem_alloc(1000);
    mem_free(a);
    mem_free(b);
    mem_free(c);
}


void
test_array()
{
    void *a, *b;
    a = mem_alloc(2500);        // size=16, capacity=16 => increase to 32
    b = mem_alloc(10000);       // no increase needed here
    mem_free(a);
    mem_free(b);
}


void
test_splitting()
{
    void *m250, *m14a, *m36, *m14b, *m5, *m50b, *m50a;
    m250 = mem_alloc(1500);
    mem_free(m250);
    m14a = mem_alloc(100);
    m36 = mem_alloc(250);
    m14b = mem_alloc(80);
    m5 = mem_alloc(25);
    mem_free(m5);
    mem_free(m14a);
    m50b = mem_alloc(300);
    mem_free(m14b);
    mem_free(m36);
    m50a = mem_alloc(350);
    mem_free(m50a);
    mem_free(m50b);
}


void
test_coalescing()
{
    void *m250, *m69, *m14, *m26, *m10, *m131, *m50;
    m250 = mem_alloc(1800);
    mem_free(m250);
    m69 = mem_alloc(525);
    m14 = mem_alloc(75);
    m26 = mem_alloc(200);
    m10 = mem_alloc(70);
    mem_free(m26);
    mem_free(m14);
    m131 = mem_alloc(950);
    mem_free(m10);
    m50 = mem_alloc(281);
    mem_free(m131);
    mem_free(m69);
    mem_free(m50);
}


void
test_unsplittable()
{
    void *m4a, *m50, *m4b, *m10;
    m4a = mem_alloc(1);		// minimum: 4 blocks
    m50 = mem_alloc(300);
    mem_free(m50);
    m4b = mem_alloc(10);	// 3 blocks, minimum: 4 blocks
    m10 = mem_alloc(40);	// 7 blocks, but have only 10, cannot split
    mem_free(m4a);
    mem_free(m4b);
    mem_free(m10);
}


void
fill_mem(unsigned char *buffer, unsigned int size)
{
    unsigned int i;
    unsigned char *p;
    uint64_t sum;
    p = buffer;
    sum = 0;
    for (i = 2; i < size; i++)
    {
        p[i] = (unsigned char)(rand() % 0x100);
        sum += p[i];
    }
    buffer[0] = (unsigned char)(sum % 0x100);
    buffer[1] = (unsigned char)((sum / 0x100) % 0x100);
}


void
check_sum(unsigned char *buffer, unsigned int size)
{
    unsigned int i;
    unsigned int check, sum;
    check = (unsigned int)(buffer[0] + 0x100 * buffer[1]);
    sum = 0;
    for (i = 2; i < size; i++)
    {
        sum += buffer[i];
    }
    if (sum % 0x10000 != check) {
        printf("check sum error sum=%x check=%x\n", sum, check);
        exit(1);
    }
}


void
print_area(unsigned char *buffer, unsigned int size)
{
    int i;
    for (i = 0; i < size; i++)
    {
        if (i > 0)
        {
            printf(" ");
        }
        printf("%02x", buffer[i]);
    }
    printf("\n");
}


void
test_random()
{
    unsigned int count, i, j, sz, t;
    void *array[ARRAY_SIZE];
    unsigned int sizes[ARRAY_SIZE];
    FILE *f = fopen("out", "w");

//    struct timespec ts; 
//    clock_gettime(CLOCK_REALTIME, &ts); 
//    t = (unsigned int)(ts.tv_nsec + ts.tv_sec * 1000000000);
  
//    struct timeval tv;
//    gettimeofday(&tv, NULL);
//    t = (unsigned int)(1000000 * tv.tv_sec + tv.tv_usec);

//    t = time(0);
    
    struct timeb tb;
    ftime(&tb);
    t = (unsigned int)(1000000 * tb.time + tb.millitm);

    srand(t);

    // initialize the array
    for (i = 0; i < ARRAY_SIZE; i++)
    {
        array[i] = NULL;
    }

    count = 0;
    j = 0;
    fprintf(f, "\nvoid\ntest_random_gen()\n{\n");
    while (count < NUMBER_OF_ALLOCATIONS)
    {
        i = (unsigned int)(rand() % ARRAY_SIZE);
        if (array[i] == NULL)
        {
            sz = (unsigned int)((rand() % MAXIMUM_ALLOC_SIZE) + 1);
            fprintf(f, "    array[%d] = mem_alloc(%d);\n", i, sz);
            array[i] = mem_alloc(sz);
            sizes[i] = sz;
            fill_mem((unsigned char*)array[i], sizes[i]);
            count++;
        }
        else
        {
            fprintf(f, "    mem_free(array[%d]);\n", i);
            check_sum(array[i], sizes[i]);
            mem_free(array[i]);
            array[i] = NULL;
        }
        if (array[j] != NULL)
        {
            fprintf(f, "[%03d]: fill_mem sz=%d\n", j, sizes[j]);
            fill_mem((unsigned char*)array[j], sizes[j]);
        }
        j = (j + 1) % ARRAY_SIZE;
    }

    // free remaining
    for (i = 0; i < ARRAY_SIZE; i++)
    {
        if (array[i] != NULL)
        {
            fprintf(f, "    mem_free(array[%d]);\n", i);
            check_sum(array[i], sizes[i]);
            mem_free(array[i]);
        }
    }
    fprintf(f, "}\n");
    fclose(f);
}


void
test_random_gen1()
{
    void *array[4];

    array[2] = mem_alloc(721);
    mem_free(array[2]);
    array[1] = mem_alloc(501);
    array[0] = mem_alloc(12);
    array[2] = mem_alloc(307);
    mem_free(array[0]);
    mem_free(array[1]);
    array[1] = mem_alloc(438);
    mem_free(array[2]);
}


void
test_random_gen2()
{
    void *array[25];
    array[9] = mem_alloc(97);
    array[21] = mem_alloc(76);
    array[22] = mem_alloc(98);
    mem_free(array[22]);
    array[6] = mem_alloc(77);
    array[18] = mem_alloc(91);
    mem_free(array[9]);
}

void
test_random_gen3()
{
    void *array[25];
    array[18] = mem_alloc(2493);
    mem_free(array[18]);
}

int
main(int argc, char **argv)
{
    mem_init();

//    test_1();
//    test_2();
//    test_array();
//    test_splitting();
//    test_coalescing();
//    test_unsplittable();
    test_random();
//    test_random_gen1();
//    test_random_gen2();
//    test_random_gen3();

    mem_finalize();
    return 0;
}
