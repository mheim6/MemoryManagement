/*************************************************
* Team: Monica Heim
* Computer: Linux-18
* CSCE 321 - Homework Assignment 3: Memory Management
* C. Lauter
* Fall 2019
*************************************************/

#include <stddef.h> // size_t
#include <sys/mman.h> // mmap, munmap
#include <errno.h> // errno
#include <pthread.h> // pthread

/* ///////////////////////////////////////////////
// Copyright 2019-20 by
// University of Alaska Anchorage, College of Engineering
// All rights reserved.
//
// Contributors:  Christoph Lauter,
//                Monica Heim
//
// See file memory.c on how to compile this code.
/////////////////////////////////////////////////*/

/*******************************/
/* Predefined helper functions */
/*******************************/

static void *__memset(void *s, int c, size_t n) {
  unsigned char *p;
  size_t i;

  if (n == ((size_t) 0)) return s;
  for (i=(size_t) 0,p=(unsigned char *)s;
       i<=(n-((size_t) 1));
       i++,p++) {
    *p = (unsigned char) c;
  }
  return s;
}

static void *__memcpy(void *dest, const void *src, size_t n) {
  unsigned char *pd;
  const unsigned char *ps;
  size_t i;

  if (n == ((size_t) 0)) return dest;
  for (i=(size_t) 0,pd=(unsigned char *)dest,ps=(const unsigned char *)src;
       i<=(n-((size_t) 1));
       i++,pd++,ps++) {
    *pd = *ps;
  }
  return dest;
}

static int __try_size_t_multiply(size_t *c, size_t a, size_t b) {
  size_t t, r, q;

  /* If any of the arguments a and b is zero, everthing works just fine. */
  if ((a == ((size_t) 0)) ||
      (a == ((size_t) 0))) {
    *c = a * b;
    return 1;
  }

  t = a * b;
  q = t / a;
  r = t % a;

  /* If the rest r is non-zero, the multiplication overflowed. */
  if (r != ((size_t) 0)) return 0;

  if (q != b) return 0;
  *c = t;
  return 1;
}

/* End of predefined helper functions */

/*************************/
/* Your helper functions */
/*************************/

pthread_mutex_t global_impl_lock; // mutex object

/* Declaring a linked list - memory block storage */
struct __mem_block_struct_t{
  struct __mem_block_struct_t *next; // points to next memory block
  size_t size; // size bytes
  size_t mmap_size; // size allocated in the memory
  void *mmap_start; // memory block to be stored in this pointer
};

typedef struct __mem_block_struct_t mem_blk_t; // memory block node of data type struct __mem_block_struct_t

#define __ALLOC_HEAD_SIZE   offsetof(mem_blk_t, mmap_size)
#define __MEM_MAP_MIN_SIZE  __ALLOC_HEAD_SIZE + 32

static mem_blk_t *__free_mem_blks = NULL; // initialize: assign free memory block pointer to NULL

static void __get_memory_block(size_t rawsize)
{
  size_t nmemb, size;
  mem_blk_t *curr, *prev, *new;

  if(rawsize == ((size_t)0))  return NULL;

  size = rawsize - ((size_t)1);
  nmemb = size + sizeof(mem_blk_t);
  if(nmemb < size)  return NULL;

  nmemb /= sizeof(mem_blk_t);
  if(!__try_size_t_multiply(&size, nmemb, sizeof(mem_blk_t))) return NULL;

  for(curr = __free_mem_blks, prev = NULL; curr != NULL; curr = (prev = curr)->next)
  {
    if(curr->size >= size) // curr size >= size bytes
    {
      if((curr->size - size) < sizeof(mem_blk_t)) // allocated memory size < block of memory size
      {
        if(prev == NULL)  __free_mem_blks = curr->next; // assign free memory block pointer to the next curr pointer
        // else, prev != NULL
        prev->next = curr->next; // link next prev pointer to next curr pointer

        return curr;
      }
      // else, curr size < size bytes
      new = (mem_blk_t *)(((void *)curr) + size);
      new->size = curr->size - size; // link new size pointer to curr size pointer - size
      new->mmap_start = curr->mmap_start; // link new pointer to curr pointer
      new->mmap_size = curr->mmap_size; // link new memory size to curr memory size
      new->next = curr->next; // link next new pointer to next curr pointer

      if(prev == NULL) // prev == NULL
        __free_mem_blks = new; // assign free memory block pointer to a new pointer
      else // prev != NULL
        prev->next = new; // assign next prev pointer to new pointer

      curr->size = size; // assign curr size to size bytes

      return curr;
    }
  }
  return NULL;
}

