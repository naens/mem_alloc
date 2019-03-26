#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

/* 64-bit OS */
#if defined(__x86_64__)
#define MIN_SIZE 3
#define SIZE_1 4
#define SIZE_2 5
#define SIZE_3 7

/* 32-bit OS */
#elif defined(__386__) || defined(__i386__) || defined(__DJGPP__)
#define MIN_SIZE 2
#define SIZE_1 3
#define SIZE_2 4
#define SIZE_3 5

/* 16-bit OS */
#elif defined(__I86__) || defined(__86__)
#define MIN_SIZE 1
#define SIZE_1 2
#define SIZE_2 3
#define SIZE_3 4

#endif

#define BLOCK_SIZE 8
#define POINTER_SIZE sizeof(uintptr_t)
#define HEADER_SIZE POINTER_SIZE
#define MINIMUM_ALLOC (POINTER_SIZE << 6)


#define LEFT 0
#define RIGHT 1
#define BLOCKS(n) ((n+BLOCK_SIZE-1)/BLOCK_SIZE)

#define PTR_NUM(ptr) ((unsigned int)(((uintptr_t)ptr) % 0x1000))

#define boolean int

#ifndef max
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif /* max */


struct cell {
    uintptr_t size;
    void *items;
};


struct array {
    struct cell *data;
    unsigned int size;
    unsigned int capacity;
};


/* Item accessors and mutators
 * Fields:
 *   - size: number of blocks
 *   - is_in_use: if is used
 *   - lr_bit: is left or right
 *   - inh_bit: the inheritance bit
 *   - prev: the pointer to the previous item
 *   - next: the pointer to the next item
 * Layout:
 *   - header: 64 bits: size and 3 bits lowest (not used by size)
 *   - area: prev (64 bits) and next (64 bits)
 *   => total minimal size is 64 bits * 3 = 24 bytes = 3 blocks
 *       
 */
uintptr_t
item_get_size(void *item)
{
    uintptr_t size_field = ((uintptr_t*)item)[0];
    return size_field >> 3;
}

void
item_set_size(void *item, uintptr_t size)
{
    uintptr_t size_field = ((uintptr_t*)item)[0];
    uint8_t flags = size_field & 7;
    uintptr_t new_size_field = flags | (size << 3);
    ((uintptr_t*)item)[0] = new_size_field;
}


// in_use
boolean
item_is_in_use(void *item)
{
    return (((uintptr_t*)item)[0] & 4) != 0;
}

void
item_set_in_use(void *item, boolean in_use)
{
    uintptr_t size_field = ((uintptr_t*)item)[0] & (~(uintptr_t)4);
    uint8_t tmp = in_use ? 4 : 0;
    uintptr_t new_size_field = size_field | tmp;
    ((uintptr_t*)item)[0] = new_size_field;
}


// lr_bit
boolean
item_get_lr_bit(void *item)
{
    return (((uintptr_t*)item)[0] & 2) != 0;
}

void
item_set_lr_bit(void *item, boolean in_use)
{
    uintptr_t size_field = ((uintptr_t*)item)[0] & (~(uintptr_t)2);
    uint8_t tmp = in_use ? 2 : 0;
    uintptr_t new_size_field = size_field | tmp;
    ((uintptr_t*)item)[0] = new_size_field;
}


// inh_bit
boolean
item_get_inh_bit(void *item)
{
    return (((uintptr_t*)item)[0] & 1) != 0;
}

void
item_set_inh_bit(void *item, boolean in_use)
{
    uintptr_t size_field = ((uintptr_t*)item)[0] & (~(uintptr_t)1);
    uint8_t tmp = in_use ? 1 : 0;
    uintptr_t new_size_field = size_field | tmp;
    ((uintptr_t*)item)[0] = new_size_field;
}


// area
void*
item_get_area(void *item)
{
    return &((void**)item)[1];
}

void*
item_from_area(void *area)
{
    return &((void**)area)[-1];
}


// prev
void*
item_get_prev(void *item)
{
    return ((void**)item)[1];
}

void
item_set_prev(void *item, void *prev)
{
    ((void**)item)[1] = prev;
}


// next
void*
item_get_next(void *item)
{
    return ((void**)item)[2];
}

void
item_set_next(void *item, void *next)
{
    ((void**)item)[2] = next;
}

