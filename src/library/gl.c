#include "gl.h"

#include <stdio.h>
#include <string.h>

void _bolt_glcontext_init(struct GLContext*, void*, void*);
void _bolt_glcontext_free(struct GLContext*);

struct GLList contexts = {0};
_Thread_local struct GLContext* current_context = NULL;

#define LIST_GROWTH_STEP 256
#define PTR_LIST_CAPACITY 256*256
#define CONTEXTS_CAPACITY 64 // not growable so we just have to hard-code a number and hope it's enough forever

#define MAKE_GETTERS(STRUCT, NAME, ID_TYPE) \
struct STRUCT* _bolt_find_##NAME(struct GLList* list, ID_TYPE id) { \
    struct STRUCT** pointer_cache = list->pointers; \
    if (id == 0) return NULL; \
    uint8_t cacheable = (id < PTR_LIST_CAPACITY); \
    if (cacheable && pointer_cache[id] != NULL) return pointer_cache[id]; \
    for (size_t i = 0; i < list->capacity; i += 1) { \
        struct STRUCT* ptr = &((struct STRUCT*)(list->data))[i]; \
        if (ptr->id == id) { \
            if (cacheable) pointer_cache[id] = ptr; \
            return ptr; \
        } \
    } \
    return NULL; \
} \
struct STRUCT* _bolt_get_##NAME(struct GLList* list, ID_TYPE id) { \
    struct STRUCT** pointer_cache = list->pointers; \
    if (id == 0) return NULL; \
    uint8_t cacheable = (id < PTR_LIST_CAPACITY); \
    if (cacheable && pointer_cache[id] != NULL) return pointer_cache[id]; \
    else if (!cacheable) { \
        for (size_t i = 0; i < list->capacity; i += 1) { \
            struct STRUCT* ptr = &((struct STRUCT*)(list->data))[i]; \
            if (ptr->id == id) { \
                return ptr; \
            } \
        } \
    } \
    if (list->first_empty >= list->capacity) { \
        size_t old_capacity = list->capacity; \
        list->capacity += LIST_GROWTH_STEP; \
        struct STRUCT* new_ptr = calloc(list->capacity, sizeof(struct STRUCT)); \
        memcpy(new_ptr, list->data, old_capacity * sizeof(struct STRUCT)); \
        intptr_t ptr_offset = (intptr_t)new_ptr - (intptr_t)list->data; \
        free(list->data); \
        list->data = new_ptr; \
        for (size_t i = 0; i < PTR_LIST_CAPACITY; i += 1) { \
            if (pointer_cache[i]) pointer_cache[i] = (struct STRUCT*)((uintptr_t)(pointer_cache[i]) + ptr_offset);\
        } \
    } \
    struct STRUCT* ptr = &((struct STRUCT*)(list->data))[list->first_empty]; \
    pointer_cache[id] = ptr; \
    ptr->id = id; \
    struct STRUCT* inc_ptr = ptr; \
    while (list->first_empty < list->capacity && inc_ptr->id != 0) { \
        inc_ptr += 1; \
        list->first_empty += 1; \
    } \
    return ptr; \
}
MAKE_GETTERS(GLArrayBuffer, buffer, unsigned int)
MAKE_GETTERS(GLProgram, program, unsigned int)
MAKE_GETTERS(GLTexture2D, texture, unsigned int)

struct GLContext* _bolt_context() {
    return current_context;
}

void _bolt_create_context(void* egl_context, void* shared) {
    if (!contexts.capacity) {
        contexts.capacity = CONTEXTS_CAPACITY;
        contexts.data = malloc(CONTEXTS_CAPACITY * sizeof(struct GLContext));
    }
    for (size_t i = 0; i < contexts.capacity; i += 1) {
        struct GLContext* ptr = &((struct GLContext*)(contexts.data))[i];
        if (ptr->id == 0) {
            _bolt_glcontext_init(ptr, egl_context, shared);
            ptr->is_attached = 1;
            return;
        }
    }
}

void _bolt_make_context_current(void* egl_context) {
    if (current_context) {
        current_context->is_attached = 0;
        if (current_context->deferred_destroy) _bolt_destroy_context(current_context);
    }
    if (!egl_context) {
        current_context = 0;
        return;
    }
    if (!contexts.capacity) {
        contexts.capacity = CONTEXTS_CAPACITY;
        contexts.data = malloc(CONTEXTS_CAPACITY * sizeof(struct GLContext));
    }
    for (size_t i = 0; i < contexts.capacity; i += 1) {
        struct GLContext* ptr = &((struct GLContext*)(contexts.data))[i];
        if (ptr->id == (uintptr_t)egl_context) {
            current_context = ptr;
            return;
        }
    }
}

void _bolt_destroy_context(void* egl_context) {
    for (size_t i = 0; i < contexts.capacity; i += 1) {
        struct GLContext* ptr = &((struct GLContext*)(contexts.data))[i];
        if (ptr->id == (uintptr_t)egl_context) {
            if (ptr->is_attached) {
                ptr->deferred_destroy = 1;
            } else {
                _bolt_glcontext_free(ptr);
                ptr->id = 0;
            }
            break;
        }
    }
}

