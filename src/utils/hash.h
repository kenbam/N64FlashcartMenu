#ifndef UTILS_HASH_H
#define UTILS_HASH_H

#include <stddef.h>
#include <stdint.h>

#define FNV1A_64_OFFSET_BASIS 14695981039346656037ULL
#define FNV1A_64_PRIME        1099511628211ULL
#define FNV1A_32_OFFSET_BASIS 2166136261u
#define FNV1A_32_PRIME        16777619u

static inline uint64_t fnv1a64_str(const char *s) {
    uint64_t h = FNV1A_64_OFFSET_BASIS;
    if (!s) {
        return h;
    }
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= FNV1A_64_PRIME;
    }
    return h;
}

static inline uint64_t fnv1a64_buf(const void *data, size_t len) {
    uint64_t h = FNV1A_64_OFFSET_BASIS;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV1A_64_PRIME;
    }
    return h;
}

static inline uint32_t fnv1a32_u8(uint32_t h, uint8_t v) {
    h ^= v;
    h *= FNV1A_32_PRIME;
    return h;
}

static inline uint32_t fnv1a32_str(uint32_t h, const char *s) {
    if (!s) {
        return fnv1a32_u8(h, 0xFF);
    }
    for (; *s; s++) {
        h = fnv1a32_u8(h, (uint8_t)(*s));
    }
    return fnv1a32_u8(h, 0xFF);
}

static inline uint32_t fnv1a32_u64(uint32_t h, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        h = fnv1a32_u8(h, (uint8_t)((v >> (i * 8)) & 0xFF));
    }
    return h;
}

#endif
