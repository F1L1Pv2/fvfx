#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#undef ERROR
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

// Configuration
#define DEFAULT_EXAMPLE "bindless_textures"
#define OUTPUT_PROGRAM_NAME "main"
#define BUILD_PATH(debug) (debug ? "build/debug/" : "build/release/")
#define COMPILER_ARGS PLATFORM_COMPILER_ARGS "-I./", "-I./src"
#define LINKER_FLAGS PLATFORM_LINKER_FLAGS

#ifdef _WIN32
#define VULKAN_SDK_SEARCH_PATH "C:/VulkanSDK"
const char* vulkanSDKPathLIB;
const char* vulkanSDKPathINC;

#define PLATFORM_COMPILER_ARGS vulkanSDKPathINC, "-Wno-deprecated-declarations",
#define PLATFORM_LINKER_FLAGS vulkanSDKPathLIB, "-lvulkan-1", "-lkernel32", "-luser32", "-lgdi32", \
                              "-lshaderc_shared", "-lshaderc_util", "-lglslang", \
                              "-lSPIRV", "-lSPIRV-Tools", "-lSPIRV-Tools-opt", \
                              "-Wno-deprecated-declarations",
#else
#define PLATFORM_COMPILER_ARGS
#define PLATFORM_LINKER_FLAGS "-lvulkan", "-lX11", "-lXrandr", "-lshaderc", "-lc", "-lm"
#endif

static char* strltrim(char* s) {
    while (isspace(*s)) s++;
    return s;
}

static bool is_dir(const char* path) {
#ifdef _WIN32
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES && 
           (attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

static const char* nob_get_ext(const char* path) {
    const char* end = path;
    while (*end) end++;
    while (end >= path) {
        if (*end == '.') return end + 1;
        if (*end == '/' || *end == '\\') break;
        end--;
    }
    return path + strlen(path);
}

static void remove_directory(const char *path) {
#ifdef _WIN32
    WIN32_FIND_DATA find_data;
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);

    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", path, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            remove_directory(full_path);
        } else {
            DeleteFile(full_path);
        }
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);
    RemoveDirectory(path);
#else
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat statbuf;
        if (lstat(full_path, &statbuf) == -1) {
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            remove_directory(full_path);
        } else {
            unlink(full_path);
        }
    }
    closedir(dir);
    rmdir(path);
#endif
}

static void remove_backslashes(char* data) {
    char* backslash;
    while((backslash=strchr(data, '\\'))) {
        switch(backslash[1]) {
        case '\n':
            memmove(backslash, backslash+2, strlen(backslash+2)+1);
            break;
        default:
            memmove(backslash, backslash+1, strlen(backslash+1)+1);
        }
        data=backslash;
    }
}

static bool dep_analyse_str(char* data, char** result, Nob_File_Paths* paths) {
    char* result_end = strchr(data, ':');
    if(!result_end) return false;
    result_end[0] = '\0';
    *result = data;
    data = result_end+1;
    remove_backslashes(data);
    char* lineend;
    if((lineend=strchr(data, '\n')))
        lineend[0] = '\0';
    while((data=(char*)strltrim(data))[0]) {
        char* path=data;
        while(data[0] && data[0] != ' ') data++;
        nob_da_append(paths, path);
        if(data[0]) {
            data[0] = '\0';
            data++;
        }
    }
    return true;
}

static bool nob_c_needs_rebuild(Nob_String_Builder* string_buffer, Nob_File_Paths* paths, 
                               const char* output_path, const char* input_path) {
    string_buffer->count = 0;
    paths->count = 0;
    
    const char* ext = nob_get_ext(output_path);
    char d_file[1024];
    snprintf(d_file, sizeof(d_file), "%.*sd", (int)(ext - output_path), output_path);

    if (nob_needs_rebuild(d_file, &input_path, 1) != 0) {
        return true;
    }

    if (!nob_read_entire_file(d_file, string_buffer)) {
        return true;
    }
    nob_da_append(string_buffer, '\0');
    
    char* obj;
    char* data = string_buffer->items;

    char* current_pos = data;
    while ((current_pos = strchr(current_pos, '\r'))) {
        memmove(current_pos, current_pos + 1, strlen(current_pos + 1) + 1);
    }
    current_pos = data;
    while ((current_pos = strchr(current_pos, '\\'))) {
        if (current_pos[1] != '\r' && current_pos[1] != '\n') {
            *current_pos = '/';
        }
        current_pos++;
    }

    if(!dep_analyse_str(data, &obj, paths)) return true;
    
    return nob_needs_rebuild(output_path, (const char**)paths->items, paths->count) != 0;
}

