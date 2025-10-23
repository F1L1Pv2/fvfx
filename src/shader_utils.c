#include "shader_utils.h"
#include <stdio.h>

#include "arena_alloc.h"
#include "ll.h"

char* get_vfxInputTypeName(VfxInputType type){
    switch (type)
    {
        case VFX_BOOL: return "bool";
        case VFX_INT: return "int";
        case VFX_UINT: return "uint";
        case VFX_FLOAT: return "float";
        case VFX_DOUBLE: return "double";
        case VFX_BVEC2: return "bvec2";
        case VFX_BVEC3: return "bvec3";
        case VFX_BVEC4: return "bvec4";
        case VFX_IVEC2: return "ivec2";
        case VFX_IVEC3: return "ivec3";
        case VFX_IVEC4: return "ivec4";
        case VFX_UVEC2: return "uvec2";
        case VFX_UVEC3: return "uvec3";
        case VFX_UVEC4: return "uvec4";
        case VFX_VEC2: return "vec2";
        case VFX_VEC3: return "vec3";
        case VFX_VEC4: return "vec4";
        case VFX_DVEC2: return "dvec2";
        case VFX_DVEC3: return "dvec3";
        case VFX_DVEC4: return "dvec4";

        default: UNREACHABLE("update this!");
    }
}

size_t get_vfxInputTypeSize(VfxInputType type){
    switch (type)
    {
        case VFX_BOOL: return sizeof(bool);
        case VFX_INT: return sizeof(int32_t);
        case VFX_UINT: return sizeof(uint32_t);
        case VFX_FLOAT: return sizeof(float);
        case VFX_DOUBLE: return sizeof(double);
        case VFX_BVEC2: return sizeof(bool) * 2;
        case VFX_BVEC3: return sizeof(bool) * 3;
        case VFX_BVEC4: return sizeof(bool) * 4;
        case VFX_IVEC2: return sizeof(int32_t) * 2;
        case VFX_IVEC3: return sizeof(int32_t) * 3;
        case VFX_IVEC4: return sizeof(int32_t) * 4;
        case VFX_UVEC2: return sizeof(uint32_t) * 2;
        case VFX_UVEC3: return sizeof(uint32_t) * 3;
        case VFX_UVEC4: return sizeof(uint32_t) * 4;
        case VFX_VEC2: return sizeof(float) * 2;
        case VFX_VEC3: return sizeof(float) * 3;
        case VFX_VEC4: return sizeof(float) * 4;
        case VFX_DVEC2: return sizeof(double) * 2;
        case VFX_DVEC3: return sizeof(double) * 3;
        case VFX_DVEC4: return sizeof(double) * 4;

        default: UNREACHABLE("update this!");
    }
}

static void* ll_arena_allocator(size_t size, void* caller_data){
    return aa_alloc((ArenaAllocator*)caller_data,size);
}

