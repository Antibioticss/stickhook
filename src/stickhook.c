#include "stickhook.h"

#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <stdint.h>
#include <string.h>

#ifdef COMPACT
    #define LOG_ERROR(fmt, ...) ((void)0)
#else
    #if TARGET_OS_OSX
        #include <printf.h> // fprintf()
        #define LOG_ERROR(fmt, ...) (void)fprintf(stderr, "ERROR [%s:%d]: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
    #elif TARGET_OS_IOS
        #include <os/log.h> // os_log()
        #define LOG_ERROR(fmt, ...) os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR, "ERROR [%s:%d]: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
    #endif
#endif

static int nstick;
static struct stick_entry *stick_info;

static int32_t name2index(const char *image_name) {
    size_t len = strlen(image_name);
    uint32_t total = _dyld_image_count();
    for (int32_t i = 0; i < total; i++) {
        const char *full_name = _dyld_get_image_name(i);
        size_t full_len = strlen(full_name);
        if (len <= full_len && strcmp(image_name, full_name + full_len - len) == 0) {
            return i;
        }
    }
    LOG_ERROR("name2index: %s not found!", image_name);
    return -1;
}

__attribute__((noinline, naked)) static void stick_dispatcher() {
    // x16 --> stick index!
    // x17 --> branch (yes can use)
    asm volatile("mov x17, %0\n"
                 "mul x16, x16, x17\n"
                 :
                 : "i"(sizeof(struct stick_entry))
                 : "x16", "x17");
    asm volatile("adrp x17, %0@PAGE\n"
                 "ldr  x17, [x17, %0@PAGEOFF]\n"
                 :
                 : "i"(&stick_info)
                 : "x17");
    asm volatile("add x17, x16, x17\n"
                 "ldr x17, [x17, %0]\n"
                 "br  x17\n"
                 :
                 : "i"(offsetof(struct stick_entry, replacement))
                 : "x17");
}

int stick_init(void) {
    void *self_slide = NULL;

    /* parse macho header */
    const struct mach_header_64 *self_header = &_mh_dylib_header;
    struct load_command *ld_command = (void *)(self_header + 1);
    struct section_64 *info_sect = NULL;
    for (int i = 0; i < self_header->ncmds; i++) {
        if (ld_command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (struct segment_command_64 *)ld_command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                /* slide + vmaddr = text_seg = header */
                /* slide = header - vmaddr */
                self_slide = (void *)self_header - segment->vmaddr;
            }
            else if (strcmp(segment->segname, "__DATA") == 0) {
                struct section_64 *data_sect = (void *)(segment + 1);
                for (int j = 0; j < segment->nsects; j++) {
                    if (strcmp(data_sect[j].sectname, "__stick_info") == 0) {
                        info_sect = data_sect + j;
                        break;
                    }
                }
            }
        }
        ld_command = (void *)ld_command + ld_command->cmdsize;
    }
    if (info_sect == NULL) {
        LOG_ERROR("stickhook_init: __stick_info section not found!");
        return 1;
    }

    /* update global stick info */
    nstick = (int)(info_sect->size / sizeof(struct stick_entry));
    stick_info = self_slide + info_sect->addr;

    intptr_t img_slide = 0;
    for (int i = 0; i < nstick; i++) {
        struct stick_entry *entry = stick_info + i;
        if (i == 0 || strcmp(entry->image_name, (entry - 1)->image_name) != 0) {
            // hooks for the same image should be in a row
            uint32_t img_idx = name2index(entry->image_name);
            if (img_idx == -1) continue;
            img_slide = _dyld_get_image_vmaddr_slide(img_idx);
            // reserved field of the first image stored the vmaddr of dispatcher ptr
            if (entry->reserved != 0) *(void **)(img_slide + entry->reserved) = stick_dispatcher;
        }
        entry->vmaddr += img_slide + STICK_HEADSIZE;
    }
    return 0;
}
