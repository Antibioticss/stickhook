
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/mman.h> // mmap
#include <sys/stat.h>
#include <unistd.h>

#include "stickhook.h"

#define ADDR_MASK 0x0000FFFFFFFFFFFFULL

enum stick_error {
    STICK_OK,
    STICK_ENOTMACHO,
    STICK_ENOTEXTSECT,
    STICK_ENOINFOSECT,
    STICK_EBADORDER,
    STICK_MAPFAIL,
};
const char *stick_errstr[] = {
    "success",
    "not a valid 64-bit Mach-O file",
    "__text section not found in",
    "__stick_info section not found in",
    "images are not in a row in",
    "failed to map file",
};
enum stick_error stick_errno;

void pstickerr(const char *name, const char *file) {
    (void)fprintf(stderr, "%s: %s '%s'\n", name, stick_errstr[stick_errno], file);
}

/* aarch64 assembly */
#define AARCH64_B    0x14000000 // b        +0
#define AARCH64_BL   0x94000000 // bl       +0
#define AARCH64_ADRP 0x90000011 // adrp     x17, 0
#define AARCH64_LDR  0xf9400231 // ldr      x17, [x17]
#define AARCH64_LDUR 0xf85f8231 // ldur     x17, [x17, -8]
#define AARCH64_BR   0xd61f0220 // br       x17
#define AARCH64_BLR  0xd63f0220 // blr      x17
#define AARCH64_ADD  0x91000231 // add      x17, x17, 0
#define AARCH64_SUB  0xd1000231 // sub      x17, x17, 0
#define AARCH64_MOV  0xd2800010 // mov      x16, 0

uint32_t a64_mov(uint32_t imm) {
    return (imm << 5) | AARCH64_MOV;
}

uint32_t a64_add(uint32_t imm) {
    return (imm << 10) | AARCH64_ADD;
}

uint32_t a64_b(uint64_t src, uint64_t dst) {
    int64_t dist = (int64_t)dst - (int64_t)src;
    return (dist >> 2 & 0x3ffffff) | AARCH64_B;
}

uint32_t a64_adrp(uint64_t src, uint64_t dst) {
    int64_t dist = (int64_t)(dst >> 12) - (int64_t)(src >> 12);
    return ((dist & 0x3) << 29) | ((dist & 0x1ffffc) << 3) | AARCH64_ADRP;
}

/*  read/write file with mmap */
struct mapped {
    off_t size;
    void *mem;
};

int map_file(const char *file, struct mapped *mpd) {
    int fd;
    void *mem;
    struct stat st;

    fd = open(file, O_RDWR);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    if (fstat(fd, &st) == -1) {
        perror("fstat");
        close(fd);
        return 1;
    }

    mem = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    mpd->mem = mem;
    mpd->size = st.st_size;

    close(fd);
    return 0;
}

int unmap_file(struct mapped *mpd) {
    int err = 0;

    if (msync(mpd->mem, mpd->size, MS_SYNC) == -1) {
        perror("msync");
        err = 1;
    }

    if (munmap(mpd->mem, mpd->size) == -1) {
        perror("munmap");
        err = 1;
    }

    memset(mpd, 0, sizeof(struct mapped));

    return err;
}

/* mach-o parse */
struct stick_img {
    int index;
    const char *name;
};

struct stick_lib {
    void *data;
    int64_t vm_slide;

    int nstick;
    uint32_t stick_offset;
    uint64_t stick_vmaddr;
    struct stick_entry *entries;

    int nimg;
    struct stick_img *images;
};

struct target_bin {
    int64_t vm_slide;
    uint64_t disp_addr; /* the end of __DATA, 4KB aligned address */
    struct {
        uint32_t offset;
        uint64_t vmaddr;
    } stick_stub;
};

int parse_target(struct target_bin *bin, const void *data) {
    const struct mach_header_64 *header = data;
    if (header->magic != MH_MAGIC_64) {
        stick_errno = STICK_ENOTMACHO;
        return 1;
    }

    const struct section_64 *text_sect = NULL;
    const struct load_command *command = data + sizeof(struct mach_header_64);
    for (int i = 0; i < header->ncmds; i++) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (struct segment_command_64 *)command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                bin->vm_slide = (int64_t)segment->fileoff - (int64_t)segment->vmaddr;
                const struct section_64 *sections = (void *)segment + sizeof(struct segment_command_64);
                for (int j = 0; j < segment->nsects; j++) {
                    const struct section_64 *section = sections + j;
                    if (strcmp(section->sectname, "__text") == 0) {
                        text_sect = section;
                        break;
                    }
                }
            }
            else if (strcmp(segment->segname, "__DATA") == 0) {
                bin->disp_addr = segment->vmaddr + segment->vmsize;
            }
        }
        command = (void *)command + command->cmdsize;
    }
    if (text_sect == NULL) {
        stick_errno = STICK_ENOTEXTSECT;
        return 1;
    }

    bin->stick_stub.offset = (uint64_t)&text_sect->reserved1 - (uint64_t)data;
    bin->stick_stub.vmaddr = bin->stick_stub.offset + bin->vm_slide;
    return 0;
}

