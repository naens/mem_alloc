/****h* mem_alloc/mem
 *  NAME
 *    mem - Generalized Fibonacci Memory Allocator
 *  DESCRIPTION
 *    A simple Fibonacci memory allocator.  Uses blocks of size 8.
 *    The sequence is 1, 2, 3, 4, 5, 7, 10, 14, 19, 26..., where
 *    a(n) = a(n-1) + a(n-4).
 *    Its functions are
 *    * void mem_init() to initialize the allocator
 *    * void *mem_alloc(unsigned int n) to initialize n bytes
 *    * void mem_free(void *area) to free a previously allocated area
 *    * void mem_finalize() to finalize the allocator
 ******
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

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


#define MEM_ALLOC_DEBUG 1


static inline void
debug(char *fmt, ...)
{
    if (MEM_ALLOC_DEBUG)
    {
        va_list myargs;
        va_start(myargs, fmt);
        vfprintf(stderr, fmt, myargs);
        va_end(myargs);
    }
}


/****s*
 *  NAME mem/item
 *    item - memory space that represents an allocation unit
 *  DESCRIPTION
 *    An item is an object that contains a header specifying its size
 *    and an area that can be returned to the user when as the result
 *    of the mem_alloc function.  Its job is to represent a usable
 *    block of memory.  When its not used, the area contains the pointers
 *    to the previous and next items making a doubly linked list to
 *    which points the cell.
 *    The header contains the size field which is equal to the size field
 *    in the cell.  The header also contains 3 bits which indicate:
 *    * is_in_use: whether the area is being used
 *    * lr_bit: whether it's a left or a right buddy
 *    * inh_bit: bit needed when merging buddies in order to know if it's
 *          a right buddy or a left buddy.  The left child inherits
 *          parent's lr_bit and the right child inherits the inh_bit,
 *          so when merging the lr_bit and inh_bit can be restored.
 ******
 * Layout:
 *   - header: 64 bits: size and 3 bits lowest (not used by size)
 *   - area: prev (64 bits) and next (64 bits)
 *   => total minimal size is 64 bits * 3 = 24 bytes = 3 blocks
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
    debug("ITEM %04x %-16s    size=%-6" PRIxPTR "\t", PTR_NUM(item), msg, size);
    debug("    in_use: %-6s lr_bit: %-6s inh_bit: %-6s",
        in_use ? "true," : "false,",
        lr_bit == LEFT ? "LEFT," : "RIGHT,",
        inh_bit == LEFT ? "LEFT" : "RIGHT");
    if (size != 0)
    {
        uintptr_t prev = (uintptr_t)item_get_prev(item);
        uintptr_t next = (uintptr_t)item_get_next(item);
        debug("    prev: %04x", PTR_NUM(prev));
        debug("    next: %04x", PTR_NUM(next));
    }
    else
    {
        debug("    FAKE_RIGHT");
    }
    debug("\n");
}



/****s* mem/cell
 *  NAME
 *    struct cell - a cell of the array
 *  DESCRIPTION
 *    The array that contains free lists orders them by size.
 *    Each size is a number of the genearalized Fibonacci sequence.
 *    A cell contains a pointer to a linked list of items of the
 *    same size.
 ******
 */

struct cell {
    uintptr_t size;
    void *items;
};


/****s*
 *  NAME mem/array
 *    struct array - array of free lists
 *  DESCRIPTION
 *    The array contains free items that are available for use.
 *    They are ordered according to the geneealized Fibonacci seqeuence in
 *    increasing order.  It's a dynamic array, meaning that when it to
 *    contain items bigger than its maximum, its capacity is increased.
 *    The capacity field tells how many items are there before needing to
 *    create a new array and copy everything there.  The size, on the
 *    other hand tells how many cells are used.  When a cell is in use,
 *    the cell is initialized and its size field is set.  From that point
 *    it can contain free items of a specific size.
 ******
 */
 
struct array {
    struct cell *data;
    unsigned int size;
    unsigned int capacity;
};


/****f* mem/array_set_size, array_free
 *  NAME
 *    array_set_size - set the size of the array
 *  SYNOPSIS
 *    void array_set_size(struct array *array, unsigned int new_size);
 *    struct array {
 *        struct cell *data;
 *        unsigned int size;
 *        unsigned int capacity;
 *    };
 *  DESCRIPTION
 *    The function array_set_size modifies the size of the array.  Usually it
 *    only increases the size of the array->size variable, but when the 
 *    requested size more than the current capacity, a new array is allocated,
 *    data is copied into it, and the old array is freed.  And, of course, a
 *    new capacity is assigned.
 *    When a new size is set, it is made sure that array->size is initialized.
 *    The function array_free frees the array and its data;
 *******
 */

