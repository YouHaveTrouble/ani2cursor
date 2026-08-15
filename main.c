#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int x;
    int y;
} point;

static void process_file(const char *name);

static void write_cursor_conf(
    const char *baseName,
    unsigned long default_jif,
    int num_steps,
    const int *rate_values,
    int rate_count,
    const int *seq_values,
    int seq_count,
    point cursor_size,
    point hotspot
);

int main(const int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ani2cursor /path.ani");
        return 0;
    }
    process_file(argv[1]);
    return 0;
}

void process_file(const char *name) {
    if (!strstr(name, ".ani")) {
        fprintf(stderr, "Usage: ani2cursor /path.ani");
        return;
    }

    // Copy the file name, so the extension can be dropped
    char *fileName = malloc(strlen(name) + 1);
    strcpy(fileName, name);
    *strstr(fileName, ".ani") = '\0';
    char *cursor_conf_name = malloc(strlen(fileName) + 10);

    FILE *file = fopen(name, "rb");

    if (file == NULL) {
        fprintf(stderr, "Unable to open file %s.\n", name);
        return;
    }

    // jump to the end of file - that gets file size in bytes
    fseek(file, 0, SEEK_END);
    const unsigned long fileLen = ftell(file);

    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(fileLen + 1);

    fread(buffer, fileLen, 1, file); // reads entire file into memory
    fclose(file);

    char *new_png_name = malloc(strlen(name) + 9); // reserve space for filename + numbered counter

    char png_counter_string[5];
    int png_counter = 1;
    unsigned long default_jif = 0; // jifRate from anih (1 jiffy = 1/60 sec)
    int num_steps = 0; // cSteps from anih
    int num_frames = 0; // cFrames from anih
    int *rate_values = NULL;
    int rate_count = 0; // how many entries we found in "rate"
    int *seq_values = NULL;
    int seq_count = 0;
    point cursor_size = {.x = 32, .y = 32};
    point hotspot = {.x = 0, .y = 0};

    for (int i = 0; i <= fileLen; i++) {
        if (png_counter == 9999) {
            free(fileName);
            free(buffer);
            free(new_png_name);
            return;
        }

        // timing extraction
        if (i + 44 <= fileLen && memcmp(buffer + i, "anih", 4) == 0 && num_frames == 0) {
            // chunk layout: "anih" (4) + size (4) + 36 bytes of ANIHEADER
            // ANIHEADER (little-endian DWORDs):
            //   offset 0:  cbSizeof  (should be 36)
            //   offset 4:  cFrames
            //   offset 8:  cSteps
            //   offset 28: jifRate
            const unsigned char *p = (const unsigned char *) (buffer + i + 8);
            num_frames = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
            num_steps = p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24);
            default_jif = p[28] | (p[29] << 8) | (p[30] << 16) | (p[31] << 24);
        }

        // sequence parsing
        if (i + 12 <= fileLen && memcmp(buffer + i, "seq ", 4) == 0 && seq_count == 0) {
            const unsigned char *p = (const unsigned char *) (buffer + i + 8);
            const unsigned long seq_size = (unsigned char) buffer[i + 4]
                                           | ((unsigned char) buffer[i + 5] << 8)
                                           | ((unsigned char) buffer[i + 6] << 16)
                                           | ((unsigned char) buffer[i + 7] << 24);
            const int num_seqs = (int) seq_size / 4;
            if (num_seqs > 0) {
                seq_values = malloc((size_t) num_seqs * sizeof(int));
                if (!seq_values) {
                    fprintf(stderr, "OOM allocating seq\n");
                    free(fileName);
                    free(buffer);
                    free(new_png_name);
                    free(cursor_conf_name);
                    free(rate_values);
                    return;
                }
                for (int s = 0; s < num_seqs; s++) {
                    seq_values[s] = p[s * 4] | (p[s * 4 + 1] << 8)
                                    | (p[s * 4 + 2] << 16) | (p[s * 4 + 3] << 24);
                }
                seq_count = num_seqs;
            }
        }

        // per step timing extraction
        if (i + 12 <= fileLen && memcmp(buffer + i, "rate", 4) == 0 && rate_count == 0) {
            const unsigned char *p = (const unsigned char *) (buffer + i + 8);
            const unsigned long rate_size = (unsigned char) buffer[i + 4]
                                            | ((unsigned char) buffer[i + 5] << 8)
                                            | ((unsigned char) buffer[i + 6] << 16)
                                            | ((unsigned char) buffer[i + 7] << 24);
            const int num_rates = (int) rate_size / 4;
            if (num_rates > 0) {
                rate_values = malloc((size_t) num_rates * sizeof(int));
                if (!rate_values) {
                    fprintf(stderr, "OOM allocating rate\n");
                    free(fileName);
                    free(buffer);
                    free(new_png_name);
                    free(cursor_conf_name);
                    free(seq_values);
                    free(rate_values);
                    return;
                }
                for (int r = 0; r < num_rates; r++) {
                    rate_values[r] = p[r * 4] | (p[r * 4 + 1] << 8)
                                     | (p[r * 4 + 2] << 16) | (p[r * 4 + 3] << 24);
                }
                rate_count = num_rates;
            }
        }

        // icon extraction
        if (i + 4 <= fileLen && memcmp(buffer + i, "icon", 4) == 0) {
            sprintf(png_counter_string, "%d", png_counter);
            strcpy(new_png_name, fileName);
            strcat(new_png_name, png_counter_string);
            strcat(new_png_name, ".png");
            png_counter++;

            //cursor size and hotspot extraction. realistically this will be static so parsing it once is enough, but
            //format itself DOES support per frame hotspot and cursor size
            if (png_counter == 2 && i + 22 <= fileLen) {
                const unsigned char *cur = (const unsigned char *)(buffer + i);
                const int w = cur[14];
                const int h = cur[15];
                cursor_size.x  = w != 0 ? w : 32;
                cursor_size.y = h != 0 ? h : 32;
                hotspot.x = cur[18] | (cur[19] << 8);
                hotspot.y = cur[20] | (cur[21] << 8);
            }

            FILE *png_image = fopen(new_png_name, "wb");
            if (!png_image) {
                fprintf(stderr, "Unable to open file %s.\n", new_png_name);
                free(fileName);
                free(buffer);
                free(new_png_name);
                return;
            }

            int j = 8; // bytes to skip ("icon" and 4 bytes of RIFF format header)

            // read until next "icon" is found
            while (i + j + 4 <= fileLen) {
                if (memcmp(buffer + i + j + 1, "icon", 4) == 0)
                    break;
                if (j == 10)
                    putc(0x01, png_image); // some weird format hack, to research later
                else
                    putc(buffer[i + j], png_image);
                j++;
            }
            if (i + j <= fileLen)
                putc(buffer[i + j], png_image);
            if (fileLen - i - j <= 3) {
                putc(buffer[i + j + 1], png_image);
                putc(buffer[i + j + 2], png_image);
            }
            fclose(png_image);
            i += j;
        }
    }

    if (num_steps > 0) {
        write_cursor_conf(fileName, default_jif, num_steps, rate_values, rate_count, seq_values, seq_count, cursor_size, hotspot);
    }

    printf("\n=== Animation Timing ===\n");
    printf("Frames: %d\n", num_frames);
    printf("Steps:  %d\n", num_steps);
    printf("Default rate: %lu jiffies (%.1f ms)\n",
           default_jif, (double) default_jif * 1000.0 / 60.0);
    if (rate_count > 0) {
        printf("Per-step rates (from 'rate' chunk):\n");
        for (int r = 0; r < rate_count; r++) {
            printf("  step %d: %d jiffies (%.1f ms)\n",
                   r, rate_values[r], rate_values[r] * 1000.0 / 60.0);
        }
    } else {
        printf("No 'rate' chunk found — all steps use the default.\n");
    }
    printf("========================\n\n");

    free(fileName);
    free(buffer);
    free(new_png_name);
    free(cursor_conf_name);
    free(seq_values);
    free(rate_values);
}