void
print_item(void *item, char *msg)
{
    uintptr_t size = item_get_size(item);
    boolean in_use = item_is_in_use(item);
    boolean lr_bit = item_get_lr_bit(item);
    boolean inh_bit = item_get_inh_bit(item);
    printf("ITEM %04x %-16s    size=%-6" PRIxPTR "\t", PTR_NUM(item), msg, size);
    printf("    in_use: %-6s lr_bit: %-6s inh_bit: %-6s",
        in_use ? "true," : "false,",
        lr_bit == LEFT ? "LEFT," : "RIGHT,",
        inh_bit == LEFT ? "LEFT" : "RIGHT");
    if (size != 0)
    {
        uintptr_t prev = (uintptr_t)item_get_prev(item);
        uintptr_t next = (uintptr_t)item_get_next(item);
        printf("    prev: %04x", PTR_NUM(prev));
        printf("    next: %04x", PTR_NUM(next));
    }
    else
    {
        printf("    FAKE_RIGHT");
    }
    printf("\n");
}


/* Dynamic array of <size,item>
 *  When the user wants to set an item, but the array is too small, the
 *  array can be increased with array_set_size(new_size).  The array is a
 *  structure, which contains the data, the size, and the capacity.  The
 *  capacity is the maximum size the array can reach without the need for a
 *  new allocation and copying everything to the new array.
 */


void
array_set_size(struct array *array, unsigned int new_size)
{
    if (new_size < array->size)
    {
        // error, should not be called
        fprintf(stderr, "bad array size: %d\n", new_size);
        exit(1);
    }
    if (new_size <= array->capacity)
    {
//        printf("new size: %d capacity=%d\n", new_size, array->capacity);
        array->size = new_size;
    }
    else
    {	// here we need to allocate new_size and free the old array
        unsigned int new_capacity = max(2 * array->size, new_size);
        struct cell *new_data = malloc(new_capacity * sizeof(struct cell));
        memcpy(new_data, array->data, array->size * sizeof(struct cell));
        free(array->data);
        array->data = new_data;
        array->size = new_size;
        array->capacity = new_capacity;
//        printf("resize array: new size: %d, new capacity: %d\n",
//            array->size, array->capacity);
    }
}


void
array_free(struct array *array)
{
    free(array->data);
    free(array);
}


void
fill_array(struct array *array, unsigned int from, unsigned int to)
{
    unsigned int i;
    for (i = from; i < to; i++)
    {
        array->data[i].size = i;
    }
}


void
print_array(struct array *array)
{
    unsigned int i, j;
    void *items;
    printf("array: ");
    for (i = 0; i < array->size; i++)
    {
        if (i > 0)
        {
            printf(" ");
        }
        items = array->data[i].items;
        if (items == NULL) {
//            printf("[%d](%d):null", i, array->data[i].size);
        } else {
            printf("[%d](%d):", i, (unsigned int)array->data[i].size);
            j = 0;
            while (items != NULL) {
                if (j > 0) {
                    printf(",");
                }
                // DEBUG
//                if (j > 10) {
//                    exit(2);
//                }
                printf("%04x", PTR_NUM(items));
                items = item_get_next(items);
                j++;
            }
        }
    }
    printf("\n");
}


static struct array array;

void *mem_list;

void
mem_init()
{
    array.data = malloc(4 * sizeof(struct cell));
    array.size = 4;
    array.data[0].size = MIN_SIZE;
    array.data[0].items = NULL;
    array.data[1].size = SIZE_1;
    array.data[1].items = NULL;
    array.data[2].size = SIZE_2;
    array.data[2].items = NULL;
    array.data[3].size = SIZE_3;
    array.data[3].items = NULL;
    mem_list = NULL;
//    print_array(&array);
}

void
mem_finalize()
{
    free(array.data);

    // free all allocated blocks
    while (mem_list != NULL)
    {
        void *tmp = mem_list;
        mem_list = *((void**)mem_list);
        free(tmp);
    }
}

/* takes the first item from the array at index in order to return
 * for memory allocation: pointers are not valid
 */
void*
take_item(struct array *array, unsigned int i)
{
    void *next, *item;
    next = item_get_next(array->data[i].items);
    if (next != NULL)
    {
        item_set_prev(next, NULL);
    }
    item = array->data[i].items;
    array->data[i].items = next;
//    printf("TAKE_ITEM at %d of size %" PRIuPTR ": items = %04x, item = %04x\n",
//        i, array->data[i].size, PTR_NUM(array->data[i].items), PTR_NUM(item));
    return item;
}

