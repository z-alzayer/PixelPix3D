// This file owns both stb implementations — only define them once.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "image_load.h"
#include "camera.h"
#include "filter.h"
#include "apng_enc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

// ---------------------------------------------------------------------------
// Helper: pack RGB888 pixels → RGB565 dst with nearest-neighbour scale
// ---------------------------------------------------------------------------
static void pixels_to_rgb565(const uint8_t *pixels, int img_w, int img_h,
                              uint16_t *dst, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx  = x * img_w / width;
            int sy  = y * img_h / height;
            int idx = (sy * img_w + sx) * 3;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            dst[y * width + x] = ((uint16_t)(r >> 3) << 11)
                                | ((uint16_t)(g >> 2) <<  5)
                                |  (uint16_t)(b >> 3);
        }
    }
}

int load_jpeg_to_rgb565(const char *path, uint16_t *dst, int width, int height) {
    int img_w, img_h, channels;
    uint8_t *pixels = stbi_load(path, &img_w, &img_h, &channels, 3);
    if (!pixels) return 0;
    pixels_to_rgb565(pixels, img_w, img_h, dst, width, height);
    stbi_image_free(pixels);
    return 1;
}

// ---------------------------------------------------------------------------
// load_apng_2frames_to_rgb565
// Parse the APNG chunk stream and decode frame 0 (IDAT) and frame 1 (fdAT)
// into dst0 and dst1 respectively.  Both are scaled to (width x height).
// ---------------------------------------------------------------------------

// Read a big-endian u32 from raw bytes
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

// CRC-32 for a buffer (reuse table from apng_enc.c if already built,
// but we can compute inline here as a local helper)
static uint32_t simple_crc32(const uint8_t *data, size_t len) {
    // Standard CRC-32 poly 0xEDB88320
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return ~crc;
}

// Build a standalone PNG in out_buf (must be >= raw_len + 300 bytes):
//   PNG sig + IHDR (from src_ihdr[13]) + IDAT(data, len) + IEND
// Returns total bytes written.
static size_t build_single_png(uint8_t *out, size_t out_cap,
                                const uint8_t *ihdr13,
                                const uint8_t *idat_data, uint32_t idat_len) {
    if (out_cap < idat_len + 300) return 0;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    size_t pos = 0;
    // sig
    memcpy(out + pos, sig, 8); pos += 8;
    // IHDR chunk: len=13
    out[pos]=0; out[pos+1]=0; out[pos+2]=0; out[pos+3]=13; pos += 4;
    memcpy(out + pos, "IHDR", 4); // type
    memcpy(out + pos + 4, ihdr13, 13);
    uint32_t crc = simple_crc32(out + pos, 17);
    pos += 17;
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    // IDAT chunk
    out[pos]=(uint8_t)(idat_len>>24); out[pos+1]=(uint8_t)(idat_len>>16);
    out[pos+2]=(uint8_t)(idat_len>>8); out[pos+3]=(uint8_t)idat_len; pos += 4;
    memcpy(out + pos, "IDAT", 4);
    memcpy(out + pos + 4, idat_data, idat_len);
    crc = simple_crc32(out + pos, 4 + idat_len);
    pos += 4 + idat_len;
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    // IEND chunk
    out[pos]=0; out[pos+1]=0; out[pos+2]=0; out[pos+3]=0; pos += 4;
    memcpy(out + pos, "IEND", 4); pos += 4;
    crc = simple_crc32(out + pos - 4, 4);
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    return pos;
}

