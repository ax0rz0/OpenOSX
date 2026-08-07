#pragma once

#include <stdint.h>
#include <string.h>
#include <byteswap.h>

typedef uint32_t cpu_type_t;
typedef uint32_t cpu_subtype_t;
// Types for memory mapping
typedef uintptr_t vm_offset_t;
typedef size_t    vm_size_t;

// ABI64 flag used in cputype
#define CPU_ARCH_ABI64 0x01000000

typedef enum {
    NX_UnknownByteOrder,
    NX_LittleEndian,
    NX_BigEndian
} NXByteOrder;

typedef struct {
    const char *name;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    NXByteOrder byteorder;
    const char *description;
} NXArchInfo;

static const NXArchInfo *NXGetLocalArchInfo(void) {
    static const NXArchInfo x86_64_host = {
        "x86_64", 0x01000007, 3, NX_LittleEndian, NULL
    };
    return &x86_64_host;
}

// Mach-O constants
#define MH_MAGIC        0xfeedface
#define MH_MAGIC_64     0xfeedfacf
#define MH_KEXT_BUNDLE  0xb
#define MH_INCRLINK     0x2

#define LC_SEGMENT      0x1
#define LC_SEGMENT_64   0x19
#define LC_SYMTAB       0x2
#define LC_UUID         0x1b

#define SG_NORELOC      0x4

#define N_EXT           0x01
#define N_UNDF          0x0
#define N_INDR          0xa
#define N_DESC_DISCARDED 0x20

#define SEG_LINKEDIT    "__LINKEDIT"

#define TRUE 1
#define FALSE 0
typedef int boolean_t;

#define mach_vm_round_page(x) (((x) + 0xfff) & ~0xfff)

// Mach-O structs (simplified)
struct mach_header {
    uint32_t magic;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct segment_command {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct nlist {
    union {
        uint32_t n_strx;
    } n_un;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint32_t n_value;
};

struct symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct uuid_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint8_t uuid[16];
};

struct mach_header_64 {
    uint32_t magic;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct nlist_64 {
    union {
        uint32_t n_strx;
    } n_un;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint64_t n_value;
};

/* Generic load-command header */
struct load_command {
    uint32_t cmd;      /* LC_* constant */
    uint32_t cmdsize;  /* total size of this command */
};

/* 32-bit section record (68 bytes) */
struct section {
    char      sectname[16];
    char      segname[16];
    uint32_t  addr;
    uint32_t  size;
    uint32_t  offset;
    uint32_t  align;
    uint32_t  reloff;
    uint32_t  nreloc;
    uint32_t  flags;
    uint32_t  reserved1;
    uint32_t  reserved2;
};

/* 64-bit section record (80 bytes) */
struct section_64 {
    char      sectname[16];
    char      segname[16];
    uint64_t  addr;
    uint64_t  size;
    uint32_t  offset;
    uint32_t  align;
    uint32_t  reloff;
    uint32_t  nreloc;
    uint32_t  flags;
    uint32_t  reserved1;
    uint32_t  reserved2;
    uint32_t  reserved3;
};

/* Section-attribute bits the tool cares about */
#define S_ATTR_DEBUG 0x02000000u   /* section contains debug data */

/* Already-defined constants for byte-swapped headers */
#define MH_CIGAM      0xcefaedfeu
#define MH_CIGAM_64   0xcffaedfeu

// Dummy byte swapping functions for Linux (no-op)
static inline void swap_mach_header(struct mach_header *h, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        h->magic        = __builtin_bswap32(h->magic);
        h->cputype      = __builtin_bswap32(h->cputype);
        h->cpusubtype   = __builtin_bswap32(h->cpusubtype);
        h->filetype     = __builtin_bswap32(h->filetype);
        h->ncmds        = __builtin_bswap32(h->ncmds);
        h->sizeofcmds   = __builtin_bswap32(h->sizeofcmds);
        h->flags        = __builtin_bswap32(h->flags);
    }
}

static inline void swap_mach_header_64(struct mach_header_64 *h, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        h->magic        = __builtin_bswap32(h->magic);
        h->cputype      = __builtin_bswap32(h->cputype);
        h->cpusubtype   = __builtin_bswap32(h->cpusubtype);
        h->filetype     = __builtin_bswap32(h->filetype);
        h->ncmds        = __builtin_bswap32(h->ncmds);
        h->sizeofcmds   = __builtin_bswap32(h->sizeofcmds);
        h->flags        = __builtin_bswap32(h->flags);
        h->reserved     = __builtin_bswap32(h->reserved);
    }
}