bool extractVFXModuleMetaData(String_View sv, VfxModule* out, ArenaAllocator* aa){
    sv = sv_trim_left(sv);
    if(sv.count < 2) {
        printf("Empty file!");
        return false;
    }
    if(sv.data[0] != '/' && sv.data[1] != '*') {
        printf("No metadata descriptor\n");
        return false;
    }
    sv.data += 2;
    sv = sv_trim_left(sv);
    if(sv.count == 0) {
        printf("Empty file!");
        return false;
    }

    String_Builder sb = {0};
    size_t push_constant_offset = 0;

    out->inputs = NULL;
    while(sv.data[0] != '*' && sv.data[1] != '/' && sv.count > 0){
        String_View leftSide = sv_chop_by_delim(&sv, ':');
        sv = sv_trim_left(sv);

        String_View arg = sv_chop_by_delim(&sv, '\n');
        if(arg.count == 0) printf("Expected value for "SV_Fmt"\n",SV_Arg(leftSide));
        if(sv_eq(leftSide, sv_from_cstr("Name"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->name = aa_strdup(aa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Description"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->description = aa_strdup(aa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Author"))){
            sb.count = 0;
            sb_append_buf(&sb, arg.data, arg.count);
            sb_append_null(&sb);
            out->author = aa_strdup(aa, sb.items);
        }
        else if(sv_eq(leftSide, sv_from_cstr("Input"))){
            String_View inputArg = sv_trim_left(arg);
            String_View inputType = sv_trim(sv_chop_by_delim(&inputArg, ' '));
            String_View inputName = sv_trim(sv_chop_by_delim(&inputArg, ' '));

            if(inputType.count == 0){
                printf("Please provide type for MetaInput\n");
                return false;
            }

            if(inputName.count == 0){
                printf("Please provide name for MetaInput\n");
                return false;
            }

            VfxInput input = {0};
            
                 if(sv_eq(inputType, sv_from_cstr("float"))) input.type = VFX_FLOAT;
            else if(sv_eq(inputType, sv_from_cstr("int"))) input.type = VFX_INT;
            else if(sv_eq(inputType, sv_from_cstr("uint"))) input.type = VFX_UINT;
            else if(sv_eq(inputType, sv_from_cstr("double"))) input.type = VFX_DOUBLE;
            else if(sv_eq(inputType, sv_from_cstr("bool"))) input.type = VFX_BOOL;

            else if(sv_eq(inputType, sv_from_cstr("vec2"))) input.type = VFX_VEC2;
            else if(sv_eq(inputType, sv_from_cstr("vec3"))) input.type = VFX_VEC3;
            else if(sv_eq(inputType, sv_from_cstr("vec4"))) input.type = VFX_VEC4;

            else if(sv_eq(inputType, sv_from_cstr("bvec2"))) input.type = VFX_BVEC2;
            else if(sv_eq(inputType, sv_from_cstr("bvec3"))) input.type = VFX_BVEC3;
            else if(sv_eq(inputType, sv_from_cstr("bvec4"))) input.type = VFX_BVEC4;

            else if(sv_eq(inputType, sv_from_cstr("ivec2"))) input.type = VFX_IVEC2;
            else if(sv_eq(inputType, sv_from_cstr("ivec3"))) input.type = VFX_IVEC3;
            else if(sv_eq(inputType, sv_from_cstr("ivec4"))) input.type = VFX_IVEC4;

            else if(sv_eq(inputType, sv_from_cstr("uvec2"))) input.type = VFX_UVEC2;
            else if(sv_eq(inputType, sv_from_cstr("uvec3"))) input.type = VFX_UVEC3;
            else if(sv_eq(inputType, sv_from_cstr("uvec4"))) input.type = VFX_UVEC4;

            else if(sv_eq(inputType, sv_from_cstr("dvec2"))) input.type = VFX_DVEC2;
            else if(sv_eq(inputType, sv_from_cstr("dvec3"))) input.type = VFX_DVEC3;
            else if(sv_eq(inputType, sv_from_cstr("dvec4"))) input.type = VFX_DVEC4;

            if(input.type == VFX_NONE){
                printf("Unknown input type: "SV_Fmt"\n", SV_Arg(inputType));
                return false;
            }
            
            sb.count = 0;
            sb_append_buf(&sb, inputName.data, inputName.count);
            sb_append_null(&sb);
            input.name = aa_strdup(aa, sb.items);

            if(inputArg.count > 0){
                sb.count = 0;
                sb_append_buf(&sb, inputArg.data, inputArg.count);
                sb_append_null(&sb);

                input.defaultValue = aa_alloc(aa, sizeof(*input.defaultValue));
                out->hasDefaultValues = true;

                switch (input.type)
                {
                case VFX_FLOAT: sscanf(sb.items,"%f",         &input.defaultValue->as.Float); break;
                case VFX_VEC2: sscanf(sb.items,"%f,%f",       &input.defaultValue->as.vec2.x, &input.defaultValue->as.vec2.y); break;
                case VFX_VEC3: sscanf(sb.items,"%f,%f,%f",    &input.defaultValue->as.vec3.x, &input.defaultValue->as.vec3.y, &input.defaultValue->as.vec3.z); break;
                case VFX_VEC4: sscanf(sb.items,"%f,%f,%f,%f", &input.defaultValue->as.vec4.x, &input.defaultValue->as.vec4.y, &input.defaultValue->as.vec4.z, &input.defaultValue->as.vec4.w); break;
                
                default: TODO("IMPLEMENT THIS");
                }
            }

            input.push_constant_offset = push_constant_offset;
            push_constant_offset += get_vfxInputTypeSize(input.type);

            ll_push(&out->inputs, input, ll_arena_allocator, aa);
        }
        else{
            printf("Unknown metadata attribute: "SV_Fmt"\n", SV_Arg(leftSide));

            da_free(sb);
            return false;
        }
        sv = sv_trim_left(sv);
        if(sv.count == 0) {
            printf("No metadata ending '*/' reached end of file");
            da_free(sb);
            return false;
        }
    }

    da_free(sb);
    return true;
}

bool preprocessVFXModule(String_Builder* sb, VfxModule* module){
    String_Builder newSB = {0};

    const char* prepend = 
                        "#version 450\n"
                        "layout(location = 0) out vec4 outColor;\n"
                        "layout(location = 0) in vec2 uv;\n"
                        "layout (set = 0, binding = 0) uniform sampler2D imageIN;\n"
    ;

    sb_append_cstr(&newSB, prepend);


    /*
    
    layout (push_constant) uniform constants
    {
        mat4 projView;
        SpriteDrawBuffer spriteDrawBuffer;
    } Input;
    
    */
        
    sb_append_cstr(&newSB, "layout (push_constant) uniform constants\n{\n");
    sb_append_cstr(&newSB,"vec2 renderArea;\n");
    sb_append_cstr(&newSB,"vec2 mediaArea;\n");

    for(VfxInput* input = module->inputs; input != NULL; input = input->next){
        assert(input->type != VFX_NONE);
        sb_append_cstr(&newSB, get_vfxInputTypeName(input->type));
        sb_append_cstr(&newSB, " ");
        sb_append_cstr(&newSB, input->name);
        sb_append_cstr(&newSB, ";\n");
    }

    sb_append_cstr(&newSB, "} Input;\n");


    sb_append_buf(&newSB,sb->items,sb->count);

    da_free((*sb));

    *sb = newSB;

    return true;
}

#define LERP(a,b,t) ((a) + ((b) - (a)) * (t))

void lerpVfxValue(VfxInputType type, VfxInputValue* out, VfxInputValue* a, VfxInputValue* b, double t) {
    switch (type) {
        case VFX_FLOAT:
            out->as.Float = (float)LERP(a->as.Float, b->as.Float, t);
            break;
        case VFX_DOUBLE:
            out->as.Double = LERP(a->as.Double, b->as.Double, t);
            break;
        case VFX_INT:
            out->as.Int = (int32_t)LERP(a->as.Int, b->as.Int, t);
            break;
        case VFX_UINT:
            out->as.Uint = (uint32_t)LERP(a->as.Uint, b->as.Uint, t);
            break;
        case VFX_VEC2:
            out->as.vec2.x = (float)LERP(a->as.vec2.x, b->as.vec2.x, t);
            out->as.vec2.y = (float)LERP(a->as.vec2.y, b->as.vec2.y, t);
            break;
        case VFX_VEC3:
            out->as.vec3.x = (float)LERP(a->as.vec3.x, b->as.vec3.x, t);
            out->as.vec3.y = (float)LERP(a->as.vec3.y, b->as.vec3.y, t);
            out->as.vec3.z = (float)LERP(a->as.vec3.z, b->as.vec3.z, t);
            break;
        case VFX_VEC4:
            out->as.vec4.x = (float)LERP(a->as.vec4.x, b->as.vec4.x, t);
            out->as.vec4.y = (float)LERP(a->as.vec4.y, b->as.vec4.y, t);
            out->as.vec4.z = (float)LERP(a->as.vec4.z, b->as.vec4.z, t);
            out->as.vec4.w = (float)LERP(a->as.vec4.w, b->as.vec4.w, t);
            break;
        default:
            *out = *a;
            break;
    }
}