int load_apng_frames_to_rgb565(const char *path,
                                uint16_t **frames, int max_frames,
                                int *out_n_frames, int *out_delay_ms,
                                int width, int height) {
    *out_n_frames = 0;
    *out_delay_ms = 250;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0 || fsz > (4 * 1024 * 1024)) { fclose(f); return 0; }

    uint8_t *raw = (uint8_t *)malloc((size_t)fsz);
    if (!raw) { fclose(f); return 0; }
    if ((long)fread(raw, 1, (size_t)fsz, f) != fsz) { free(raw); fclose(f); return 0; }
    fclose(f);

    // Collect per-frame compressed data pointers: frame 0 = IDAT, rest = fdAT
    uint8_t  ihdr13[13] = {0};
    const uint8_t *frame_data[8]; uint32_t frame_len[8];
    int n_found = 0;
    bool delay_read = false;

    size_t pos = 8;
    while (pos + 12 <= (size_t)fsz && n_found < max_frames) {
        uint32_t len  = rd32be(raw + pos);
        const uint8_t *type = raw + pos + 4;
        const uint8_t *data = raw + pos + 8;
        if (pos + 12 + len > (size_t)fsz) break;

        if (memcmp(type, "IHDR", 4) == 0 && len == 13) {
            memcpy(ihdr13, data, 13);
        } else if (memcmp(type, "fcTL", 4) == 0 && len >= 24 && !delay_read) {
            // fcTL data layout: seq(4) width(4) height(4) x_off(4) y_off(4)
            //                   delay_num(2) delay_den(2) dispose(1) blend(1)
            uint16_t num = (uint16_t)((data[20] << 8) | data[21]);
            uint16_t den = (uint16_t)((data[22] << 8) | data[23]);
            if (den == 0) den = 100;
            *out_delay_ms = (int)((uint32_t)num * 1000 / den);
            if (*out_delay_ms < 10) *out_delay_ms = 10;
            delay_read = true;
        } else if (memcmp(type, "IDAT", 4) == 0 && n_found == 0) {
            frame_data[n_found] = data;
            frame_len[n_found]  = len;
            n_found++;
        } else if (memcmp(type, "fdAT", 4) == 0 && len > 4) {
            frame_data[n_found] = data + 4;  // skip 4-byte seq number
            frame_len[n_found]  = len - 4;
            n_found++;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    // Decode each frame
    int loaded = 0;
    for (int i = 0; i < n_found; i++) {
        uint8_t *tmp = (uint8_t *)malloc(frame_len[i] + 300);
        if (!tmp) break;
        size_t sz = build_single_png(tmp, frame_len[i] + 300,
                                     ihdr13, frame_data[i], frame_len[i]);
        if (sz > 0) {
            int w, h, ch;
            uint8_t *pixels = stbi_load_from_memory(tmp, (int)sz, &w, &h, &ch, 3);
            if (pixels) {
                pixels_to_rgb565(pixels, w, h, frames[loaded], width, height);
                stbi_image_free(pixels);
                loaded++;
            }
        }
        free(tmp);
    }

    free(raw);
    *out_n_frames = loaded;
    return (loaded > 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// load_gif_frames_to_rgb565 — minimal GIF decoder for gallery thumbnails
// ---------------------------------------------------------------------------
// Bypasses stbi's GIF decoder which allocates ~2.7MB intermediate buffers
// (out + background + history at 4 bytes/pixel) that OOM/crash on 3DS.
// Instead we parse the GIF structure directly: read the global palette,
// LZW-decode each frame to palette indices in a static buffer, then map
// palette entries → RGB565 with nearest-neighbor scaling into the output.
// Peak allocation: only the static index buffer (VGA_WIDTH*VGA_HEIGHT bytes).

static uint8_t s_gif_indices[VGA_WIDTH * VGA_HEIGHT];

// Minimal LZW decoder for GIF — reads sub-blocks from a FILE stream.
typedef struct {
    FILE *fp;
    uint8_t block[256];
    int     block_len;
    int     block_pos;
    unsigned long accum;
    int           a_bits;
} GIFReader;

static int gif_read_byte(GIFReader *r) {
    if (r->block_pos >= r->block_len) {
        int sz = fgetc(r->fp);
        if (sz <= 0) return -1;
        r->block_len = sz;
        r->block_pos = 0;
        if ((int)fread(r->block, 1, sz, r->fp) != sz) return -1;
    }
    return r->block[r->block_pos++];
}

static int gif_read_code(GIFReader *r, int n_bits) {
    while (r->a_bits < n_bits) {
        int b = gif_read_byte(r);
        if (b < 0) return -1;
        r->accum |= (unsigned long)b << r->a_bits;
        r->a_bits += 8;
    }
    int code = (int)(r->accum & ((1 << n_bits) - 1));
    r->accum >>= n_bits;
    r->a_bits -= n_bits;
    return code;
}

// Skip GIF sub-blocks (read block sizes + data until a zero-length block).
static void gif_skip_blocks(FILE *fp) {
    int sz;
    while ((sz = fgetc(fp)) > 0) fseek(fp, sz, SEEK_CUR);
}

// Decode one LZW image from current file position into s_gif_indices.
// Returns number of pixels decoded.
#define GIF_LZW_MAX 4096
static uint16_t s_lzw_prefix[GIF_LZW_MAX];
static uint8_t  s_lzw_suffix[GIF_LZW_MAX];
static uint8_t  s_lzw_stack[GIF_LZW_MAX];

static int gif_lzw_decode(FILE *fp, int n_pixels) {
    int min_code_size = fgetc(fp);
    if (min_code_size < 2 || min_code_size > 8) return 0;

    int clear_code = 1 << min_code_size;
    int eoi_code   = clear_code + 1;

    GIFReader reader = { fp, {0}, 0, 0, 0, 0 };

    int n_bits    = min_code_size + 1;
    int max_code  = 1 << n_bits;
    int free_entry = eoi_code + 1;

    // Initialise table with single-char entries
    for (int i = 0; i < clear_code; i++) {
        s_lzw_prefix[i] = 0;
        s_lzw_suffix[i] = (uint8_t)i;
    }

    int out_pos = 0;
    int old_code = -1;
    int first_char = 0;

    while (out_pos < n_pixels) {
        int code = gif_read_code(&reader, n_bits);
        if (code < 0 || code == eoi_code) break;

        if (code == clear_code) {
            n_bits     = min_code_size + 1;
            max_code   = 1 << n_bits;
            free_entry = eoi_code + 1;
            old_code   = -1;
            continue;
        }

        int in_code = code;
        int sp = 0;

        if (code >= free_entry) {
            if (old_code < 0) break;
            s_lzw_stack[sp++] = (uint8_t)first_char;
            code = old_code;
        }

        while (code >= clear_code) {
            if (sp >= GIF_LZW_MAX) goto done;
            s_lzw_stack[sp++] = s_lzw_suffix[code];
            code = s_lzw_prefix[code];
        }
        s_lzw_stack[sp++] = s_lzw_suffix[code];
        first_char = s_lzw_suffix[code];

        // Output in reverse order
        for (int i = sp - 1; i >= 0 && out_pos < n_pixels; i--)
            s_gif_indices[out_pos++] = s_lzw_stack[i];

        // Add new entry to table
        if (old_code >= 0 && free_entry < GIF_LZW_MAX) {
            s_lzw_prefix[free_entry] = (uint16_t)old_code;
            s_lzw_suffix[free_entry] = (uint8_t)first_char;
            free_entry++;
            if (free_entry >= max_code && n_bits < 12) {
                n_bits++;
                max_code = 1 << n_bits;
            }
        }
        old_code = in_code;
    }
done:
    // Skip remaining sub-blocks
    gif_skip_blocks(fp);
    return out_pos;
}

int load_gif_frames_to_rgb565(const char *path,
                               uint16_t **frames, int max_frames,
                               int *out_n_frames, int *out_delay_ms,
                               int width, int height) {
    *out_n_frames = 0;
    *out_delay_ms = 250;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    // GIF header (6 bytes) + Logical Screen Descriptor (7 bytes)
    uint8_t hdr[13];
    if (fread(hdr, 1, 13, fp) != 13) { fclose(fp); return 0; }
    if (hdr[0] != 'G' || hdr[1] != 'I' || hdr[2] != 'F') { fclose(fp); return 0; }

    int gw = hdr[6] | (hdr[7] << 8);
    int gh = hdr[8] | (hdr[9] << 8);
    int packed = hdr[10];
    int has_gct = (packed >> 7) & 1;
    int gct_size = has_gct ? (1 << ((packed & 7) + 1)) : 0;

    if (gw <= 0 || gh <= 0 || gw > VGA_WIDTH || gh > VGA_HEIGHT) { fclose(fp); return 0; }

    // Read global colour table
    static uint8_t palette[256][3];
    memset(palette, 0, sizeof(palette));
    if (gct_size > 0) {
        if ((int)fread(palette, 3, gct_size, fp) != gct_size) { fclose(fp); return 0; }
    }

    // Pre-compute palette → RGB565 LUT
    static uint16_t pal565[256];
    for (int i = 0; i < 256; i++) {
        pal565[i] = ((uint16_t)(palette[i][0] >> 3) << 11)
                   | ((uint16_t)(palette[i][1] >> 2) <<  5)
                   |  (uint16_t)(palette[i][2] >> 3);
    }

    int n_pixels = gw * gh;
    int loaded = 0;

    // Walk blocks until we've decoded enough frames or hit the trailer
    while (loaded < max_frames) {
        int b = fgetc(fp);
        if (b < 0 || b == 0x3B) break; // EOF or trailer

        if (b == 0x21) { // Extension
            int label = fgetc(fp);
            if (label == 0xF9) { // Graphic Control Extension
                int sz = fgetc(fp);
                if (sz >= 4) {
                    uint8_t gce[4];
                    if (fread(gce, 1, 4, fp) != 4) break;
                    uint16_t cs = gce[1] | (gce[2] << 8);
                    int ms = cs * 10;
                    if (loaded == 0) *out_delay_ms = ms < 10 ? 10 : ms;
                    fgetc(fp); // block terminator
                } else {
                    gif_skip_blocks(fp);
                }
            } else {
                gif_skip_blocks(fp);
            }
            continue;
        }

        if (b == 0x2C) { // Image Descriptor
            uint8_t desc[9];
            if (fread(desc, 1, 9, fp) != 9) break;
            int iw = desc[4] | (desc[5] << 8);
            int ih = desc[6] | (desc[7] << 8);
            int ipacked = desc[8];
            int has_lct = (ipacked >> 7) & 1;
            int lct_size = has_lct ? (1 << ((ipacked & 7) + 1)) : 0;

            // Skip local colour table if present (our encoder never uses one)
            if (lct_size > 0) fseek(fp, lct_size * 3, SEEK_CUR);

            if (iw != gw || ih != gh || iw * ih > VGA_WIDTH * VGA_HEIGHT) {
                // Unexpected frame size — skip LZW data
                fgetc(fp); // min code size
                gif_skip_blocks(fp);
                continue;
            }

            int decoded = gif_lzw_decode(fp, n_pixels);
            if (decoded < n_pixels) {
                // Partial decode — zero-fill the rest
                for (int i = decoded; i < n_pixels; i++)
                    s_gif_indices[i] = 0;
            }

            // Map indices → RGB565 with nearest-neighbor scaling
            for (int y = 0; y < height; y++) {
                int sy = y * gh / height;
                for (int x = 0; x < width; x++) {
                    int sx = x * gw / width;
                    frames[loaded][y * width + x] = pal565[s_gif_indices[sy * gw + sx]];
                }
            }
            loaded++;
            continue;
        }

        // Unknown block — skip
        gif_skip_blocks(fp);
    }

    fclose(fp);
    *out_n_frames = loaded;
    return (loaded > 0) ? 1 : 0;
}

// Static output buffer for JPEG encoding.
// 1 MB covers 4x upscaled output (1600x960 at quality 90 is ~600-800 KB).
// No runtime allocation during save; safe because saves are serialized via busy flag.
#define JPEG_BUF_CAP (1024 * 1024)
static uint8_t s_jpeg_buf[JPEG_BUF_CAP];
static int     s_jpeg_len;

static void jpeg_accum(void *ctx, void *data, int size) {
    (void)ctx;
    if (s_jpeg_len + size > JPEG_BUF_CAP) return;
    memcpy(s_jpeg_buf + s_jpeg_len, data, size);
    s_jpeg_len += size;
}

// Encode to a static memory buffer, then write the whole file in one fwrite.
// Avoids thousands of tiny SD-card writes from stbi's 64-byte internal buffer.
int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height) {
    s_jpeg_len = 0;
    stbi_write_jpg_to_func(jpeg_accum, NULL, width, height, 3, rgb888, 90);
    if (s_jpeg_len <= 0) return 0;

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = (fwrite(s_jpeg_buf, 1, s_jpeg_len, f) == (size_t)s_jpeg_len);
    fclose(f);
    return ok;
}

// Single shared counter for all saved files (GB_ and GW_).
// Seeded by file_counter_init(); incremented on each save.
static int s_next_n = -1;  // -1 = not yet initialised

// Call once at startup. ini_val is the persisted next_file_n from settings
// (pass 0 if not present). We take max(ini_val, dir_scan+1) so we never
// reuse a number even if the INI was deleted or is stale.
void file_counter_init(const char *dir, int ini_val) {
    mkdir("sdmc:/DCIM", 0777);
    mkdir(dir, 0777);

    int max_n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            int n = 0;
            if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1 && n > max_n) max_n = n;
            if (sscanf(e->d_name, "GW_%d.gif", &n) == 1 && n > max_n) max_n = n;
            if (sscanf(e->d_name, "GW_%d.png", &n) == 1 && n > max_n) max_n = n;
        }
        closedir(d);
    }
    int from_scan = max_n + 1;
    s_next_n = (ini_val > from_scan) ? ini_val : from_scan;
}

