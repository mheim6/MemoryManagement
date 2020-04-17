# MemoryManagement
Memory Management Operating System

This assignment is about memory management by utilizing mmap and munmap level 2 system
calls to implement the malloc, calloc, realloc, and free family of functions.
Implementations of these functions are found in the implementation.c file which is used by the memory.c file. 
The mmap and munmap calls were used to create private, anonymous memory mappings to get basic, 
“raw” access to user space memory. 
Much memory waste is avoided by reducing the number of system calls and grouping them into large blocks of memory accesses.