static void __sort_memory_blocks(void *ptr)
{
  mem_blk_t *temp_ptr, *temp_next;
  void *temp_blk;

  temp_ptr = (mem_blk_t *)ptr; // assign temporary ptr to ptr
  temp_next = temp_ptr->next; // assign next temporary ptr to next ptr

  while(temp_next != NULL)
  {
    while(temp_next != temp_ptr)
    {
      if(temp_next->mmap_size < temp_ptr->mmap_size)
      {
        temp_blk = (void *)temp_ptr->mmap_start; // assign temporary block to a temporary memory block pointer
        temp_ptr->mmap_start = (void *)temp_next->mmap_start; // link temporary memory block pointer to next temporary memory block pointer
        temp_next->mmap_start = temp_blk; // link next temporary memory block pointer to temporary block
      }
      temp_ptr = temp_ptr->next; // assign temporary pointer to next temporary pointer
    }
    temp_ptr = ptr; // link temporary pointer to ptr
    temp_next = temp_next->next; // link next temporary pointer to next next temporary pointer
  }
  return temp_ptr;
}

static void __merge_free_memory_blocks(mem_blk_t *ptr, int trim)
{
  mem_blk_t *hit;
  int did_merge = 0; // merge flag set to false

  if(ptr == NULL) return;

  if(ptr->next == NULL) return;

  if((ptr->mmap_start == ptr->next->mmap_start) && ((((void *)ptr) + ptr->size) == ((void *)(ptr->next))))
  {
    hit = ptr->next; // link hit to the next pointer
    ptr->next = hit->next; // link the next ptr to the next hit
    ptr->size += hit->size; // assign ptr size to the sum of ptr size and hit size
    did_merge = 1; // merged true
  }

  if(ptr->next == NULL)
  {
    if(did_merge && trim) __trim_memory_maps();

    return;
  }

  if(ptr->next->next == NULL)
  {
    if(did_merge && trim)  __trim_memory_maps();
  }

  if((ptr->next->mmap_start == ptr->next->next->mmap_start) && ((((void *)(ptr->next)) + ptr->next->size) == ((void *)(ptr->next->next))))
  {
    hit = ptr->next->next; // link hit to next next ptr
    ptr->next->next = hit->next; // link next next ptr to next hit
    ptr->next->size += hit->size; // link next ptr size to hit size
    did_merge = 1; //merge true
  }

  if(did_merge && trim) __trim_memory_maps();
}

static void __insert_free_memory_block(mem_blk_t *ptr, int trim)
{
  mem_blk_t *curr, *prev;

  if(ptr == NULL) return;

  for(curr = __free_mem_blks, prev = NULL; curr != NULL; curr = (prev = curr)->next)
  {
    if(((void *)ptr) < ((void *)curr))  break;
  }

  if(prev == NULL)
  {
    ptr->next = __free_mem_blks; // assign next ptr to free memory blocks pointer
    __free_mem_blks = ptr; // assign free memory block pointer to ptr
    __merge_free_memory_blocks(ptr, trim);
  }
  // else, prev != NULL
  ptr->next = curr; // link next ptr to curr pointer
  prev->next = ptr; // link next prev pointer to ptr
  __merge_free_memory_blocks(prev, trim);
}

void __trim_memory_maps()
{
  mem_blk_t *curr, *prev, *next;

  for(curr = __free_mem_blks, prev = NULL; curr != NULL; curr = (prev = curr)->next)
  {
    if((curr->size == curr->mmap_size) && (curr->mmap_start == ((void *)curr)))
    {
      next = curr->next; // link next pointer to curr next pointer
      if(munmap(curr->mmap_start, curr->mmap_size) == 0)
      {
        if(prev == NULL) __free_mem_blks = next;
        // else, prev != NULL
        prev->next = next; // link next prev pointer to next pointer

        return;
      }
    }
  }
}

