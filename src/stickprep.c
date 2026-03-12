#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/stickhook.h"

#define ADDR_MASK 0x0000FFFFFFFFFFFFULL

#define AARCH64_B 0x14000000    // b        +0
#define AARCH64_BL 0x94000000   // bl       +0
#define AARCH64_ADRP 0x90000011 // adrp     x17, 0
#define AARCH64_LDR 0xf9400231  // ldr      x17, [x17]
#define AARCH64_LDUR 0xf85f8231 // ldur     x17, [x17, -8]
#define AARCH64_BR 0xd61f0220   // br       x17
#define AARCH64_BLR 0xd63f0220  // blr      x17
#define AARCH64_ADD 0x91000231  // add      x17, x17, 0
#define AARCH64_SUB 0xd1000231  // sub      x17, x17, 0
#define AARCH64_MOV 0xd2800010  // mov      x16, 0

static struct {
    int64_t vm_slide;

    int nstick;
    uint64_t stick_vmaddr;
    uint32_t stick_offset;
    struct stick_entry *stick_info;

    int img_stick;  /* starting index of current image stick_entry */
    int nimg_stick; /* number of the current image stick_entry */
} lib;

static struct {
    int64_t vm_slide;
    uint64_t data_end;
    uint64_t stub_vmaddr;
    uint32_t stub_offset;
} bin;

/* file io */
void *read_file(FILE *fp, const size_t len);
void *read_file_off(FILE *fp, const size_t len, const long int offset);
int write_file(FILE *fp, const void *data, const size_t len);
int write_file_off(FILE *fp, const void *data, const size_t len, const long int offset);

/* mach-o parser */
int parse_lib(FILE *dylib, const char *bin_name);
int parse_bin(FILE *binary);

/* aarch64 assembly */
uint32_t a64_mov(uint32_t imm);
uint32_t a64_add(uint32_t imm);
uint32_t a64_b(uint64_t src, uint64_t dst);
uint32_t a64_adrp(uint64_t src, uint64_t dst);

int main(int argc, char **argv) {
    int err = 0;

    if (argc != 3) {
        printf("usage: stickprep <dylib> <binary>");
        return 1;
    }

    FILE *dylib, *binary;
    const char *bin_name;

    dylib = fopen(argv[1], "rb+");
    if (dylib == NULL) {
        perror("fopen");
        return 1;
    }
    binary = fopen(argv[2], "rb+");
    if (binary == NULL) {
        perror("fopen");
        err = 1;
        goto exit;
    }
    bin_name = strrchr(argv[2], '/') + 1;
    if (bin_name == NULL + 1) bin_name = argv[2];

    /* parse macho files */
    if (parse_lib(dylib, bin_name) || parse_bin(binary)) {
        err = 1;
        goto exit;
    }

    /* time for patchhhhing! */

    /* update stub info in stick entry */
    lib.stick_info[lib.img_stick].reserved = bin.data_end - sizeof(void *);
    /* write dispatcher stub to binary */
    uint32_t stub_code[3];
    stub_code[0] = a64_adrp(bin.stub_vmaddr, bin.data_end);
    stub_code[1] = AARCH64_LDUR; /* load stick_dispatcher address */
    stub_code[2] = AARCH64_BR;
    write_file_off(binary, stub_code, sizeof(stub_code), bin.stub_offset);

    /* insert jumps and store origins */
    for (int i = lib.img_stick; i < lib.img_stick + lib.nimg_stick; i++) {
        const struct stick_entry *entry = lib.stick_info + i;
        /* store origin function stub */
        if (entry->original != 0) {
            uint32_t origin_code[STICK_ORIGSIZE / 4];
            /* store header first */
            void *func_header = read_file_off(binary, STICK_HEADSIZE, (long)(bin.vm_slide + entry->vmaddr));
            memcpy(origin_code, func_header, STICK_HEADSIZE);
            free(func_header);
            /* insert a jump */
            int64_t origin_addr = ADDR_MASK & (uint64_t)entry->original;
            int64_t dst_vmaddr = (int64_t)lib.stick_vmaddr + (int64_t)&entry->vmaddr - (int64_t)lib.stick_info;
            origin_code[STICK_HEADSIZE / 4 + 0] = a64_adrp(origin_addr + STICK_HEADSIZE, dst_vmaddr);
            origin_code[STICK_HEADSIZE / 4 + 1] = a64_add(dst_vmaddr & 0xfff);
            origin_code[STICK_HEADSIZE / 4 + 2] = AARCH64_LDR; /* load original function address from .vmaddr */
            origin_code[STICK_HEADSIZE / 4 + 3] = AARCH64_BR;
            /* write to dylib */
            write_file_off(dylib, origin_code, STICK_ORIGSIZE, origin_addr + lib.vm_slide);
        }
        /* write jumps to the dispatcher stub */
        uint32_t jump_insn[2];
        jump_insn[0] = a64_mov(i);
        jump_insn[1] = a64_b(entry->vmaddr + 4, bin.stub_vmaddr);
        write_file_off(binary, jump_insn, sizeof(jump_insn), (long)(entry->vmaddr + bin.vm_slide));
    }

    /* write back updated stick info! */
    write_file_off(dylib, lib.stick_info, sizeof(struct stick_entry) * lib.nstick, lib.stick_offset);

exit:
    (void)fclose(dylib);
    (void)fclose(binary);
    free(lib.stick_info);
    return err;
}