static void write_cursor_conf(
    const char *baseName,
    const unsigned long default_jif,
    const int num_steps,
    const int *rate_values,
    const int rate_count,
    const int *seq_values,
    const int seq_count,
    const point cursor_size,
    const point hotspot
) {
    char *confName = malloc(strlen(baseName) + 6);
    strcpy(confName, baseName);
    strcat(confName, ".conf");

    FILE *conf = fopen(confName, "w");
    if (!conf) {
        fprintf(stderr, "Unable to write cursor config %s.\n", confName);
    }

    for (int step = 0; step < num_steps; step++) {
        const int jif = rate_count > 0 && step < rate_count
                            ? rate_values[step]
                            : (int) default_jif;
        const int ms = jif * 1000 / 60;

        const int frame = seq_count > 0 && step < seq_count
                              ? seq_values[step]
                              : step;

        char pngName[1024];
        char numStr[5];
        sprintf(numStr, "%d", frame + 1);
        strcpy(pngName, baseName);
        strcat(pngName, numStr);
        strcat(pngName, ".png");

        // size and hotspot are placeholders
        fprintf(conf, "%d\t%d\t%d\t%d\t%s\t%d\n",
        cursor_size.x, cursor_size.y, hotspot.x, hotspot.y, pngName, ms);
    }

    fclose(conf);
    printf("Cursor config written to %s\n", confName);

    free(confName);
}
