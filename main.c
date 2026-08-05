#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void process_file(const char *name);

static int contains_icon(const char *buffer, int start);

int main(const int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ani2cursor /path.ani");
        return 0;
    }
    process_file(argv[1]);
    return 0;
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

void process_file(const char *name) {
    if (!strstr(name, ".ani")) {
        fprintf(stderr, "Usage: ani2cursor /path.ani");
        return;
    }

    // Copy the file name, so the extension can be dropped
    char *fileName = malloc(strlen(name) + 1);
    strcpy(fileName, name);
    *strstr(fileName, ".ani") = '\0';

    FILE *file = fopen(name, "rb");

    fseek(file, 0, SEEK_END);
    const unsigned long fileLen = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(fileLen + 1);

    fread(buffer, fileLen, 1, file);
    fclose(file);

    char *new_png_name = malloc(strlen(name) + 5);

    char png_counter_string[5];
    int png_counter = 1;

    for (int i = 0; i <= fileLen; i++) {
        if (png_counter == 9999) {
            free(fileName);
            free(buffer);
            free(new_png_name);
            return;
        }

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

            int j = 8;
            while (i + j + 4 <= fileLen) {
                if (contains_icon(buffer, i + j + 1) == 1)
                    break;
                if (j == 10)
                    putc(0x01, png_image);
                else
                    putc(*(buffer + i + j), png_image);
                j++;
            }
            if (i + j <= fileLen)
                putc(*(buffer + i + j), png_image);
            if (fileLen - i - j <= 3) {
                putc(*(buffer + i + j + 1), png_image);
                putc(*(buffer + i + j + 2), png_image);
            }
            fclose(png_image);
            i += j;
        }
    }

    free(new_png_name);
    free(buffer);
    free(fileName);
}