void
array_set_size(struct array *array, unsigned int new_size)
{
    if (new_size < array->size)
    {
        fprintf(stderr, "bad array size: %d\n", new_size);
        exit(1);
    }
    if (new_size <= array->capacity)
    {
        array->size = new_size;
    }
    else
    {
        unsigned int new_capacity = max(2 * array->size, new_size);
        struct cell *new_data = malloc(new_capacity * sizeof(struct cell));
        memcpy(new_data, array->data, array->size * sizeof(struct cell));
        free(array->data);
        array->data = new_data;
        array->size = new_size;
        array->capacity = new_capacity;
    }
}


void
print_array(struct array *array)
{
    unsigned int i, j;
    void *items;
    debug("array: ");
    for (i = 0; i < array->size; i++)
    {
        if (i > 0)
        {
            debug(" ");
        }
        items = array->data[i].items;
        if (items != NULL) {
            debug("[%d](%d):", i, (unsigned int)array->data[i].size);
            j = 0;
            while (items != NULL) {
                if (j > 0) {
                    debug(",");
                }
                debug("%04x", PTR_NUM(items));
                items = item_get_next(items);
                j++;
            }
        }
    }
    debug("\n");
}


static struct array array;

void *mem_list;

void
mem_init()
{
    debug("memory initialization\n");
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
    debug("memory finalized\n");
}


/****f* mem/take_item
 *  NAME
 *    take_item - delete the first item from a free list and return it
 *  SYNOPSIS
 *    void *take_item(struct array *array, unsigned int i);
 *  DESCRIPTION
 *    Deletes the first item from the free list at index i in the array.
 *    It should be checked before calling this function that there is
 *    at least one item in the array, that is, array->data[i].items is
 *    not NULL.
 *  RETURN VALUE
 *    Returns the first item from the specified free list.
 ******
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
    return item;
}


/****f* mem/insert_item
 *  NAME
 *    insert_item - insert the item into the array
 *  SYNOPSIS
 *    void insert_item(struct array *array, unsigned int i, void *item);
 *  DESCRIPTION
 *    Inserts the item into the free list at i as the first element.
 *  RETURN VALUE
 *    This function returns nothing.
 ******
 */

void
insert_item(struct array *array, unsigned int i, void *item)
{
    item_set_next(item, array->data[i].items);
    if (array->data[i].items != NULL)
    {
        item_set_prev(array->data[i].items, item);
    }
    array->data[i].items = item;
    item_set_prev(item, NULL);
}


/****f* mem/split_item
 *  NAME
 *    split_item - split an item until of the requested size is created
 *  SYNOPSIS
 *      void* split_item(struct array *array, unsigned int i, void *item,
 *          uintptr_t n)
 *  DESCRIPTION
 *      This function is given the number of blocks requested, an item,
 *      and the index of the free list corresponding to its size in the
 *      array.  The purpose is to reduce the size of the item by
 *      splitting it into two buddies and inserting one of them in the
 *      free list, until we get an item as small as possible that can
 *      hold n blocks.
 *
 *      So, at each step we check if the item needs to be split, split
 *      it, determine if we want to use the left buddy or the right
 *      buddy, and continue the loop, which this time checks the buddy
 *      we have chosen and so on.  The buddy that is not used is
 *      inserted back into the free list.
 ******
 */

void*
split_item(struct array *array, unsigned int i, void *item, uintptr_t n)
{
    void *curr, *left, *right;
    uintptr_t szl, szr;
    boolean inh_l, inh_r;
    unsigned int i_left, i_right;
    curr = item;
    while (array->data[i-1].size >= n && i > 4)
    {
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
        i_left = i - 4;
        i_right = i - 1;
        if (szl >= n)
        {
            insert_item(array, i_right, right);
            i = i_left;
            curr = left;
        }
        else
        {
            insert_item(array, i_left, left);
            i = i_right;
            curr = right;
        }
    }
    return curr;
}


/****f* mem/alloc_new_item
 *  NAME
 *    alloc_new_item - allocate a new item from the OS
 *  SYNOPSIS
 *    void *alloc_new_item(unsigned int n);
 *  DESCRIPTION
 *    The function alloc_new_item allocates a new item of n blocks.  It
 *    also allocates a fake empty buddy, so that it does not merge more
 *    than it should.  It's a fake right buddy that is marked in use and,
 *    thus, stops the merging of buddies.
 *    The function is always passed the numbers of the generalized
 *    Fibonacci sequence in the n parameter.
 *  RETURN VALUE
 *    This function returns the address of new item allocated.
 ******
 */

void*
alloc_new_item(unsigned int n)
{
    void *tmp, *fake_right, *item;
    tmp = malloc(BLOCK_SIZE * n + sizeof(void*)*2);
    *((void**)tmp) = mem_list;
    mem_list = tmp;
    fake_right = ((char*)tmp) + BLOCK_SIZE * n + sizeof(void*);
    item_set_size(fake_right, 0);
    item_set_lr_bit(fake_right, RIGHT);
    item_set_in_use(fake_right, 1);
    item = ((char*)tmp) + sizeof(void*);
    item_set_size(item, n);
    item_set_lr_bit(item, LEFT);
    return item;
}