void
insert_item(struct array *array, unsigned int i, void *item)
{
//    printf("INSERT_ITEM %04x at %d: items = %04x\n",
//        PTR_NUM(item), i, PTR_NUM(array->data[i].items));
    item_set_next(item, array->data[i].items);
    if (array->data[i].items != NULL)
    {
        item_set_prev(array->data[i].items, item);
    }
    array->data[i].items = item;
    item_set_prev(item, NULL);
    item_set_in_use(item, 0);
//    printf("INSERT_ITEM %04x at %d: items = %04x\n",
//        PTR_NUM(item), i, PTR_NUM(array->data[i].items));
}

void*
split_item(struct array *array, unsigned int i, void *item, uintptr_t n)
{
//    boolean first;
    void *curr, *left, *right;
    uintptr_t szl, szr;
    boolean inh_l, inh_r;
    unsigned int i_left, i_right;
    curr = item;
//    first = 1;
//    printf("----------------- SPLIT BEGIN: i=%d, item=%04x, n=%" PRIuPTR "\n",
//        i, PTR_NUM(item), n);
    while (array->data[i-1].size >= n && i > 4)
    {
//        if (first) {
//            first = 0;
//        } else {
//            printf("-----------------\n");
//        }
//        printf("SPLIT_ITEM: i=%d\n", i);
//        print_item(item, "split");
        szl = array->data[i-4].size;
        szr = array->data[i-1].size;
        inh_l = item_get_lr_bit(curr);
        inh_r = item_get_inh_bit(curr);
        left = curr;
        right = ((char*)curr) + szl * BLOCK_SIZE;
        item_set_size(left, szl);
        item_set_size(right, szr);
        item_set_lr_bit(left, LEFT);
        item_set_lr_bit(right, RIGHT);
        item_set_in_use(left, 0);
        item_set_in_use(right, 0);
        item_set_inh_bit(left, inh_l);
        item_set_inh_bit(right, inh_r);
//        printf("SPLIT_ITEM: szl=%" PRIuPTR ", szr=%" PRIuPTR "\n", szl, szr);
//        print_item(left, "left");
//        print_item(right, "right");
        i_left = i - 4;
        i_right = i - 1;
        if (szl >= n)
        {
            insert_item(array, i_right, right);
//            printf("SPLIT_ITEM(right): insert at %d, size=%" PRIuPTR "\n",
//                i_right, array->data[i_right].size);
//            print_item(right, "right");
            i = i_left;
            curr = left;
        }
        else
        {
            insert_item(array, i_left, left);
//            printf("SPLIT_ITEM(left): insert at %d, size=%" PRIuPTR "\n",
//                i_left, array->data[i_left].size);
//            print_item(right, "left");
            i = i_right;
            curr = right;
        }
    }
//    printf("----------------- SPLIT END\n");
    return curr;
}

void*
alloc_new_item(unsigned int n)
{
    void *tmp, *fake_right, *item;
    tmp = calloc(BLOCK_SIZE * n + sizeof(void*)*2, 1);
    *((void**)tmp) = mem_list;
    mem_list = tmp;
    fake_right = ((char*)tmp) + BLOCK_SIZE * n + sizeof(void*);
    item_set_size(fake_right, 0);
    item_set_lr_bit(fake_right, RIGHT);
    item_set_in_use(fake_right, 1);
//    print_item(fake_right, "fake_right");
    item = ((char*)tmp) + sizeof(void*);
//    printf("alloc new item of %d blocks\n", n);
    item_set_size(item, n);
    return item;
}

