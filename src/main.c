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

bool parseArgument(String_View* name, String_View* value, char* file_content, char* end_file_content, char** out_file_content){
    name->data = file_content;
    name->count = 0;
    while(file_content[0] != ':' && file_content < end_file_content) {
        name->count++;
        file_content++;
    }

    if(file_content >= end_file_content){
        fprintf(stderr, "expected : after parameter name\n");
        return false;
    }

    file_content++;
    value->data = file_content;
    value->count = 0;
    while(file_content[0] != '\n' && file_content < end_file_content) {
        value->count++;
        file_content++;
    }

    if(file_content >= end_file_content){
        fprintf(stderr, "expected new line after parameter value\n");
        return false;
    }
    file_content++;

    *name = sv_trim_left(*name);
    *name = sv_trim(*name);
    *value = sv_trim(*value);
    *value = sv_trim_left(*value);

    *out_file_content = file_content;
    return true;
}

bool parseHeader(char** outputFilename, char** filename, char* file_content, char* end_file_content, char** out_file_content){
    if(end_file_content - file_content < 3) {
        fprintf(stderr, "too small file!\n");
        return false;
    }

    if(!(file_content[0] == '/' && file_content[1] == '*')) {
        fprintf(stderr, "header needs to start with /* and end with */\n");
        return false;
    }

    file_content += 2;

    while(file_content[0] == '\n' && end_file_content - file_content > 0) file_content++;

    String_View parameter_name = {0};
    String_View parameter_value = {0};

    while(file_content+1 < end_file_content && file_content[0] != '*' && end_file_content[1] != '/'){
        if(!parseArgument(&parameter_name, &parameter_value, file_content, end_file_content, &file_content)) return false;
        assert(parameter_name.count > 0);
        assert(parameter_value.count > 0);

        if(sv_eq(parameter_name, sv_from_cstr("output"))){
            *outputFilename = calloc(parameter_value.count+1,sizeof(char));
            memcpy(*outputFilename, parameter_value.data, parameter_value.count);
            continue;
        }
        if(sv_eq(parameter_name, sv_from_cstr("media"))){
            *filename = calloc(parameter_value.count+1,sizeof(char));
            memcpy(*filename, parameter_value.data, parameter_value.count);
            continue;
        }
    }

    file_content += 2;

    *out_file_content = file_content;
    return true;
}

bool parseSlice(Slice* slice, char* file_content, char* end_file_content, char** out_file_content){
    size_t hour;
    size_t minute;
    size_t second;
    size_t milisecond;

    double duration;

    char* slice_ptr = file_content;
    while(end_file_content - file_content > 0 && file_content[0] != '\n') file_content++;
    size_t slice_size = file_content - slice_ptr;

    char* buff = calloc(slice_size+1, sizeof(char));
    memcpy(buff, slice_ptr, slice_size);

    if(sscanf(buff, "%zu:%zu:%zu:%zu %lf", &hour, &minute, &second, &milisecond, &duration) != 5){
        printf("failed to parse slice!\n");
        free(buff);
        return false;
    }

    free(buff);

    slice->duration = duration;
    slice->offset = 0;
    slice->offset += hour*60*60;
    slice->offset += minute*60;
    slice->offset += second;
    slice->offset += (double)milisecond / 1000;

    *out_file_content = file_content;
    return true;
}

bool parseProjectFile(char** outputFilename, char** filename, Slices* slices, char* file_content, size_t file_size){
    char* end_file_content = file_content + file_size;
    if(!parseHeader(outputFilename, filename, file_content, end_file_content, &file_content)) return false;
    Slice slice = {0};
    while(end_file_content - file_content > 0){
        while(file_content[0] == '\n'  && end_file_content - file_content > 0) file_content++;

        if(!parseSlice(&slice, file_content, end_file_content, &file_content)) return false;
        da_append(slices, slice);
    }

    return true;
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

    if(!parseProjectFile(&outputFilename, &filename, &slices, sb.items, strlen(sb.items))) return 1;

    printf("%s:%s\n", filename, outputFilename);

    printf("Slices %zu:\n", slices.count);
    for(size_t i = 0; i < slices.count; i++){
        Slice* slice = &slices.items[i];
        printf("%lf:%lf\n", slice->offset, slice->duration);
    }
    
    printf("Hjello Freunder!\n");

    Media media = {0};
    if(!ffmpegMediaInit(filename, &media)){
        fprintf(stderr, "Couldn't initialize ffmpeg media!\n");
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