void *read_file(FILE *fp, const size_t len) {
    void *data = malloc(len);
    if (fread(data, len, 1, fp) != 1) {
        free(data);
        perror("fread");
        return NULL;
    }
    return data;
}

void *read_file_off(FILE *fp, const size_t len, const long int offset) {
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("fseek");
        return NULL;
    }
    return read_file(fp, len);
}

int write_file(FILE *fp, const void *data, const size_t len) {
    if (fwrite(data, len, 1, fp) != 1) {
        perror("fwrite");
        return 1;
    }
    return 0;
}

int write_file_off(FILE *fp, const void *data, const size_t len, const long int offset) {
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("fseek");
        return 1;
    }
    return write_file(fp, data, len);
}

int parse_lib(FILE *dylib, const char *bin_name) {
    int err = 0;

    (void)fseek(dylib, 0, SEEK_END);
    const struct mach_header_64 *header = read_file_off(dylib, ftell(dylib), 0);
    if (header->magic != MH_MAGIC_64) {
        (void)fprintf(stderr, "stickprep: dylib is not a valid 64-bit Mach-O file!\n");
        err = 1;
        goto exit;
    }

    /* parse load commands */
    const struct load_command *commands = (void *)(header + 1);
    const struct load_command *command = commands;
    struct section_64 *info_sect = NULL;
    for (int i = 0; i < header->ncmds; i++) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (struct segment_command_64 *)command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                lib.vm_slide = (int64_t)segment->fileoff - (int64_t)segment->vmaddr;
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
        command = (void *)command + command->cmdsize;
    }

    if (info_sect == NULL) {
        (void)fprintf(stderr, "stickprep: __stick_info not found!\n");
        err = 1;
        goto exit;
    }

    /* update stick info */
    lib.nstick = (int)(info_sect->size / sizeof(struct stick_entry));
    lib.stick_vmaddr = info_sect->addr;
    lib.stick_offset = info_sect->offset;
    lib.stick_info = malloc(info_sect->size);
    memcpy(lib.stick_info, (void *)header + lib.stick_offset, info_sect->size);

#define ENTRY_NAME(i) (str_base + (ADDR_MASK & (int64_t)(lib.stick_info + (i))->image_name))
    const char *str_base = (const char *)header + lib.vm_slide;
    /* check image names in stick info */
    int nuniq = 0;
    const char **unique_names = (const char **)malloc(lib.nstick * sizeof(char *));
    for (int i = 0; i < lib.nstick - 1; i++) {
        const char *curn = ENTRY_NAME(i);
        const char *nextn = ENTRY_NAME(i + 1);
        if (strcmp(curn, nextn) != 0) {
            for (int j = 0; j < nuniq; j++) {
                if (strcmp(unique_names[j], nextn) == 0) {
                    (void)fprintf(stderr, "stickprep: hooks for image '%s' are not in a row\n", bin_name);
                    free((void *)unique_names);
                    err = 1;
                    goto exit;
                }
            }
            unique_names[nuniq++] = curn;
        }
    }
    free((void *)unique_names);

    /* update current image info */
    lib.img_stick = -1;
    lib.nimg_stick = 0;
    for (int i = 0; i < lib.nstick; i++) {
        if (strcmp(bin_name, ENTRY_NAME(i)) == 0) {
            lib.img_stick = i;
            break;
        }
    }
    if (lib.img_stick == -1) {
        (void)fprintf(stderr, "stickprep: no hook found for image: %s\n", bin_name);
        err = 1;
        goto exit;
    }
    for (int i = lib.img_stick; i < lib.nstick; i++) {
        if (strcmp(bin_name, ENTRY_NAME(i)) != 0) break;
        lib.nimg_stick++;
    }
#undef ENTRY_NAME

exit:
    free((void *)header);
    return err;
}

int parse_bin(FILE *binary) {
    (void)fseek(binary, 0, SEEK_SET);
    const struct mach_header_64 *header = read_file(binary, sizeof(struct mach_header_64));
    if (header->magic != MH_MAGIC_64) {
        (void)fprintf(stderr, "stickprep: binary is not a valid 64-bit Mach-O file!\n");
        free((void *)header);
        return 1;
    }

    /* parse load commands */
    const struct load_command *commands = read_file(binary, header->sizeofcmds);
    const struct load_command *command = commands;
    for (int i = 0; i < header->ncmds; i++) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (struct segment_command_64 *)command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                bin.vm_slide = (int64_t)segment->fileoff - (int64_t)segment->vmaddr;
                struct section_64 *text_sect = (void *)(segment + 1);
                for (int j = 0; j < segment->nsects; j++) {
                    if (strcmp(text_sect[j].sectname, "__text") == 0) {
                        bin.stub_offset = sizeof(struct mach_header_64) + (uint64_t)&text_sect[j].reserved1 - (uint64_t)commands;
                        bin.stub_vmaddr = bin.stub_offset + bin.vm_slide;
                        break;
                    }
                }
            }
            else if (strcmp(segment->segname, "__DATA") == 0) {
                bin.data_end = segment->vmaddr + segment->vmsize;
            }
        }
        command = (void *)command + command->cmdsize;
    }
    free((void *)header);
    free((void *)commands);
    return 0;
}

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