static String_View get_dirname(const char* path) {
    String_View sv = sv_from_cstr(path);
    if (sv.count == 0) return sv;

    const char* end = path + sv.count - 1;
    while (end > path && *end != '/' && *end != '\\') end--;
    
    sv.count = (end > path) ? (size_t)(end - path) : 0;
    return sv;
}

static bool folder_exists(const char* path) {
#ifdef _WIN32
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES && 
           (attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

static void make_needed_folders_recursive(const char* path) {
    if (folder_exists(path)) return;
    String_View sv = sv_from_cstr(path);
    String_View dir = sv;
    sv_chop_by_delim(&dir, '/');
    
    String_Builder sb = {0};
    while (sv.count > 0) {
        String_View component = sv_chop_by_delim(&sv, '/');
        if (component.count == 0) continue;
        sb_append_buf(&sb, component.data, component.count);
        sb_append_cstr(&sb, "/");
        sb_append_null(&sb);
        nob_mkdir_if_not_exists(sb.items);
        sb.count--; // Remove null terminator
    }
    sb_free(sb);
}

static bool change_extension(String_Builder* sb, const char* filename, const char* new_ext) {
    String_View sv = sv_from_cstr(filename);
    sv_chop_by_delim(&sv, '.');

    if (sv.count == 0) return false;
    sb_append_buf(sb, filename, sv.data - filename);
    sb_append_cstr(sb, new_ext);
    return true;
}

static void change_sb_extension(String_Builder* sb, const char* new_ext) {
    // Find the last '.' in the last path component
    size_t i = sb->count;
    while (i > 0) {
        char c = sb->items[i-1];
        if (c == '.') {
            sb->count = i-1;
            break;
        }
        if (c == '/' || c == '\\') {
            break;
        }
        i--;
    }
    sb_append_cstr(sb, ".");
    sb_append_cstr(sb, new_ext);
}

static bool collect_source_files(const char* dirpath, Nob_File_Paths* paths, char* extension) {
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(dirpath, &children)) return false;

    for (size_t i = 0; i < children.count; ++i) {
        const char* child = children.items[i];
        if (child[0] == '.') continue;

        char* fullpath = nob_temp_sprintf("%s/%s", dirpath, child);

        if (is_dir(fullpath)) {
            if (!collect_source_files(fullpath, paths, extension)) {
                nob_da_free(children);
                return false;
            }
        } else {
            const char* ext = nob_get_ext(child);
            if (ext && strcmp(ext, extension) == 0) {
                nob_da_append(paths, nob_temp_strdup(fullpath));
            }
        }
    }

    nob_da_free(children);
    return true;
}

static bool build_source_file(Nob_Cmd* cmd, const char* filename, const char* example_name, bool debug) {
    bool is_example = false;
    char* relative_path = NULL;
    if (strstr(filename, "src/") == filename) {
        relative_path = (char*)filename + 4;
    } else if (strstr(filename, "examples/") == filename) {
        relative_path = (char*)filename + 9;
        is_example = true;
    } else {
        nob_log(NOB_ERROR, "File %s is not in src or examples", filename);
        return false;
    }

    // Form the object file path
    Nob_String_Builder obj_path = {0};
    sb_append_cstr(&obj_path, BUILD_PATH(debug));
    if (is_example) {
        sb_append_cstr(&obj_path, "examples/");
    } else {
        sb_append_cstr(&obj_path, "src/");
    }
    sb_append_cstr(&obj_path, relative_path);
    change_sb_extension(&obj_path, "o");
    sb_append_null(&obj_path);

    // Make sure the directory exists
    String_View dir = get_dirname(obj_path.items);
    if (dir.count > 0) {
        char dir_str[1024];
        snprintf(dir_str, sizeof(dir_str), "%.*s", (int)dir.count, dir.data);
        make_needed_folders_recursive(dir_str);
    }

    Nob_String_Builder string_buffer = {0};
    Nob_File_Paths deps = {0};
    bool needs_build = nob_c_needs_rebuild(&string_buffer, &deps, obj_path.items, filename);
    if (!needs_build) {
        nob_sb_free(obj_path);
        nob_sb_free(string_buffer);
        nob_da_free(deps);
        return true;
    }

    cmd->count = 0;
    nob_cc(cmd);
    nob_cmd_append(cmd, "-c", filename, "-o", obj_path.items, 
               "-MP", "-MMD");
    nob_cmd_append(cmd, COMPILER_ARGS);
    nob_cmd_append(cmd, debug ? "-g" : "-O3");
    if (debug) nob_cmd_append(cmd, "-DDEBUG");
    nob_cmd_append(cmd, "-I./src");
    if (is_example) {
        char* example_include = nob_temp_sprintf("-I./examples/%s", example_name);
        nob_cmd_append(cmd, example_include);
    }

    bool res = nob_cmd_run_sync(*cmd);
    nob_sb_free(obj_path);
    nob_sb_free(string_buffer);
    nob_da_free(deps);
    return res;
}

static bool build_example(const char* example_name, bool debug) {
    // Ensure build directory exists
    const char* build_path = BUILD_PATH(debug);
    make_needed_folders_recursive(build_path);

    Nob_File_Paths src_files = {0};
    if (!collect_source_files("src", &src_files, "c")) {
        nob_log(NOB_ERROR, "Failed to collect source files");
        return false;
    }

    char* example_dir = nob_temp_sprintf("examples/%s", example_name);
    Nob_File_Paths example_files = {0};
    if (!collect_source_files(example_dir, &example_files, "c")) {
        nob_log(NOB_ERROR, "Failed to collect example files for %s", example_name);
        nob_da_free(src_files);
        return false;
    }

    Nob_Cmd cmd = {0};
    for (size_t i = 0; i < src_files.count; i++) {
        if (!build_source_file(&cmd, src_files.items[i], example_name, debug)) {
            nob_log(NOB_ERROR, "Failed to build %s", src_files.items[i]);
            nob_da_free(src_files);
            nob_da_free(example_files);
            nob_cmd_free(cmd);
            return false;
        }
    }

    for (size_t i = 0; i < example_files.count; i++) {
        if (!build_source_file(&cmd, example_files.items[i], example_name, debug)) {
            nob_log(NOB_ERROR, "Failed to build %s", example_files.items[i]);
            nob_da_free(src_files);
            nob_da_free(example_files);
            nob_cmd_free(cmd);
            return false;
        }
    }

    // Form executable path
    char* exe_path = nob_temp_sprintf("%s%s%s", build_path, example_name,
#ifdef _WIN32
        ".exe"
#else
        ""
#endif
    );

    // Collect all object files
    Nob_File_Paths obj_files = {0};
    for (size_t i = 0; i < src_files.count; i++) {
        Nob_String_Builder obj_path = {0};
        sb_append_cstr(&obj_path, build_path);
        sb_append_cstr(&obj_path, "src/");
        const char* rel_path = src_files.items[i] + 4; // Skip "src/"
        sb_append_cstr(&obj_path, rel_path);
        change_sb_extension(&obj_path, "o");
        sb_append_null(&obj_path);
        nob_da_append(&obj_files, nob_temp_strdup(obj_path.items));
        nob_sb_free(obj_path);
    }

    for (size_t i = 0; i < example_files.count; i++) {
        Nob_String_Builder obj_path = {0};
        sb_append_cstr(&obj_path, build_path);
        sb_append_cstr(&obj_path, "examples/");
        const char* rel_path = example_files.items[i] + 9; // Skip "examples/"
        sb_append_cstr(&obj_path, rel_path);
        change_sb_extension(&obj_path, "o");
        sb_append_null(&obj_path);
        nob_da_append(&obj_files, nob_temp_strdup(obj_path.items));
        nob_sb_free(obj_path);
    }

    // Check if executable needs rebuilding
    if (nob_needs_rebuild(exe_path, (const char**)obj_files.items, obj_files.count) == 0) {
        nob_log(NOB_INFO, "Executable %s is up to date", exe_path);
        nob_da_free(src_files);
        nob_da_free(example_files);
        nob_da_free(obj_files);
        nob_cmd_free(cmd);
        return true;
    }

    // Link executable
    cmd.count = 0;
    nob_cc(&cmd);
    for (size_t i = 0; i < obj_files.count; i++) {
        nob_cmd_append(&cmd, obj_files.items[i]);
    }
    nob_cc_output(&cmd, exe_path);
    nob_cmd_append(&cmd, LINKER_FLAGS);
    if (debug) nob_cmd_append(&cmd, "-g");

    bool res = nob_cmd_run_sync(cmd);
    nob_da_free(src_files);
    nob_da_free(example_files);
    nob_da_free(obj_files);
    nob_cmd_free(cmd);
    return res;
}

static bool get_all_examples(Nob_File_Paths* examples) {
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir("examples", &children)) return false;

    for (size_t i = 0; i < children.count; ++i) {
        const char* child = children.items[i];
        if (child[0] == '.') continue;

        char* fullpath = nob_temp_sprintf("examples/%s", child);
        if (is_dir(fullpath)) {
            nob_da_append(examples, nob_temp_strdup(child));
        }
    }

    nob_da_free(children);
    return true;
}

