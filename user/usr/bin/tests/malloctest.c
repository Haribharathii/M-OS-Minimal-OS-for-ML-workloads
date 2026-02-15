#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
    printf("malloctest: starting\n");
    
    /* Test sbrk(0) - should return current break */
    void *initial_brk = sbrk(0);
    printf("malloctest: initial break = %p\n", initial_brk);
    
    /* Test malloc - should call brk() internally */
    printf("malloctest: calling malloc(100)\n");
    char *ptr1 = (char *)malloc(100);
    printf("malloctest: malloc(100) returned %p\n", ptr1);
    
    if (ptr1 == NULL) {
        printf("malloctest: malloc failed!\n");
        return 1;
    }
    
    /* Write to the allocated memory */
    printf("malloctest: writing to allocated memory\n");
    ptr1[0] = 'A';
    ptr1[99] = 'Z';
    printf("malloctest: successfully wrote to memory: [0]=%c [99]=%c\n", ptr1[0], ptr1[99]);
    
    /* Test another malloc */
    printf("malloctest: calling malloc(200)\n");
    char *ptr2 = (char *)malloc(200);
    printf("malloctest: malloc(200) returned %p\n", ptr2);
    
    if (ptr2 == NULL) {
        printf("malloctest: second malloc failed!\n");
        return 1;
    }
    
    /* Check new break */
    void *final_brk = sbrk(0);
    printf("malloctest: final break = %p\n", final_brk);
    printf("malloctest: break grew by %d bytes\n", (int)((char*)final_brk - (char*)initial_brk));
    
    printf("malloctest: SUCCESS - all tests passed!\n");
    return 0;
}