/* End of your helper functions */

/************************************************************/
/* Start of the actual malloc/calloc/realloc/free functions */
/************************************************************/

/* releases a block of memory block allocated by ptr */
void __free_impl(void *);

/* allocates an array of size bytes at runtime
   and leaves each element uninitialized */
void *__malloc_impl(size_t size)
{
  mem_blk_t *new_ptr;
  size_t sz, nmemb, minnmemb;
  void *ptr;

  if(size == ((size_t)0)) return NULL;

  sz = size + sizeof(mem_blk_t);
  if(sz < size) return NULL;

  ptr = (void *)(__get_memory_block(sz));

  if(ptr == NULL) return NULL;

  pthread_mutex_lock(&global_impl_lock);

  //__new_memory_map(sz);
  nmemb /= sizeof(mem_blk_t);
  minnmemb = __MEM_MAP_MIN_SIZE / sizeof(mem_blk_t);
  if(nmemb < minnmemb)  nmemb = minnmemb;

  if(!__try_size_t_multiply(&sz, nmemb, sizeof(mem_blk_t))) return;

  ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  //if(ptr == MAP_FAILED) return;
  if(ptr == MAP_FAILED)
  {
    pthread_mutex_unlock(&global_impl_lock);
    errno = EINVAL;

    return;
  }

  new_ptr = (mem_blk_t *)ptr; // assign new_ptr to ptr
  new_ptr->size = size; // assign new_ptr size to size bytes
  new_ptr->mmap_size = size; // assign new_ptr memory block size to size bytes
  new_ptr->mmap_start = ptr; // link memory block at new_ptr to ptr
  new_ptr->next = NULL; // assign next new_ptr to NULL

  __insert_free_memory_block(new_ptr, 0);
  //
  __sort_memory_blocks(new_ptr);

  ptr = (void *)(__get_memory_block(sz));

  return (ptr + sizeof(mem_blk_t));
}

/* allocates an array of nmemb elements
   each of size bytes and initializes
   the allocated memory to ZERO */
void *__calloc_impl(size_t nmemb, size_t size)
{
  size_t sz;
  void *ptr;

  if(!__try_size_t_multiply(&sz, nmemb, size))  return NULL;

  ptr = __malloc_impl(sz); // call __malloc_impl
  //if(ptr != NULL) __memset(ptr, 0, sz);
  if(ptr == NULL) return NULL;
  // else, ptr != NULL
  __memset(ptr, 0, sz); // allocate memory to ZERO

  return ptr;
}

/* re-alloctates memory extending it upto size bytes */
void *__realloc_impl(void *ptr, size_t size)
{
  mem_blk_t *old_mem_blk_t = (mem_blk_t *)(ptr - sizeof(mem_blk_t));
  void *new_ptr;
  size_t sz;

  sz = old_mem_blk_t->size; // assign sz to size of old memory block
  if(size < sz) sz = size;

  if(ptr == NULL) return __malloc_impl(size);

  if(size == ((size_t)0))
  {
    __free_impl(ptr);

    return NULL;
  }

  if(old_mem_blk_t->mmap_size >= size)  return ptr;

  new_ptr = __malloc_impl(size);
  if(new_ptr == NULL)  return NULL;

  __memcpy(new_ptr, ptr, sz); // copy to new_ptr from ptr of size sz bytes return new_ptr
  __free_impl(ptr); // free block of memory from ptr

  return new_ptr;
}

/* releases a block of memory block specified by ptr */
void __free_impl(void *ptr)
{
  mem_blk_t *head;

  if(ptr == NULL) return;

  __insert_free_memory_block(ptr - sizeof(mem_blk_t), 1);

  pthread_mutex_lock(&global_impl_lock);
  head = (mem_blk_t *)(ptr - (void *)1);
  if(munmap(head->mmap_start, head->size) == 0)
  {
    pthread_mutex_unlock(&global_impl_lock);

    return;
  }

  pthread_mutex_unlock(&global_impl_lock);

  return;
}

/* End of the actual malloc/calloc/realloc/free functions */