// Returns the current value of the shared counter (the next number that will
// be used). Call after file_counter_init to persist it in settings.
int file_counter_next(void) { return s_next_n; }

int next_save_path(const char *dir, char *out_path, int out_len) {
    if (s_next_n < 0) file_counter_init(dir, 0);
    if (s_next_n > 9999) return 0;
    snprintf(out_path, out_len, "%s/GB_%04d.JPG", dir, s_next_n);
    s_next_n++;
    return 1;
}

// Scan dir for GB_XXXX.JPG and GW_XXXX.GIF files, fill paths[] sorted descending by number.
// Both file types share the same 4-digit counter space so they interleave naturally.
int list_saved_photos(const char *dir, char paths[][64], int max) {
    // Pack type (0=JPG, 1=GIF) in high bit of a 32-bit slot alongside the number.
    // Sorting by value descending keeps them interleaved correctly.
    int nums[256];
    int types[256];  // 0=JPG, 1=GIF
    int count = 0;

    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        int n = 0;
        if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1) {
            nums[count]  = n;
            types[count] = 0;
            count++;
        } else if (sscanf(e->d_name, "GW_%d.gif", &n) == 1) {
            nums[count]  = n;
            types[count] = 1;
            count++;
        } else if (sscanf(e->d_name, "GW_%d.png", &n) == 1) {
            nums[count]  = n;
            types[count] = 2;
            count++;
        }
    }
    closedir(d);

    // Insertion sort descending by number
    for (int i = 1; i < count; i++) {
        int kn = nums[i], kt = types[i], j = i - 1;
        while (j >= 0 && nums[j] < kn) {
            nums[j+1]  = nums[j];
            types[j+1] = types[j];
            j--;
        }
        nums[j+1]  = kn;
        types[j+1] = kt;
    }

    for (int i = 0; i < count; i++) {
        if (types[i] == 0)
            snprintf(paths[i], 64, "%s/GB_%04d.JPG", dir, nums[i]);
        else if (types[i] == 1)
            snprintf(paths[i], 64, "%s/GW_%04d.gif", dir, nums[i]);
        else
            snprintf(paths[i], 64, "%s/GW_%04d.png", dir, nums[i]);
    }

    return count;
}

