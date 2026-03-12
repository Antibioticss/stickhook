#ifndef STICKHOOK_H
#define STICKHOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STICK_HEADSIZE 8
#define STICK_JUMPSIZE 16
#define STICK_ORIGSIZE (STICK_HEADSIZE + STICK_JUMPSIZE)

struct stick_entry {
    char *image_name;  /* image file name, used for static patching and dynamic hooking */
    uint64_t vmaddr;   /* vmaddr of the function in the image */
    void *replacement; /* new implementation function pointer */
    void *original;    /* original function pointer (statically generated) */
    uint64_t reserved; /* reserved for the vmaddr to store dispatcher ptr (end of __DATA section) */
};

void stick_init(void);

/*
void stick_hook(char *image_name, intptr_t vmaddr, void *replacement, void **originptr);
*/
#define stick_hook(_image_name, _vmaddr, _replacement, _originptr)                                                                                   \
    do {                                                                                                                                             \
        __attribute__((used, section("__TEXT,__text"))) static const unsigned char orig_wrapper[STICK_ORIGSIZE];                                     \
        __attribute__((used, section("__DATA,__stick_info"))) static const struct stick_entry hook_entry = {                                         \
            .image_name = (_image_name), .vmaddr = (_vmaddr), .replacement = (void *)(_replacement), .original = (void *)orig_wrapper};              \
        *(const void **)(_originptr) = orig_wrapper;                                                                                                 \
    } while (0);

/*
void stick_replace(char *image_name, intptr_t vmaddr, void *replacement);
*/
#define stick_replace(_image_name, _vmaddr, _replacement)                                                                                            \
    do {                                                                                                                                             \
        __attribute__((used, section("__DATA,__stick_info"))) static const struct stick_entry replace_entry = {                                      \
            .image_name = (_image_name), .vmaddr = (_vmaddr), .replacement = (void *)(_replacement)};                                                \
    } while (0);

#ifdef __cplusplus
}
#endif

#endif
