/*
 * Standalone IEF1 v1 writer for a single T_CVEC payload.
 * Shakti strings cannot hold NULs (strlen / T_STR), so MP4 bytes go here.
 *
 *   iefs_pack_cvec in.bin out.iefs
 *
 * Layout matches src/iefs_format.c (magic IEF1, CRC32 over payload).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IEFS_MAGIC "IEF1"
#define IEFS_VERSION 1u
#define IEFS_HEADER_SIZE 24u
#define T_CVEC 22
#define IEFS_MAX_ELEMS UINT32_MAX

static uint32_t crc32_table[256];
static int crc32_ready;

static void crc32_init(void) {
    uint32_t i;
    if (crc32_ready)
        return;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        int k;
        for (k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n) {
    size_t i;
    crc32_init();
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

static void put_u16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put_u64(unsigned char *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}

static int write_all(FILE *fp, const unsigned char *p, size_t n) {
    while (n > 0) {
        size_t w = fwrite(p, 1, n, fp);
        if (w == 0)
            return -1;
        p += w;
        n -= w;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *in_path;
    const char *out_path;
    FILE *in;
    FILE *out;
    long z;
    size_t n;
    size_t pay_len;
    size_t total;
    unsigned char *buf;
    unsigned char *file;
    char tmp_path[4096];
    uint32_t crc;

    if (argc != 3) {
        fprintf(stderr, "usage: iefs_pack_cvec <in.bin> <out.iefs>\n");
        return 2;
    }
    in_path = argv[1];
    out_path = argv[2];

    in = fopen(in_path, "rb");
    if (!in) {
        fprintf(stderr, "iefs_pack_cvec: open %s: %s\n", in_path, strerror(errno));
        return 1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "iefs_pack_cvec: seek %s\n", in_path);
        fclose(in);
        return 1;
    }
    z = ftell(in);
    if (z < 0) {
        fprintf(stderr, "iefs_pack_cvec: size %s\n", in_path);
        fclose(in);
        return 1;
    }
    if ((unsigned long)z > (unsigned long)IEFS_MAX_ELEMS) {
        fprintf(stderr, "iefs_pack_cvec: file too large\n");
        fclose(in);
        return 1;
    }
    n = (size_t)z;
    if (fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        return 1;
    }
    buf = malloc(n ? n : 1);
    if (!buf) {
        fprintf(stderr, "iefs_pack_cvec: out of memory\n");
        fclose(in);
        return 1;
    }
    if (n > 0 && fread(buf, 1, n, in) != n) {
        fprintf(stderr, "iefs_pack_cvec: read %s\n", in_path);
        free(buf);
        fclose(in);
        return 1;
    }
    fclose(in);

    pay_len = 1u + 8u + n;
    total = (size_t)IEFS_HEADER_SIZE + pay_len;
    file = malloc(total);
    if (!file) {
        fprintf(stderr, "iefs_pack_cvec: out of memory\n");
        free(buf);
        return 1;
    }
    memcpy(file, IEFS_MAGIC, 4);
    put_u16(file + 4, (uint16_t)IEFS_VERSION);
    put_u16(file + 6, 0);
    put_u64(file + 8, (uint64_t)pay_len);
    put_u32(file + 20, 0);
    file[IEFS_HEADER_SIZE] = (unsigned char)T_CVEC;
    put_u64(file + IEFS_HEADER_SIZE + 1, (uint64_t)n);
    if (n)
        memcpy(file + IEFS_HEADER_SIZE + 9, buf, n);
    crc = crc32_update(0, file + IEFS_HEADER_SIZE, pay_len);
    put_u32(file + 16, crc);
    free(buf);

    if (snprintf(tmp_path, sizeof tmp_path, "%s.tmp", out_path) >= (int)sizeof tmp_path) {
        fprintf(stderr, "iefs_pack_cvec: path too long\n");
        free(file);
        return 1;
    }
    out = fopen(tmp_path, "wb");
    if (!out) {
        fprintf(stderr, "iefs_pack_cvec: open %s: %s\n", tmp_path, strerror(errno));
        free(file);
        return 1;
    }
    if (write_all(out, file, total) != 0) {
        fprintf(stderr, "iefs_pack_cvec: write %s\n", tmp_path);
        fclose(out);
        remove(tmp_path);
        free(file);
        return 1;
    }
    if (fclose(out) != 0) {
        remove(tmp_path);
        free(file);
        return 1;
    }
    free(file);
    if (rename(tmp_path, out_path) != 0) {
        fprintf(stderr, "iefs_pack_cvec: rename: %s\n", strerror(errno));
        remove(tmp_path);
        return 1;
    }
    return 0;
}