/****f* mem/mem_alloc
 *  NAME
 *    mem_alloc - allocate an area block of a minumum number of bytes 
 *  SYNOPSIS
 *    void *mem_alloc(unsigned int x)
 *  DESCRIPTION
 *    Allocates minimum x bytes.  First it finds the right location, which is
 *    either the first free item with the number of blocks required, or a
 *    newly allocated block.
 *
 *    Then it splits the item if it can be split, that is the number of blocks
 *    needed is less or equal to a generalized Fibonacci number before the
 *    block found in the previous step.
 *
 *    Then it set the in_use bit of the item and return the area of the item.
 *  RETURN VALUE
 *    An area of minimum x bytes.
 ******
 */

void*
mem_alloc(unsigned int x)
{
    unsigned int i;
    void *item, *area;
    uintptr_t n = BLOCKS(x + HEADER_SIZE);

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
    }
    else
    {
        item = take_item(&array, i);
    }

    item = split_item(&array, i, item, n);
    item_set_in_use(item, 1);
    area = item_get_area(item);
    debug("allocated %d bytes at %p\n", x, area);
    return area;
}


/****f* mem/item_get_buddy
 *  NAME
 *    item_get_buddy - given an item, return its buddy
 *  SYNOPSIS
 *  void *item_get_buddy(struct array *array, void *item, unsigned int i,
 *      unsigned int *ibuddy);
 *  DESCRIPTION
 *    Calculates the address of the buddy and returns it.  It uses the fact
 *    that the free list containing the size of the buddy is either 3 cells
 *    to the left or 3 cells to the right, depending on whether the item is
 *    is a left or the right buddy.  Then, knowing the size, it's easy to
 *    know the location of the buddy.
 *  RETURN VALUE
 *    The address of the buddy.  It also sets the address pointed by
 *    ibuddy, which storing the index in the array containing lists of 
 *    the size of the buddy.
 ******
 */

void*
item_get_buddy(struct array *array, void *item, unsigned int i,
    unsigned int *ibuddy)
{
    uintptr_t size, buddy_size;
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


/****f* mem/delete_item
 *  NAME
 *    delete_item - delete an item from the free list at a specified index
 *  SYNOPSIS
 *    void delete_item(struct array *array, unsigned int i, void *item);
 *  DESCRIPTION
 *    The function delete_item deletes one item, which it finds by address,
 *    from the free list specified by the index i in the array.
 *    It deletes in two distinct steps: first it finds the item, then it
 *    deletes it.  The delete operation is like a normal delete operation
 *    from a doubly linked list, except that if it's the first item, then
 *    the entry in the array is modified to point to the new head
 *  RETURN VALUE
 *    Does not return anything
 ******
 */

void
delete_item(struct array *array, unsigned int i, void *item)
{
    void *curr = array->data[i].items;
    while (curr != NULL && curr != item)
    {
        curr = item_get_next(curr);
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


/****f* mem/coalesce
 *  NAME
 *    coalesce - merge buddies until an buddy in use is found.
 *  DESCRIPTION
 *    The coalesce function makes the opposite of splitting: it merges buddies
 *    that are not in use, and stop when it finds a buddy which is in use,
 *    which will happen sooner or later because the item at the top had a fake
 *    buddy which is in use.
 *  SYNOPSIS 
 *    void coalesce(struct array *array, unsigned int);
 *  RETURN VALUE
 *    This function returns nothing.
 ******
 */

void
coalesce(struct array *array, unsigned int i)
{
    unsigned int ibuddy;
    void *item, *buddy, *left, *right;
    boolean lr_bit, inh_bit;
    uintptr_t size;
    item = array->data[i].items;
    buddy = item_get_buddy(array, item, i, &ibuddy);
    while (!item_is_in_use(buddy)    // !! don't coalesce if buddy not complete
        && array->data[ibuddy].size == item_get_size(buddy))
    {
        delete_item(array, i, item);
        delete_item(array, ibuddy, buddy);
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
        item_set_in_use(item, 0);
        buddy = item_get_buddy(array, item, i, &ibuddy);
        insert_item(array, i, item);
    }
}


/****f* mem/mem_free
 *  NAME
 *    mem_free - put the item back into the free list
 *  SYNOPSIS
 *    void mem_free(void *area)
 *  DESCRIPTION
 *    Return the item after use to the free list.  The first thing is to
 *    get the item from the address from the area.  As the header has always
 *    the same size, it's easy.  Using the header, it's easy to find the size,
 *    and, having found the size we can find the index because the array has a
 *    size for each cell.
 *  RETURN VALUE
 *    Does not return anything.
 ******
 */

void
mem_free(void *area)
{
    unsigned int i;
    void *item;
    uintptr_t size;

    debug("freeing %p\n", area);

    item = item_from_area(area);
    size = item_get_size(item);
    i = 0;
    while (size != array.data[i].size)
    {
        i++;
    }
    item_set_in_use(item, 0);
    insert_item(&array, i, item);
    coalesce(&array, i);
}