void*
mem_alloc(unsigned int x)
{
    unsigned int i;
    void *item;
    uintptr_t n = BLOCKS(x + HEADER_SIZE);
//    printf("\nMEM_ALLOC: x = %d, -> %" PRIuPTR " blocks needed\n", x, n);

    // stop loop: 
    // * size >= n && items != NULL
    // * size >= n && end-of-array && size >= MINIMUM
    // size < n && (i < array.size || size >= MINIMUM)
    // end-of-array test: i == array.size - 1
    i = 0;
    while (array.data[i].size < n
        || (array.data[i].items == NULL
                && (i < array.size - 1
                        || array.data[i].size < BLOCKS(MINIMUM_ALLOC))))
    {
        if (i == array.size - 1)
        {
            array_set_size(&array, array.size + 1);
            array.data[i+1].size = array.data[i].size + array.data[i-3].size;
            array.data[i+1].items = NULL;
        }
        i++;
    }

    if (array.data[i].items == NULL)
    {
        item = alloc_new_item((unsigned int)array.data[i].size);
//        printf("allocated at size %" PRIuPTR " item with size %" PRIuPTR "\n",
//            array.data[i].size, item_get_size(item));
    }
    else
    {
        item = take_item(&array, i);
    }
    item = split_item(&array, i, item, n);
    item_set_in_use(item, 1);
//    print_item(item, "mem_alloc item");
//    print_array(&array);
    return item_get_area(item);
}

void*
item_get_buddy(struct array *array, void *item, unsigned int i, unsigned int *ibuddy)
{
    uintptr_t size, buddy_size;
//    print_item(item, "get_buddy");
    if (item_get_lr_bit(item) == LEFT)
    {
        *ibuddy = i + 3;
        size = item_get_size(item);
        return ((char*)item) + size * BLOCK_SIZE;
    }
    else
    {
        *ibuddy = i - 3;
        buddy_size = array->data[*ibuddy].size;
        return ((char*)item) - buddy_size * BLOCK_SIZE;
    }
}

void
delete_item(struct array *array, unsigned int i, void *item)
{
//    printf("DELETE_ITEM: %d\n", i);
//    print_item(item, "deleting...");
    void *curr = array->data[i].items;
    unsigned int cnt = 0;
    while (curr != NULL && curr != item)
    {
        curr = item_get_next(curr);
        if (curr != NULL) {
//            printf("curr=%p\n", curr);
            // DEBUG
//            print_item(curr, "curr");
//            if (cnt > 10) {
//                exit(1);
//            }
            cnt++;
        }
        
    }
    if (curr != NULL)
    {
        void *prev = item_get_prev(curr);
        void *next = item_get_next(curr);
        if (prev != NULL)
        {
            item_set_next(prev, next);
        }
        if (next != NULL)
        {
            item_set_prev(next, prev);
        }
        if (curr == array->data[i].items) {
            array->data[i].items = next;
        }
        
    }
}

void
coalesce(struct array *array, unsigned int i)
{
    unsigned int ibuddy;
    void *item, *buddy, *left, *right;
    boolean lr_bit, inh_bit;
    uintptr_t size;
//    printf("================= COALESCE BEGIN: i=%d\n", i);
    item = array->data[i].items;
//    print_item(item, "item");
    buddy = item_get_buddy(array, item, i, &ibuddy);
//    print_item(buddy, "buddy");
    while (!item_is_in_use(buddy)    // !! don't coalesce if buddy not complete
        && array->data[ibuddy].size == item_get_size(buddy))
    {
//        printf("================= COALESCE: i: %d, ibuddy: %d\n", i, ibuddy);
        //take_item(array, i);
        delete_item(array, i, item);
//        printf("item deleted\n");
        delete_item(array, ibuddy, buddy);
//        printf("buddy deleted\n");
        if (item_get_lr_bit(item) == LEFT)
        {
            left = item;
            right = buddy;
            i += 4;
        }
        else
        {
            left = buddy;
            right = item;
            i += 1;
        }
        item = left;
        size = array->data[i].size;	// new i
        lr_bit = item_get_inh_bit(left);
        inh_bit = item_get_inh_bit(right);
        item_set_lr_bit(item, lr_bit);
        item_set_inh_bit(item, inh_bit);
        item_set_size(item, size);
        buddy = item_get_buddy(array, item, i, &ibuddy);
        insert_item(array, i, item);
//        print_item(item, "item");
//        print_item(buddy, "buddy");
    }
//    printf("================= COALESCE END\n");
}

void
mem_free(void *area)
{
    unsigned int i;
    void *item;
    uintptr_t size;
    item = item_from_area(area);
//    printf("\nMEM_FREE: %04x\n", PTR_NUM(item));
    size = item_get_size(item);
    i = 0;
    while (size != array.data[i].size)
    {
//        printf("MEM_FREE: i=%d size=%" PRIuPTR "\n", i, array.data[i].size);
        i++;
    }
    insert_item(&array, i, item);
//    print_item(item, "inserted");
    coalesce(&array, i);
//    print_array(&array);
}