int next_wiggle_path(const char *dir, char *out_path, int out_len) {
    if (s_next_n < 0) file_counter_init(dir, 0);
    if (s_next_n > 9999) return 0;
    snprintf(out_path, out_len, "%s/GW_%04d.gif", dir, s_next_n);
    s_next_n++;
    return 1;
}

// ---------------------------------------------------------------------------
// Save an edited wiggle APNG — composite stickers/frame onto each animation
// frame then encode as APNG, preserving the original frame count and timing.
// ---------------------------------------------------------------------------

#define APNG_BUF_CAP (4 * 1024 * 1024)
static uint8_t s_apng_buf[APNG_BUF_CAP];

int save_edited_apng(const char *path,
                     const uint16_t * const *frames_rgb565,
                     int n_frames, int delay_ms,
                     int w, int h,
                     composite_fn_t composite_fn, void *userdata)
{
    if (n_frames < 1 || n_frames > 8) return 0;
    int npix = w * h;

    uint8_t *rgb_bufs[8] = {0};
    const uint8_t *frame_ptrs[8];

    for (int f = 0; f < n_frames; f++) {
        rgb_bufs[f] = malloc(npix * 3);
        if (!rgb_bufs[f]) goto fail;
        rgb565_to_rgb888(rgb_bufs[f], frames_rgb565[f], npix);
        if (composite_fn)
            composite_fn(rgb_bufs[f], w, h, userdata);
        frame_ptrs[f] = rgb_bufs[f];
    }

    uint16_t delay_num = (uint16_t)delay_ms;
    uint16_t delay_den = 1000;
    size_t apng_len = apng_encode(s_apng_buf, APNG_BUF_CAP,
                                  frame_ptrs, n_frames,
                                  w, h, delay_num, delay_den);

    for (int f = 0; f < n_frames; f++) free(rgb_bufs[f]);

    if (apng_len == 0) return 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_apng_buf, 1, apng_len, fp) == apng_len);
    fclose(fp);
    return ok;

fail:
    for (int f = 0; f < n_frames; f++) free(rgb_bufs[f]);
    return 0;
}