#ifdef _WIN32
static bool is_version_newer(const char* v1, const char* v2) {
    if(strcmp(v1, ".") == 0 || strcmp(v1, "..") == 0){
        return false;
    }

    if(strcmp(v2, ".") == 0 || strcmp(v2, "..") == 0){
        return true;
    }

    int major1, minor1, patch1;
    int major2, minor2, patch2;
    if (sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1) != 3) return false;
    if (sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2) != 3) return false;

    return (major1 > major2) || 
           (major1 == major2 && minor1 > minor2) ||
           (major1 == major2 && minor1 == minor2 && patch1 > patch2);
}
#endif

// Add this function near the other utility functions
static bool compile_shader(const char* input_path, const char* output_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "glslc");
    
    // Add appropriate flags based on shader type
    const char* filename = strrchr(input_path, '/');
    if (!filename) filename = strrchr(input_path, '\\');
    if (!filename) filename = input_path;
    else filename++;

    String_View sv = sv_from_cstr(input_path);
    sv_chop_by_delim(&sv, '.');
    
    if(sv_eq(sv,sv_from_cstr("vert.glsl"))) {
        nob_cmd_append(&cmd, "-fshader-stage=vertex");
    } else if (sv_eq(sv,sv_from_cstr("frag.glsl"))) {
        nob_cmd_append(&cmd, "-fshader-stage=fragment");
    }

    nob_cmd_append(&cmd, input_path, "-o", output_path);
    
    bool result = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return result;
}