void _bolt_set_attr_binding(struct GLAttrBinding* binding, unsigned int buffer, int size, const void* offset, unsigned int stride, uint32_t type, uint8_t normalise) {
    binding->buffer = buffer;
    binding->offset = (uintptr_t)offset;
    binding->size = size;
    binding->stride = stride;
    binding->normalise = normalise;
    binding->type = type;
}

uint8_t _bolt_get_attr_binding(struct GLContext* c, const struct GLAttrBinding* binding, size_t index, size_t num_out, float* out) {
    struct GLArrayBuffer* buffer = _bolt_find_buffer(c->shared_buffers, binding->buffer);
    if (!buffer || !buffer->data) return 0;
    uintptr_t buf_offset = binding->offset + (binding->stride * index);

    const void* ptr = buffer->data + buf_offset;
    if (!binding->normalise) {
        switch (binding->type) {
            case GL_FLOAT:
                memcpy(out, ptr, num_out * sizeof(float));
                break;
            case GL_UNSIGNED_BYTE:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(uint8_t*)(ptr + i);
                break;
            case GL_UNSIGNED_SHORT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(uint16_t*)(ptr + (i * 2));
                break;
            case GL_UNSIGNED_INT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(uint32_t*)(ptr + (i * 4));
                break;
            case GL_BYTE:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(int8_t*)(ptr + i);
                break;
            case GL_SHORT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(int16_t*)(ptr + (i * 2));
                break;
            case GL_INT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = (float)*(int32_t*)(ptr + (i * 4));
                break;
            default:
                printf("warning: unsupported non-normalise type %u\n", binding->type);
                memset(out, 0, num_out * sizeof(float));
                break;
        }
    } else {
        switch (binding->type) {
            case GL_FLOAT:
                memcpy(out, ptr, num_out * sizeof(float));
                break;
            case GL_UNSIGNED_BYTE:
                for (size_t i = 0; i < num_out; i += 1) out[i] = ((float)*(uint8_t*)(ptr + i)) / 255.0;
                break;
            case GL_UNSIGNED_SHORT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = ((float)*(uint16_t*)(ptr + (i * 2))) / 65535.0;
                break;
            case GL_UNSIGNED_INT:
                for (size_t i = 0; i < num_out; i += 1) out[i] = ((float)*(uint32_t*)(ptr + (i * 4))) / 4294967295.0;
                break;
            case GL_BYTE:
                for (size_t i = 0; i < num_out; i += 1) out[i] = ((((float)*(int8_t*)(ptr + i)) + 128.0) * 2.0 / 255.0) - 1.0;
                break;
            default:
                printf("warning: unsupported normalise type %u\n", binding->type);
                memset(out, 0, num_out * sizeof(float));
                break;
        }
    }
    return 1;
}

void _bolt_glcontext_init(struct GLContext* context, void* egl_context, void* egl_shared) {
    struct GLContext* shared = NULL;
    if (egl_shared) {
        for (size_t i = 0; i < contexts.capacity; i += 1) {
            struct GLContext* ptr = &((struct GLContext*)(contexts.data))[i];
            if (ptr->id == (uintptr_t)egl_shared) {
                shared = ptr;
                break;
            }
        }
    }
    memset(context, 0, sizeof(*context));
    context->id = (uintptr_t)egl_context;
    if (shared) {
        context->shared_programs = &shared->programs;
        context->shared_buffers = &shared->buffers;
        context->shared_textures = &shared->textures;
    } else {
        context->is_shared_owner = 1;
        context->programs.pointers = calloc(PTR_LIST_CAPACITY, sizeof(void*));
        context->buffers.pointers = calloc(PTR_LIST_CAPACITY, sizeof(void*));
        context->textures.pointers = calloc(PTR_LIST_CAPACITY, sizeof(void*));
        context->shared_programs = &context->programs;
        context->shared_buffers = &context->buffers;
        context->shared_textures = &context->textures;
    }
}

void _bolt_glcontext_free(struct GLContext* context) {
    if (context->is_shared_owner) {
        free(context->programs.pointers);
        free(context->buffers.pointers);
        free(context->textures.pointers);
        free(context->programs.data);
        free(context->buffers.data);
        free(context->textures.data);
    }
}

void _bolt_context_destroy_buffers(struct GLContext* context, unsigned int n, const unsigned int* list) {
    for (size_t i = 0; i < n; i += 1) {
        struct GLArrayBuffer* buffer = _bolt_find_buffer(context->shared_buffers, list[i]);
        if (!buffer) continue;
        buffer->id = 0;
        free(buffer->data);
        buffer->data = NULL;
        if (list[i] < PTR_LIST_CAPACITY) ((struct GLArrayBuffer**)(context->shared_buffers->pointers))[list[i]] = NULL;
        if (context->shared_buffers->first_empty > list[i]) context->shared_buffers->first_empty = list[i];
    }
}

void _bolt_context_destroy_textures(struct GLContext* context, unsigned int n, const unsigned int* list) {
    for (size_t i = 0; i < n; i += 1) {
        struct GLTexture2D* tex = _bolt_find_texture(context->shared_textures, list[i]);
        if (!tex) continue;
        free(tex->data);
        tex->data = NULL;
        tex->width = 0;
        tex->height = 0;
        if (list[i] < PTR_LIST_CAPACITY) ((struct GLTexture2D**)(context->shared_textures->pointers))[list[i]] = NULL;
        if (context->shared_textures->first_empty > list[i]) context->shared_textures->first_empty = list[i];
    }
}
