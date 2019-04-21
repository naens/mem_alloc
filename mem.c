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

#include "mem.h"

/* 64-bit OS */
#if defined(__x86_64__)
#define MIN_SIZE 3
#define SIZE_1 4
#define SIZE_2 5
#define SIZE_3 7
#define DATA_INIT_BLOCKS 69
#define ARRAY_INIT_SIZE 11
#define ARRAY_INIT_CAPACITY 16

/* 32-bit OS */
#elif defined(__386__) || defined(__i386__) || defined(__DJGPP__)
#define MIN_SIZE 2
#define SIZE_1 3
#define SIZE_2 4
#define SIZE_3 5
#define DATA_INIT_BLOCKS 36
#define ARRAY_INIT_SIZE 10
#define ARRAY_INIT_CAPACITY 16

/* 16-bit OS */
#elif defined(__I86__) || defined(__86__)
#define MIN_SIZE 1
#define SIZE_1 2
#define SIZE_2 3
#define SIZE_3 4
#define DATA_INIT_BLOCKS 19
#define ARRAY_INIT_SIZE 9
#define ARRAY_INIT_CAPACITY 16

#else
#error Unsupported Operating System, sorry.
#endif

#define BLOCK_SIZE 8
#define POINTER_SIZE sizeof(uintptr_t)
#define HEADER_SIZE POINTER_SIZE

/* the minimum size to allocate is pointer size * 64
 * after that every allocation is bigger, at least the next
 * number in the generalized Fibonacci sequence
 */ 


#define LEFT 0
#define RIGHT 1
#define BLOCKS(n) ((n+BLOCK_SIZE-1)/BLOCK_SIZE)

#define PTR_NUM(ptr) ((unsigned int)(((uintptr_t)ptr) % 0x1000))

#define boolean int


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
 *    An item is an object that contains a header specifying its size and an
 *    area that can be returned to the user when as the result of the
 *    mem_alloc function.  Its job is to represent a usable block of memory. 
 *    When its not used, the area contains pointers to the previous and next
 *    items making a doubly linked list to which points the cell.  The
 *    header contains the size field which is equal to the size field in the
 *    cell.  The header also contains 3 bits which indicate:
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
    debug("ITEM %04x %-16s    size=%-6" PRIxPTR "\t",
        PTR_NUM(item),
        msg,
        size);
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
 *    The items in the array are called cells, which have two fields: the
 *    size and the pointer to the items.  Each cell represents a free list
 *    of the specified size.  The cells are arranged in the order of the
 *    generalized Fibonacci sequence.  The items in one free list are all of
 *    the same size.
 ******
 */

struct cell {
    uintptr_t size;
    void *items;
};


/****s*
 *  NAME mem/array
 *    struct array - array of free lists
 *  SYNOPSIS
 *    struct array {
 *        struct cell *data;
 *        unsigned int size;
 *        unsigned int capacity;
 *    };
 *  DESCRIPTION
 *    The array contains free items that are available for use.  They are
 *    ordered according to the geneealized Fibonacci seqeuence in increasing
 *    order.  It's a dynamic array, meaning that when it to contain items
 *    bigger than its maximum, its capacity is increased.  The capacity
 *    field tells how many items are there before needing to create a new
 *    array and copy everything there.  The size, on the other hand, tells
 *    how many cells are used.  When a cell is in use, the cell is
 *    initialized and its size field is set.  From that point it can contain
 *    free items of a specific size.
 ******
 */
 
struct array {
    struct cell *data;
    unsigned int size;
    unsigned int capacity;
};


/****f* mem/array_inc_size
 *  NAME
 *    array_set_size - increase the size of the array by one
 *  SYNOPSIS
 *    void array_inc_size(struct array *array, unsigned int new_size)
 *  DESCRIPTION
 *    The function array_inc_size increases the size of the array by 1. 
 *    Usually it only increases the size of the array->size variable, but
 *    when the size reaches than the current capacity, a new array is
 *    allocated, data is copied into it, and the old array is freed.  Also a
 *    new capacity is assigned.  When a new size is set, it is made sure
 *    that array->size is initialized.  The array uses the functionality of
 *    the allocator in order to allocate and free for the case when it needs
 *    to copy itself into a new location.
 *  RETURN VALUE
 *    This function does not return anything.
 *******
 */