// Add this function to collect and compile shaders
static bool build_shaders(bool force_all) {
    Nob_File_Paths shader_files = {0};
    if (!collect_source_files("assets/shaders/compile", &shader_files, "glsl")) {
        nob_log(NOB_ERROR, "Failed to collect shader files");
        return false;
    }

    if(!folder_exists("assets/shaders/compiled")) mkdir_if_not_exists("assets/shaders/compiled");

    bool all_compiled = true;
    for (size_t i = 0; i < shader_files.count; i++) {
        const char* input_path = shader_files.items[i];

        String_View sv = sv_from_cstr(input_path);
        sv_chop_by_delim(&sv, '.');

        if(sv_eq(sv,sv_from_cstr("glsl"))) continue;

        // Create output path by changing extension to .spv
        Nob_String_Builder output_path = {0};
        sb_append_cstr(&output_path, "assets/shaders/compiled");
        sb_append_cstr(&output_path, strrchr(input_path, '/'));
        change_sb_extension(&output_path, "spv");
        sb_append_null(&output_path);

        // Make sure output directory exists
        String_View dir = get_dirname(output_path.items);
        if (dir.count > 0) {
            char dir_str[1024];
            snprintf(dir_str, sizeof(dir_str), "%.*s", (int)dir.count, dir.data);
            make_needed_folders_recursive(dir_str);
        }

        if (force_all || nob_needs_rebuild1(output_path.items, input_path)) {
            nob_log(NOB_INFO, "Compiling shader: %s", input_path);
            if (!compile_shader(input_path, output_path.items)) {
                nob_log(NOB_ERROR, "Failed to compile shader: %s", input_path);
                all_compiled = false;
            }
        } else {
            nob_log(NOB_INFO, "Shader %s is up to date", input_path);
        }

        nob_sb_free(output_path);
    }

    nob_da_free(shader_files);
    return all_compiled;
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

#ifdef _WIN32
    if (is_dir(VULKAN_SDK_SEARCH_PATH)) {
        Nob_File_Paths versions = {0};
        nob_read_entire_dir(VULKAN_SDK_SEARCH_PATH, &versions);
        
        if (versions.count == 0) {
            nob_log(NOB_ERROR, "No VulkanSDK versions found in %s", VULKAN_SDK_SEARCH_PATH);
            return 1;
        }

        const char* latest = versions.items[0];
        for (size_t i = 1; i < versions.count; i++) {
            if (is_version_newer(versions.items[i], latest)) {
                latest = versions.items[i];
            }
        }

        vulkanSDKPathINC = nob_temp_sprintf("-I%s/%s/Include", VULKAN_SDK_SEARCH_PATH, latest);
        vulkanSDKPathLIB = nob_temp_sprintf("-L%s/%s/Lib", VULKAN_SDK_SEARCH_PATH, latest);
        nob_da_free(versions);
    } else {
        nob_log(NOB_ERROR, "VulkanSDK not found at %s", VULKAN_SDK_SEARCH_PATH);
        return 1;
    }
#endif

    bool debug = true;
    bool run_after = false;
    bool clean = false;
    bool shaders_only = false;
    Nob_File_Paths examples_to_build = {0};

    File_Paths argsToPass = {0};

    bool collectingArgs = false;

    char* program = nob_shift_args(&argc, &argv);
    while (argc > 0) {
        char* arg = nob_shift_args(&argc, &argv);
        if(collectingArgs){
            nob_da_append(&argsToPass, arg);
            continue;
        }

        if (strcmp(arg, "release") == 0) debug = false;
        else if (strcmp(arg, "run") == 0) run_after = true;
        else if (strcmp(arg, "clean") == 0) clean = true;
        else if (strcmp(arg, "shaders") == 0) shaders_only = true;
        else if (strcmp(arg, "--") == 0) collectingArgs = true;
        else {
            nob_da_append(&examples_to_build, arg);
        }
    }

    if (clean) {
        remove_directory("build");
        return 0;
    }

    if (shaders_only) {
        return build_shaders(true) ? 0 : 1;
    }

    // Compile shaders before building examples
    if (!build_shaders(false)) {
        nob_log(NOB_ERROR, "Failed to compile shaders");
        return 1;
    }

    if (examples_to_build.count == 0) {
        if (!get_all_examples(&examples_to_build)) {
            nob_log(NOB_ERROR, "Failed to collect examples");
            return 1;
        }
    }

    bool all_built = true;
    for (size_t i = 0; i < examples_to_build.count; i++) {
        const char* example = examples_to_build.items[i];
        nob_log(NOB_INFO, "Building example: %s", example);
        if (!build_example(example, debug)) {
            nob_log(NOB_ERROR, "Failed to build example: %s", example);
            all_built = false;
        }
    }

    if (run_after && all_built) {
        const char* example_to_run = examples_to_build.count > 0 ? 
            examples_to_build.items[0] : DEFAULT_EXAMPLE;
        nob_log(NOB_INFO, "Running example: %s", example_to_run);

        char* exe_path = nob_temp_sprintf("%s%s%s", BUILD_PATH(debug), example_to_run,
        #ifdef _WIN32
                ".exe"
        #else
                ""
        #endif
            );
        
        Cmd cmd = {0};
        nob_cmd_append(&cmd, exe_path);
        for(int i = 0; i <argsToPass.count; i++){
            nob_cmd_append(&cmd, argsToPass.items[i]);
        }
        if(!nob_cmd_run_sync(cmd)){
            nob_log(NOB_ERROR, "Failed to run example: %s", example_to_run);
        }
    }

    nob_da_free(examples_to_build);
    return all_built ? 0 : 1;
}