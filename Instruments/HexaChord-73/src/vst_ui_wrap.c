#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdint.h>
#include "hexachord_params.h"

struct AEffect;
static void setParameter(struct AEffect *, int32_t, float);
static intptr_t dispatcher(struct AEffect *, int32_t, int32_t, intptr_t, void *, float);
static void process(struct AEffect *, float **, float **, int32_t);
static void processReplacing(struct AEffect *, float **, float **, int32_t);
static void *hexachord_allocate(size_t, size_t);

// Function-like aliases preserve the shared entry and ABI fields while intercepting UI writes.
#define setParameter(...) hexachord_base_setParameter(__VA_ARGS__)
#define dispatcher(...) hexachord_base_dispatcher(__VA_ARGS__)
#define process(...) hexachord_base_process(__VA_ARGS__)
#define processReplacing(...) hexachord_base_processReplacing(__VA_ARGS__)
#define calloc(...) hexachord_allocate(__VA_ARGS__)
#include "vst2_wrap.c"
#undef calloc
#undef processReplacing
#undef process
#undef dispatcher
#undef setParameter

typedef struct {
    wrap_t base;
    atomic_int restoring;
} hexachord_wrap_t;

enum { effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4,
       effGetProgramName = 5, effGetProgramNameIndexed = 29 };

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Program restore guard must remain lock-free");

static void *hexachord_allocate(size_t count, size_t size) {
    if (count != 1 || size != sizeof(wrap_t)) return NULL;
    hexachord_wrap_t *instance = calloc(1, sizeof *instance);
    if (instance) {
        atomic_init(&instance->restoring, 0);
        instance->base.fx.numPrograms = 4;
    }
    return instance;
}

static int programIndex(AEffect *effect) {
    int model = (int)lroundf(get_norm(effect->object, 2) * 3);
    for (int index = 0; index < 4; ++index)
        if (factoryProgramModels[index] == model) return index;
    return 0;
}

static int stateWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

static int validState(const char *state) {
    int count;
    if (!strncmp(state, "HX73 1 ", 7)) count = 16;
    else if (!strncmp(state, "HX73 2 ", 7)) count = 20;
    else if (!strncmp(state, "HX73 3 ", 7)) count = NPARAMS - 2;
    else return 0;
    const char *cursor = state + 7;
    for (int index = 0; index < count; ++index) {
        while (stateWhitespace(*cursor)) ++cursor;
        char *end;
        errno = 0;
        float value = strtof(cursor, &end);
        const param_t *parameter = &PARAMS[index < 16 ? index : index + 2];
        float minimum = parameter->nopts ? 0 : parameter->min;
        float maximum = parameter->nopts ? parameter->nopts - 1 : parameter->max;
        if (end == cursor || errno == ERANGE || !isfinite(value) || value < minimum || value > maximum ||
            (parameter->nopts && value != roundf(value)) || (*end && !stateWhitespace(*end))) return 0;
        cursor = end;
    }
    while (stateWhitespace(*cursor)) ++cursor;
    return !*cursor;
}

static void setParameter(AEffect *effect, int32_t index, float value) {
    if (index != 16 && index != 17) {
        hexachord_base_setParameter(effect, index, value);
        return;
    }
    if (!isfinite(value)) return;
    hexachord_base_setParameter(effect, index, value);
}

static void applyChordPad(AEffect *effect) {
    wrap_t *instance = effect->object;
    char pending[2];
    if (g_api->get_param(instance->dsp, "ChordPadPending", pending, sizeof pending) <= 0 || pending[0] != '1') return;
    const float before[] = {get_norm(instance, 0), get_norm(instance, 1)};
    g_api->set_param(instance->dsp, "ChordPadApply", "1");
    for (int parameter = 0; parameter < 2; ++parameter) {
        if (before[parameter] == get_norm(instance, parameter)) continue;
        instance->qacc[parameter] = 0;
        instance->last_pos[parameter] = -1;
        instance->host_pos[parameter] = -1;
        instance->changed[parameter] = 1;
        instance->need_update_display = 1;
    }
}

static void process(AEffect *effect, float **inputs, float **outputs, int32_t frames) {
    if (frames > 0) atomic_store_explicit(&((hexachord_wrap_t *)effect->object)->restoring, 0, memory_order_release);
    applyChordPad(effect);
    hexachord_base_process(effect, inputs, outputs, frames);
}

static void processReplacing(AEffect *effect, float **inputs, float **outputs, int32_t frames) {
    if (frames > 0) atomic_store_explicit(&((hexachord_wrap_t *)effect->object)->restoring, 0, memory_order_release);
    applyChordPad(effect);
    hexachord_base_processReplacing(effect, inputs, outputs, frames);
}

static intptr_t dispatcher(AEffect *effect, int32_t operation, int32_t index,
                           intptr_t value, void *pointer, float option) {
    hexachord_wrap_t *instance = effect->object;
    if (operation == effSetProgram) {
        if (value < 0 || value >= 4) return 0;
        if (atomic_load_explicit(&instance->restoring, memory_order_acquire)) return 1;
        const char *state = factoryStates[factoryProgramModels[value]];
        if (!hexachord_base_dispatcher(effect, effSetChunk, 0, strlen(state) + 1, (void *)state, 0)) return 0;
        for (int parameter = 0; parameter < NPARAMS; ++parameter)
            instance->base.changed[parameter] = parameter != 16 && parameter != 17;
        instance->base.need_update_display = 1;
        return 1;
    }
    if (operation == effGetProgram) return programIndex(effect);
    if (operation == effGetProgramName || operation == effGetProgramNameIndexed) {
        if (!pointer) return 0;
        int selected = operation == effGetProgramName ? programIndex(effect) : index;
        ((char *)pointer)[0] = 0;
        if (selected < 0 || selected >= 4) return 0;
        copy_str(pointer, factoryNames[factoryProgramModels[selected]], 24);
        return 1;
    }
    if (operation == effSetProgramName) return 0;
    if (operation == effSetChunk) {
        if (!pointer || value <= 0 || (size_t)value > sizeof(((wrap_t *)effect->object)->chunk)) return 0;
        const char *chunk = pointer;
        if (chunk[value - 1] || memchr(chunk, 0, (size_t)value - 1) || !validState(chunk)) return 0;
        intptr_t result = hexachord_base_dispatcher(effect, operation, index, value, pointer, option);
        // JUCE may reselect a program after restoring its saved chunk.
        if (result) atomic_store_explicit(&instance->restoring, 1, memory_order_release);
        return result;
    }
    return hexachord_base_dispatcher(effect, operation, index, value, pointer, option);
}
