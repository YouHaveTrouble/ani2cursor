#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void process_file(const char *name);

static int contains_icon(const char *buffer, int start);

static void write_cursor_conf(
    const char *baseName,
    unsigned long default_jif,
    int num_steps,
    const int *rate_values,
    int rate_count,
    const int *seq_values,
    int seq_count
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
    unsigned long default_jif = 0;  // jifRate from anih (1 jiffy = 1/60 sec)
    int num_steps = 0;              // cSteps from anih
    int num_frames = 0;             // cFrames from anih
    int rate_values[10000];         // per-step rates from "rate" chunk (jiffies)
    int rate_count = 0;             // how many entries we found in "rate"
    int seq_values[10000];
    int seq_count = 0;

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
            //   offset 28: jifRate   <-- what we want (1 jiffy = 1/60 sec)
            const unsigned char *p = (const unsigned char *)(buffer + i + 8);
            num_frames = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
            num_steps  = p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24);
            default_jif = p[28] | (p[29] << 8) | (p[30] << 16) | (p[31] << 24);
        }

        // per step timing extraction
        if (i + 12 <= fileLen
    && memcmp(buffer + i, "seq ", 4) == 0   // note trailing space
    && seq_count == 0) {
            const unsigned char *p = (const unsigned char *)(buffer + i + 8);
            const unsigned long seq_size = (unsigned char)buffer[i+4]
                                   | ((unsigned char)buffer[i+5] << 8)
                                   | ((unsigned char)buffer[i+6] << 16)
                                   | ((unsigned char)buffer[i+7] << 24);
            int num_seqs = seq_size / 4;
            if (num_seqs > 10000) num_seqs = 10000;
            for (int s = 0; s < num_seqs; s++) {
                seq_values[s] = p[s*4] | (p[s*4+1] << 8)
                              | (p[s*4+2] << 16) | (p[s*4+3] << 24);
            }
            seq_count = num_seqs;
    }

        // icon extraction
        if (i + 4 <= fileLen && contains_icon(buffer, i) == 1) {
            sprintf(png_counter_string, "%d", png_counter);
            strcpy(new_png_name, fileName);
            strcat(new_png_name, png_counter_string);
            strcat(new_png_name, ".png");
            png_counter++;

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
                if (contains_icon(buffer, i + j + 1) == 1)
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
            i += j; // advance the outer loop so it doesn't rescan the same bytes
        }
    }
    printf("DEBUG: frames=%d steps=%d seq_count=%d rate_count=%d\n",
       num_frames, num_steps, seq_count, rate_count);

    if (num_steps > 0) {
        write_cursor_conf(fileName, default_jif, num_steps, rate_values, rate_count, seq_values, seq_count);
    }

    printf("\n=== Animation Timing ===\n");
    printf("Frames: %d\n", num_frames);
    printf("Steps:  %d\n", num_steps);
    printf("Default rate: %lu jiffies (%.1f ms)\n",
           default_jif, default_jif * 1000.0 / 60.0);
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

    free(new_png_name);
    free(buffer);
    free(fileName);
}


/**
 * This always needs <code>i + 4 <= fileLen</code> so it doesn't read random bytes
 * @param buffer buffer to read from
 * @param start position to start from
 * @return true if found "icon"
 */
int contains_icon(const char *buffer, const int start) {
    return memcmp(buffer + start, "icon", 4) == 0;
}


static void write_cursor_conf(
    const char *baseName,
    const unsigned long default_jif,
    const int num_steps,
    const int *rate_values,
    const int rate_count,
    const int *seq_values,
    const int seq_count
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
        fprintf(conf, "32\t0\t0\t%s\t%d\n", pngName, ms);
    }

    fclose(conf);
    printf("Cursor config written to %s\n", confName);

    free(confName);

}