void*
alloc_new_item(unsigned int n);
void*
take_item(struct array *array, unsigned int i);
void*
split_item(struct array *array, unsigned int i, void *item, uintptr_t n);

void
array_inc_size(struct array *array)
{
    unsigned int i, j;
    struct cell *new_data, *old_data;
    array->size++;
    i = array->size - 1;
    array->data[i].size = array->data[i-1].size + array->data[i-4].size;
    array->data[i].items = NULL;
    if (array->size == array->capacity)
    {
        array->capacity *= 2;

        old_data = array->data;
        new_data = (struct cell*)mem_alloc(array->capacity
                                        * (unsigned int)sizeof(struct cell));
        for (j = 0; j < array->size; j++)
        {
            new_data[j] = old_data[j];
        }
        array->data = new_data;
        mem_free(old_data);
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

/* holds the lined list of the allocated elements from the OS */
void *mem_list;


/****f* mem/array_init
 *  NAME
 *    array_init - initialize the array
 *  SYNOPSIS
 *    void array_init(struct array *array)
 *  DESCRIPTION
 *    This function is called during the initialization of the memory. 
 *    There is a limit of the minimum size that we allocate from the OS. 
 *    The initial array has to be at least this size.  In the case of the
 *    supported architectures this size is bigger than the minimal size that
 *    can hold an array big enough so that it can be hold the array when the
 *    array is freed.  When the array needs to be resized, another space is
 *    allocated and the array is copied there.  The old area, that contained
 *    the previous version of the array, is inserted into the array and can
 *    be reused.
 *  RETURN VALUE
 *    This function does not return any value.
 ******
 */

void
array_init(struct array *array)
{
    unsigned int i;
    uintptr_t prev;

    void *data_item = alloc_new_item(DATA_INIT_BLOCKS);
    item_set_in_use(data_item, 1);
    array->data = item_get_area(data_item);

    array->data[0].size = MIN_SIZE;
    array->data[0].items = NULL;
    array->data[1].size = SIZE_1;
    array->data[1].items = NULL;
    array->data[2].size = SIZE_2;
    array->data[2].items = NULL;
    array->data[3].size = SIZE_3;
    array->data[3].items = NULL;
    
    prev = array->data[3].size;

    for (i = 4; i < ARRAY_INIT_SIZE; i++)
    {
        array->data[i].size = prev + array->data[i-4].size;
        array->data[i].items = NULL;
        prev = array->data[i].size;
    }

    array->size = ARRAY_INIT_SIZE;
    array->capacity = ARRAY_INIT_CAPACITY;
}


/****f* mem/mem_init
 *  NAME
 *    mem_init - initialize the memory
 *  SYNOPSIS
 *    void mem_init()
 *  DESCRIPTION
 *    This function needs to be called in order to use the memory allocator. 
 *    Its main duty is to initialize mem_list and the array.  The global
 *    variable mem_list contains a linked list of all of the memory chunks
 *    that have been allocated by the Operating System, so that they can be
 *    returned, not every OS guarantees that everything will be returned if
 *    there are memory areas which are not freed.
 *  RETURN VALUE
 *    No value is returned.
 ******
 */

void
mem_init()
{
    debug("memory initialization\n");
    mem_list = NULL;
    array_init(&array);
}


/****f* mem/mem_finalize
 *  NAME
 *    mem_finalize - return all memory used by the allocator to the OS
 *  SYNOPSIS
 *    void mem_finalize()
 *  DESCRIPTION
 *    The function mem_finalize is called by the user after having finished
 *    using the memory allocator.  This function goes through every item in
 *    the mem_list and returns it to the Operating System by calling the
 *    free function.
 *  RETURN VALUE
 *    Nothing is returned by this function.
 ******
 */

void
mem_finalize()
{
    array.data = NULL;

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
 *    void *take_item(struct array *array, unsigned int i)
 *  DESCRIPTION
 *    Deletes the first item from the free list at index i in the array.  It
 *    should be checked before calling this function that there is at least
 *    one item in the array cell, that is, array->data[i].items is not NULL.
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
 *    void insert_item(struct array *array, unsigned int i, void *item)
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
 *      This function is given the number of blocks requested, an item, and
 *      the index of the free list corresponding to its size in the array. 
 *      The purpose is to reduce the size of the item by splitting it into
 *      two buddies and inserting one of them into the free list, until we
 *      get an item as small as possible that can hold n blocks.
 *
 *      So, at each step we check if the item needs to be split.  Then we
 *      split it and determine if we want to use the left buddy or the right
 *      buddy, and we continue the loop, which this time checks the buddy we
 *      have chosen and so on.  The buddy that is not used is inserted back
 *      into the free list.
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
 *    The function alloc_new_item allocates a new item of n blocks.  It also
 *    allocates a fake empty buddy, so that it does not merge more than it
 *    should.  The fake right buddy is marked in use in order to stop the
 *    merging of buddies.
 *
 *    The whole thing is prefixed by a pointer in order to make it a singly
 *    linked list, mem_list, which is used to free all the elements
 *    allocated from the OS.
 *
 *    The number passed in the n parameter is always a number belonging to
 *    the generalized Fibonacci sequence.
 *  RETURN VALUE
 *    This function returns the address of new item allocated.
 ******
 */

void*
alloc_new_item(unsigned int n)
{
    void *tmp, *fake_right, *item;
    debug("alloc_new_item: allocate %d blocks, %d bytes\n", n, (int)(BLOCK_SIZE * n + sizeof(void*)*2));
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
 *    Allocates minimum x bytes.
 *
 *    First we check if the array contains an element that we can use in
 *    order to hold x bytes.
 *
 *    If such element is found we remove it from
 *    the array.
 *
 *    Otherwise we stretch the array if needed because the free lists in the
 *    array have to follow the generalized Fibonacci sequence.  So if we
 *    need to allocate a very big item, we have to fill everything that
 *    comes in between the end of the array and the place where the big item
 *    will have to go.  Then we can allocate the area from the OS.  The rule
 *    is to never allocate the same amount or less from the OS.
 *
 *    Once we have the item, we split it as much as needed.  Then we set the
 *    in_use bit of the item and return the area.
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
    debug("mem_alloc: needed blocks: %d\n", n);

    // try to find an item without increasing the array
    i = 0;
    while (i < array.size && (array.data[i].size < n || array.data[i].items == NULL))
    {
        i++;
    }

    // if not found, then increase the array and then allocate
    if (i == array.size)
    {
        i--;
        do 
        {
            array_inc_size(&array);
            i++;
        } while (array.data[i].size < n);

        item = alloc_new_item((unsigned int)array.data[i].size);
    }
    else
    {
        item = take_item(&array, i);
    }

    // split if needed to
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
 *    The function retuens the address of the buddy.  It also sets the
 *    address pointed by ibuddy, which corresponds to the index in the array
 *    where it would be inserted.  That is, the index of the array that
 *    gives the location of the free list of the same size as the buddy.
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
 *    void delete_item(struct array *array, unsigned int i, void *item)
 *  DESCRIPTION
 *    The function delete_item deletes one item, which it finds by address,
 *    from the free list specified by the index i in the array.  It deletes
 *    in two steps: first it finds the item, then it deletes it.  The delete
 *    operation is like a normal delete operation from a doubly linked list,
 *    except that if it's the first item, then the entry in the array is
 *    modified to point to the new head.
 *
 *    It is different from take_item, which removes any item from the free
 *    list.  The item deleted from the list can still be used (it is not
 *    freed), and can be inserted back.
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
 *    The coalesce function makes the opposite of splitting: it merges
 *    buddies that are not in use, and stops when it finds a buddy which is
 *    in use, which will happen sooner or later because the item at the top
 *    had a fake right buddy which is marked in use.
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
    while (!item_is_in_use(buddy)
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
 *    Return the item after use to the free list.  The first thing is to get
 *    the item pointer from the address from the area.  Using the header,
 *    it's easy to find the size, and, having found the size, we have the
 *    index which must match the size field of a free list in the array.
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
