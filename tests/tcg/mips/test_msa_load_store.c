/*
 * MIPS MSA tests: Load / Store instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    VECTOR_BYTES = 16,
    BUFFER_BYTES = 64,
};

#define DEFINE_MSA_COPY(format)                                          \
static void copy_##format(const uint8_t *source, uint8_t *destination,    \
                          uint8_t *loaded, uint8_t *after_store)         \
{                                                                       \
    __asm__ volatile(                                                   \
        "ld." #format " $w0, 0(%[source])\n\t"                          \
        "st.b $w0, 0(%[loaded])\n\t"                                    \
        "st." #format " $w0, 0(%[destination])\n\t"                     \
        "st.b $w0, 0(%[after_store])\n\t"                               \
        :                                                               \
        : [source] "r" (source), [destination] "r" (destination),         \
          [loaded] "r" (loaded), [after_store] "r" (after_store)          \
        : "f0", "memory");                                             \
}

DEFINE_MSA_COPY(b)
DEFINE_MSA_COPY(h)
DEFINE_MSA_COPY(w)
DEFINE_MSA_COPY(d)

typedef void (*MsaCopy)(const uint8_t *source, uint8_t *destination,
                        uint8_t *loaded, uint8_t *after_store);

static const struct {
    const char *name;
    unsigned element_bytes;
    MsaCopy copy;
} formats[] = {
    { "b", 1, copy_b },
    { "h", 2, copy_h },
    { "w", 4, copy_w },
    { "d", 8, copy_d },
};

static bool check_format(size_t format_index, const uint8_t *source,
                         uint8_t *destination, size_t first_offset,
                         const char *region)
{
    _Alignas(VECTOR_BYTES) uint8_t loaded[VECTOR_BYTES];
    _Alignas(VECTOR_BYTES) uint8_t after_store[VECTOR_BYTES];
    uint8_t expected_vector[VECTOR_BYTES];
    uint8_t expected_memory[BUFFER_BYTES];
    const uint16_t endian_probe = 1;
    unsigned endian_mask = *(const uint8_t *)&endian_probe ? 0 :
                           formats[format_index].element_bytes - 1;

    for (size_t source_offset = first_offset;
         source_offset < first_offset + VECTOR_BYTES; source_offset++) {
        for (size_t destination_offset = first_offset;
             destination_offset < first_offset + VECTOR_BYTES;
             destination_offset++) {
            memset(destination, 0xa5, BUFFER_BYTES);
            memset(expected_memory, 0xa5, BUFFER_BYTES);
            memcpy(expected_memory + destination_offset,
                   source + source_offset, VECTOR_BYTES);

            for (size_t byte = 0; byte < VECTOR_BYTES; byte++) {
                expected_vector[byte] =
                    source[source_offset + (byte ^ endian_mask)];
            }

            formats[format_index].copy(source + source_offset,
                                      destination + destination_offset,
                                      loaded, after_store);

            if (memcmp(loaded, expected_vector, VECTOR_BYTES)) {
                fprintf(stderr, "ld.%s endian mismatch (%s, offset %zu)\n",
                        formats[format_index].name, region, source_offset);
                return false;
            }
            if (memcmp(destination, expected_memory, BUFFER_BYTES)) {
                fprintf(stderr, "st.%s memory mismatch (%s, offset %zu)\n",
                        formats[format_index].name, region,
                        destination_offset);
                return false;
            }
            if (memcmp(after_store, loaded, VECTOR_BYTES)) {
                fprintf(stderr, "st.%s changed its source register (%s)\n",
                        formats[format_index].name, region);
                return false;
            }
        }
    }

    return true;
}

int main(void)
{
    _Alignas(VECTOR_BYTES) uint8_t source[BUFFER_BYTES];
    _Alignas(VECTOR_BYTES) uint8_t destination[BUFFER_BYTES];
    long page_size = sysconf(_SC_PAGESIZE);
    size_t mapping_bytes;
    uint8_t *source_mapping;
    uint8_t *destination_mapping;
    uint8_t *page_source;
    uint8_t *page_destination;
    bool success = true;

    if (page_size < BUFFER_BYTES) {
        fprintf(stderr, "Invalid guest page size: %ld\n", page_size);
        return 1;
    }
    mapping_bytes = 4 * (size_t)page_size;
    source_mapping = mmap(NULL, mapping_bytes, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    destination_mapping = mmap(NULL, mapping_bytes, PROT_NONE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (source_mapping == MAP_FAILED || destination_mapping == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (mprotect(source_mapping + page_size, 2 * page_size,
                 PROT_READ | PROT_WRITE) != 0 ||
        mprotect(destination_mapping + page_size, 2 * page_size,
                 PROT_READ | PROT_WRITE) != 0) {
        perror("mprotect");
        return 1;
    }

    page_source = source_mapping + 2 * page_size - BUFFER_BYTES / 2;
    page_destination = destination_mapping + 2 * page_size - BUFFER_BYTES / 2;
    for (size_t byte = 0; byte < BUFFER_BYTES; byte++) {
        source[byte] = page_source[byte] = byte * 13 + 7;
    }

    printf("TAP version 14\n"
           "1..%zu\n",
           sizeof(formats) / sizeof(formats[0]));
    for (size_t format_index = 0;
         format_index < sizeof(formats) / sizeof(formats[0]); format_index++) {
        bool passed = check_format(format_index, source, destination, 0,
                                   "buffer") &&
                      check_format(format_index, page_source, page_destination,
                                   VECTOR_BYTES, "page boundary");

        printf("%s %zu - MSA ld/st.%s\n", passed ? "ok" : "not ok",
               format_index + 1, formats[format_index].name);
        success &= passed;
    }

    if (munmap(source_mapping, mapping_bytes) != 0 ||
        munmap(destination_mapping, mapping_bytes) != 0) {
        perror("munmap");
        return 1;
    }
    return success ? 0 : 1;
}
