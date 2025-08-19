#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

typedef struct{
    double offset;
    double duration;
} Slice;

typedef struct{
    Slice* items;
    size_t count;
    size_t capacity;
} Slices;

static char* str_dup_range(const char* start, size_t len) {
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/**
 * Parse slices text buffer.
 * - buf/size: input text buffer
 * - out_slices: dynamic array to fill
 * - out_media: pointer to char* (caller frees)
 * - out_output: pointer to char* (caller frees)
 */
int parse_slices(const char* buf, size_t size,
                 Slices* out_slices,
                 char** out_media,
                 char** out_output)
{
    *out_media = NULL;
    *out_output = NULL;

    const char* end = buf + size;
    const char* line = buf;

    while (line < end) {
        const char* next = memchr(line, '\n', (size_t)(end - line));
        size_t line_len = next ? (size_t)(next - line) : (size_t)(end - line);

        if (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
        }

        const char* p = line;
        while (p < line + line_len && isspace((unsigned char)*p)) p++;

        if (p < line + line_len) {
            if (strncmp(p, "output:", 7) == 0) {
                p += 7;
                while (p < line + line_len && isspace((unsigned char)*p)) p++;
                *out_output = str_dup_range(p, (size_t)(line + line_len - p));
            } else if (strncmp(p, "media:", 6) == 0) {
                p += 6;
                while (p < line + line_len && isspace((unsigned char)*p)) p++;
                *out_media = str_dup_range(p, (size_t)(line + line_len - p));
            } else if (isdigit((unsigned char)*p)) {
                int h = 0, m = 0, s = 0, ms = 0;
                double duration = 0.0;

                int parsed = sscanf(p, "%d:%d:%d:%d %lf", &h, &m, &s, &ms, &duration);
                if (parsed == 5) {
                    double offset = (double)h * 3600.0 +
                                    (double)m * 60.0 +
                                    (double)s +
                                    (double)ms / 1000.0;

                    Slice slice = { offset, duration };
                    da_append(out_slices, slice);
                } else {
                    fprintf(stderr, "Warning: could not parse line: %.*s\n",
                            (int)line_len, line);
                }
            }
        }

        line = next ? next + 1 : end;
    }

    return 0;
}


void remove_carriage_return_from_str(char* data){
    char* current_pos = data;
    while ((current_pos = strchr(current_pos, '\r'))) {
        memmove(current_pos, current_pos + 1, strlen(current_pos + 1) + 1);
    }
}

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "Provide filename!\n");
        return 1;
    }

    char* filename = NULL;
    char* outputFilename = "output.mp4";
    Slices slices = {0};

    String_Builder sb = {0};
    if(!read_entire_file(argv[1], &sb)){
        fprintf(stderr, "Couldn't open project file!\n");
        return 1;
    }

    remove_carriage_return_from_str(sb.items);

    if(parse_slices(sb.items, sb.count, &slices, &filename, &outputFilename) != 0) return 1;

    printf("%s:%s\n", filename, outputFilename);

    printf("Slices %zu:\n", slices.count);
    for(size_t i = 0; i < slices.count; i++){
        Slice* slice = &slices.items[i];
        printf("%lf:%lf\n", slice->offset, slice->duration);
    }
    
    printf("Hjello Freunder!\n");

    Media media = {0};
    if(!ffmpegMediaInit(filename, &media)){
        fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", filename);
        return 1;
    }
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(&media, outputFilename, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    double duration = ffmpegMediaDuration(&media);
    size_t currentSlice = 0;

    double checkDuration = slices.items[currentSlice].duration;
    if(checkDuration == -1) checkDuration = duration - slices.items[currentSlice].offset;

    double localTime = 0;
    double timeBase = 0;
    Frame frame = {0};

    ffmpegMediaSeek(&media, &frame, slices.items[currentSlice].offset);

    while(true){
        if(localTime >= checkDuration){
            currentSlice++;
            if(currentSlice >= slices.count) break;
            localTime = 0;
            timeBase+=checkDuration;
            checkDuration = slices.items[currentSlice].duration;
            if(checkDuration == -1) checkDuration = duration - slices.items[currentSlice].offset;
            ffmpegMediaSeek(&media, &frame, slices.items[currentSlice].offset);
        }

        if(!ffmpegMediaGetFrame(&media, &frame)) break;
        localTime = frame.frameTime - slices.items[currentSlice].offset;
        frame.frameTime = timeBase + localTime;
        ffmpegMediaRenderPassFrame(&renderContext, &frame);
    }

    ffmpegMediaRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}