int parse_library(struct stick_lib *lib, const void *data) {
    lib->data = (void *)data;
    const struct mach_header_64 *header = data;
    if (header->magic != MH_MAGIC_64) {
        stick_errno = STICK_ENOTMACHO;
        return 1;
    }

    const struct section_64 *info_sect = NULL;
    const struct load_command *command = data + sizeof(struct mach_header_64);
    for (int i = 0; i < header->ncmds; i++) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (struct segment_command_64 *)command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                lib->vm_slide = (int64_t)segment->fileoff - (int64_t)segment->vmaddr;
            }
            else if (strcmp(segment->segname, "__DATA") == 0) {
                const struct section_64 *sections = (void *)segment + sizeof(struct segment_command_64);
                for (int j = 0; j < segment->nsects; j++) {
                    const struct section_64 *section = sections + j;
                    if (strcmp(section->sectname, "__stick_info") == 0) {
                        info_sect = section;
                        break;
                    }
                }
            }
        }
        command = (void *)command + command->cmdsize;
    }
    if (info_sect == NULL) {
        stick_errno = STICK_ENOINFOSECT;
        return 1;
    }

    /* update stick info */
    lib->nstick = (int)(info_sect->size / sizeof(struct stick_entry));
    lib->stick_vmaddr = info_sect->addr;
    lib->stick_offset = info_sect->offset;
    lib->entries = (void *)data + info_sect->offset;

    /* check image names in stick info */
    int nimg = 0;
    struct stick_img *images = malloc(lib->nstick * sizeof(struct stick_img));
    const char *prev = NULL;
    for (int i = 0; i < lib->nstick; i++) {
        const char *curr = (const char *)data + lib->vm_slide + (ADDR_MASK & (int64_t)lib->entries[i].image_name);
        if (prev == NULL || strcmp(prev, curr) != 0) {
            /* new image */
            for (int j = 0; j < nimg; j++) {
                if (strcmp(images[j].name, curr) == 0) {
                    stick_errno = STICK_EBADORDER;
                    free((void *)images);
                    return 1;
                }
            }
            images[nimg++] = (struct stick_img){.name = curr, .index = i};
            prev = curr;
        };
    }

    /* update images */
    lib->nimg = nimg;
    lib->images = images;
    return 0;
}

int install_hook(const char *target_path, struct stick_lib *lib, int imgidx) {
    struct mapped binmpd;
    struct target_bin bin;

    if (map_file(target_path, &binmpd) != 0) {
        stick_errno = STICK_MAPFAIL;
        return 1;
    }
    if (parse_target(&bin, binmpd.mem) != 0) {
        unmap_file(&binmpd);
        return 1;
    }

    int entry_start = lib->images[imgidx].index;
    int entry_end;
    if (imgidx + 1 < lib->nimg)
        entry_end = lib->images[imgidx + 1].index;
    else
        entry_end = lib->nstick;

    uint32_t *bin_insn, *lib_insn;
    // first entry stored the address of dispatcher ptr
    lib->entries[entry_start].reserved = bin.disp_addr - sizeof(void *);
    // write dispatcher stub into target
    bin_insn = binmpd.mem + bin.stick_stub.offset;
    bin_insn[0] = a64_adrp(bin.stick_stub.vmaddr, bin.disp_addr);
    bin_insn[1] = AARCH64_LDUR; // load the address of stick_dispatcher func
    bin_insn[2] = AARCH64_BR;

    // insert function hooks and store original func
    for (int i = entry_start; i < entry_end; i++) {
        const struct stick_entry *entry = lib->entries + i;
        if (entry->original != 0) {
            // store original func stub in library
            uint64_t stub_vmaddr = ADDR_MASK & (uint64_t)entry->original;
            // copy header first
            lib_insn = lib->data + stub_vmaddr + lib->vm_slide;
            memcpy(lib_insn, binmpd.mem + entry->vmaddr + bin.vm_slide, STICK_HEADSIZE);
            lib_insn = (void *)lib_insn + STICK_HEADSIZE;
            // insert a jump to original function
            uint64_t vmaddr_addr = lib->stick_vmaddr + i * sizeof(struct stick_entry) + offsetof(struct stick_entry, vmaddr);
            lib_insn[0] = a64_adrp(stub_vmaddr + STICK_HEADSIZE, vmaddr_addr);
            lib_insn[1] = a64_add(vmaddr_addr & 0xfff);
            lib_insn[2] = AARCH64_LDR; // load entry.vmaddr, image slide will be added in stick_init()
            lib_insn[3] = AARCH64_BR;
        }
        // insert func hook (jump to the dispatcher stub)
        bin_insn = binmpd.mem + entry->vmaddr + bin.vm_slide;
        bin_insn[0] = a64_mov(i);
        bin_insn[1] = a64_b(entry->vmaddr + 4, bin.stick_stub.vmaddr);
    }

    unmap_file(&binmpd);
    return 0;
}

int stick_prep(const char *library_path, const char *target_path) {
    struct mapped libmpd;
    struct stick_lib lib;

    if (map_file(library_path, &libmpd) != 0) {
        stick_errno = STICK_MAPFAIL;
        pstickerr("stickprep", library_path);
        return 1;
    }
    if (parse_library(&lib, libmpd.mem) != 0) {
        pstickerr("stickprep", library_path);
        unmap_file(&libmpd);
        return 1;
    }

    int installed = 0;
    size_t full_len = strlen(target_path);
    for (int i = 0; i < lib.nimg; i++) {
        size_t name_len = strlen(lib.images[i].name);
        if (name_len <= full_len && strcmp(lib.images[i].name, target_path + full_len - name_len) == 0) {
            printf("installing hook on '%s'\n", target_path);
            if (install_hook(target_path, &lib, i) != 0) {
                pstickerr("stickprep", target_path);
                free(lib.images);
                unmap_file(&libmpd);
                return 1;
            }
            installed++;
            break;
        }
    }

    if (installed == 0) {
        printf("no matching hook found for '%s'\n", target_path);
    }

    free(lib.images);
    unmap_file(&libmpd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: stickprep <library> <target>\n");
        return 1;
    }

    if (stick_prep(argv[1], argv[2]) != 0) {
        return 1;
    }
    return 0;
}