static inline void swap_segment_command(struct segment_command *s, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        s->cmd      = __builtin_bswap32(s->cmd);
        s->cmdsize  = __builtin_bswap32(s->cmdsize);
        s->vmaddr   = __builtin_bswap32(s->vmaddr);
        s->vmsize   = __builtin_bswap32(s->vmsize);
        s->fileoff  = __builtin_bswap32(s->fileoff);
        s->filesize = __builtin_bswap32(s->filesize);
        s->maxprot  = __builtin_bswap32(s->maxprot);
        s->initprot = __builtin_bswap32(s->initprot);
        s->nsects   = __builtin_bswap32(s->nsects);
        s->flags    = __builtin_bswap32(s->flags);
    }
}

static inline void swap_segment_command_64(struct segment_command_64 *s, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        s->cmd      = __builtin_bswap32(s->cmd);
        s->cmdsize  = __builtin_bswap32(s->cmdsize);
        s->vmaddr   = __builtin_bswap64(s->vmaddr);
        s->vmsize   = __builtin_bswap64(s->vmsize);
        s->fileoff  = __builtin_bswap64(s->fileoff);
        s->filesize = __builtin_bswap64(s->filesize);
        s->maxprot  = __builtin_bswap32(s->maxprot);
        s->initprot = __builtin_bswap32(s->initprot);
        s->nsects   = __builtin_bswap32(s->nsects);
        s->flags    = __builtin_bswap32(s->flags);
    }
}

static inline void swap_symtab_command(struct symtab_command *s, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        s->cmd     = __builtin_bswap32(s->cmd);
        s->cmdsize = __builtin_bswap32(s->cmdsize);
        s->symoff  = __builtin_bswap32(s->symoff);
        s->nsyms   = __builtin_bswap32(s->nsyms);
        s->stroff  = __builtin_bswap32(s->stroff);
        s->strsize = __builtin_bswap32(s->strsize);
    }
}

static inline void swap_uuid_command(struct uuid_command *s, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        s->cmd     = __builtin_bswap32(s->cmd);
        s->cmdsize = __builtin_bswap32(s->cmdsize);
        // UUID is just a byte array — no need to swap
    }
}

static inline void swap_nlist(struct nlist *n, int count, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        for (int i = 0; i < count; i++) {
            n[i].n_un.n_strx = __builtin_bswap32(n[i].n_un.n_strx);
            n[i].n_desc      = __builtin_bswap16(n[i].n_desc);
            n[i].n_value     = __builtin_bswap32(n[i].n_value);
        }
    }
}

static inline void swap_nlist_64(struct nlist_64 *n, int count, NXByteOrder order) {
    if (order != NX_LittleEndian) {
        for (int i = 0; i < count; i++) {
            n[i].n_un.n_strx = __builtin_bswap32(n[i].n_un.n_strx);
            n[i].n_desc      = __builtin_bswap16(n[i].n_desc);
            n[i].n_value     = __builtin_bswap64(n[i].n_value);
        }
    }
}

static inline uint32_t OSSwapInt16(uint32_t x) {
    return __builtin_bswap16(x);
}

static inline uint32_t OSSwapInt32(uint32_t x) {
    return __builtin_bswap32(x);
}
