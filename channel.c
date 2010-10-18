/*
 *  channel.c
 *  dang
 *
 *  Created by Ellie on 1/10/10.
 *  Copyright 2010 Ellie. All rights reserved.
 *
 */

#include <sys/errno.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bits.h"
#include "channel.h"
#include "debug.h"


#define CHANNEL_FLAG_INUSE          UINTPTR_MAX
#define CHANNEL_POOL_ITEM(handle)   g_channel_pool.m_items[(handle) - 1]

typedef struct channel_t {
    size_t m_allocated_count;
    size_t m_count;
    anon_scalar_t *m_items;
    size_t m_start;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_has_items;
    pthread_cond_t m_has_space;
    size_t m_references;
    channel_handle_t m_next_free;
} channel_t;

typedef struct channel_pool_t {
    size_t m_allocated_count;
    size_t m_count;
    channel_t *m_items;
    size_t m_free_count;
    channel_handle_t m_free_list_head;
    pthread_mutex_t m_free_list_mutex;
} channel_pool_t;


static channel_pool_t g_channel_pool;

static const size_t _channel_pool_initial_size = 64;  // FIXME arbitrary number
static const size_t _channel_initial_size = 16;

static int _channel_init(channel_t *);
static int _channel_destroy(channel_t *);
static int _channel_reserve_unlocked(channel_t *, size_t);
static void _channel_pool_add_to_free_list(channel_handle_t);

int channel_pool_init(void) {
    if (NULL != (g_channel_pool.m_items = calloc(_channel_pool_initial_size, sizeof(*g_channel_pool.m_items)))) {
        g_channel_pool.m_allocated_count = g_channel_pool.m_free_count = _channel_pool_initial_size;
        g_channel_pool.m_count = 0;
        if (0 == pthread_mutex_init(&g_channel_pool.m_free_list_mutex, NULL)) {
            g_channel_pool.m_free_list_head = 1;
            for (channel_handle_t i = 2; i < g_channel_pool.m_allocated_count - 1; i++) {
                CHANNEL_POOL_ITEM(i).m_next_free = i;
            }
            CHANNEL_POOL_ITEM(g_channel_pool.m_allocated_count).m_next_free = 0;
            return 0;
        }
        else {
            free(g_channel_pool.m_items);
            return -1;
        }
    }
    else {
        return -1;
    }
}

int channel_pool_destroy(void) {
    assert(g_channel_pool.m_items != NULL);

    if (0 == pthread_mutex_lock(&g_channel_pool.m_free_list_mutex)) {
        for (channel_handle_t i = 1; i <= g_channel_pool.m_allocated_count; i++) {
            if (CHANNEL_POOL_ITEM(i).m_next_free == CHANNEL_FLAG_INUSE) {
                _channel_destroy(&CHANNEL_POOL_ITEM(i));                
            }
        }
        free(g_channel_pool.m_items);
        pthread_mutex_destroy(&g_channel_pool.m_free_list_mutex);
        memset(&g_channel_pool, 0, sizeof(g_channel_pool));
        return 0;
    }
    else {
        return -1;        
    }
}

channel_handle_t channel_allocate(void) {
    if (0 == pthread_mutex_lock(&g_channel_pool.m_free_list_mutex)) {
        channel_handle_t handle;
        
        if (g_channel_pool.m_free_count > 0) {
            handle = g_channel_pool.m_free_list_head;
            g_channel_pool.m_free_list_head = CHANNEL_POOL_ITEM(handle).m_next_free;
            --g_channel_pool.m_free_count;
            pthread_mutex_unlock(&g_channel_pool.m_free_list_mutex);
        }
        else {
            handle = g_channel_pool.m_allocated_count + 1;
            size_t new_size = 2 * g_channel_pool.m_allocated_count;
            
            channel_t *tmp = calloc(new_size, sizeof(*tmp));
            if (tmp == NULL) {
                pthread_mutex_unlock(&g_channel_pool.m_free_list_mutex);
                return 0;
            }
            
            memcpy(tmp, g_channel_pool.m_items, g_channel_pool.m_allocated_count * sizeof(*g_channel_pool.m_items));
            free(g_channel_pool.m_items);
            g_channel_pool.m_items = tmp;
            g_channel_pool.m_allocated_count = new_size;
            
            g_channel_pool.m_free_list_head = handle + 1;
            for (channel_handle_t i = handle + 1; i < g_channel_pool.m_allocated_count - 1; i++) {
                CHANNEL_POOL_ITEM(i).m_next_free = i;
            }
            CHANNEL_POOL_ITEM(g_channel_pool.m_allocated_count).m_next_free = 0;
            g_channel_pool.m_free_count = g_channel_pool.m_allocated_count - handle;  // FIXME think about this - is it right?

            pthread_mutex_unlock(&g_channel_pool.m_free_list_mutex);
        }

        _channel_init(&CHANNEL_POOL_ITEM(handle));

        CHANNEL_POOL_ITEM(handle).m_references = 1;
        CHANNEL_POOL_ITEM(handle).m_next_free = CHANNEL_FLAG_INUSE;

        return handle;
    }
    else {
        return 0;
    }
}

int channel_release(channel_handle_t handle) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    assert(CHANNEL_POOL_ITEM(handle).m_next_free == CHANNEL_FLAG_INUSE);
    
    if (0 == pthread_mutex_lock(&CHANNEL_POOL_ITEM(handle).m_mutex)) {
        if (--CHANNEL_POOL_ITEM(handle).m_references == 0) {
            // FIXME who should hold the lock here?
            _channel_destroy(&CHANNEL_POOL_ITEM(handle));   // n.b. this destroys the mutex, so don't try to unlock it!
            _channel_pool_add_to_free_list(handle);
            return 0;
        }
        else {
            pthread_mutex_unlock(&CHANNEL_POOL_ITEM(handle).m_mutex);
            return 0;            
        }
    }
    else {
        return -1;
    }    
}

int channel_increase_refcount(channel_handle_t handle) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    assert(CHANNEL_POOL_ITEM(handle).m_next_free == CHANNEL_FLAG_INUSE);
    
    if (0 == pthread_mutex_lock(&CHANNEL_POOL_ITEM(handle).m_mutex)) {
        ++CHANNEL_POOL_ITEM(handle).m_references;
        
        pthread_mutex_unlock(&CHANNEL_POOL_ITEM(handle).m_mutex);
        return 0;       
    }
    else {
        return -1;
    }
}

int channel_read(channel_handle_t handle, anon_scalar_t *result) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    assert(CHANNEL_POOL_ITEM(handle).m_next_free == CHANNEL_FLAG_INUSE);
    assert(result != NULL);
    
    if (0 == pthread_mutex_lock(&CHANNEL_POOL_ITEM(handle).m_mutex)) {
        while (CHANNEL_POOL_ITEM(handle).m_count == 0) {
            pthread_cond_wait(&CHANNEL_POOL_ITEM(handle).m_has_items, &CHANNEL_POOL_ITEM(handle).m_mutex);
        }
        anon_scalar_assign(result, &CHANNEL_POOL_ITEM(handle).m_items[CHANNEL_POOL_ITEM(handle).m_start]);
        CHANNEL_POOL_ITEM(handle).m_start = (CHANNEL_POOL_ITEM(handle).m_start + 1) % CHANNEL_POOL_ITEM(handle).m_allocated_count;
        CHANNEL_POOL_ITEM(handle).m_count--;
        pthread_mutex_unlock(&CHANNEL_POOL_ITEM(handle).m_mutex);        
        
        pthread_cond_signal(&CHANNEL_POOL_ITEM(handle).m_has_space);
        return 0;
    }
    else {
        return -1;
    }
}

int channel_tryread(channel_handle_t handle, anon_scalar_t *result) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    assert(CHANNEL_POOL_ITEM(handle).m_next_free == CHANNEL_FLAG_INUSE);
    assert(result != NULL);
    
    if (0 == pthread_mutex_lock(&CHANNEL_POOL_ITEM(handle).m_mutex)) {
        int status;
        if (CHANNEL_POOL_ITEM(handle).m_count > 0) {
            anon_scalar_assign(result, &CHANNEL_POOL_ITEM(handle).m_items[CHANNEL_POOL_ITEM(handle).m_start]);
            CHANNEL_POOL_ITEM(handle).m_start = (CHANNEL_POOL_ITEM(handle).m_start + 1) % CHANNEL_POOL_ITEM(handle).m_allocated_count;
            CHANNEL_POOL_ITEM(handle).m_count--;
            status = 0;
        }
        else {
            status = EWOULDBLOCK;
        }
        pthread_mutex_unlock(&CHANNEL_POOL_ITEM(handle).m_mutex);
        return status;
    }
    else {
        return -1;
    }
}

int channel_write(channel_handle_t handle, const anon_scalar_t *value) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    assert(CHANNEL_POOL_ITEM(handle).m_next_free == CHANNEL_FLAG_INUSE);
    assert(value != NULL);
    
    static const struct timespec wait_timeout = { 0, 250000 };
    
    if (0 == pthread_mutex_lock(&CHANNEL_POOL_ITEM(handle).m_mutex)) {
        while (CHANNEL_POOL_ITEM(handle).m_count >= CHANNEL_POOL_ITEM(handle).m_allocated_count) {
            if (ETIMEDOUT == pthread_cond_timedwait(&CHANNEL_POOL_ITEM(handle).m_has_space, &CHANNEL_POOL_ITEM(handle).m_mutex, &wait_timeout)) {
                _channel_reserve_unlocked(&CHANNEL_POOL_ITEM(handle), CHANNEL_POOL_ITEM(handle).m_allocated_count * 2);
            }
        }
        size_t index = (CHANNEL_POOL_ITEM(handle).m_start + CHANNEL_POOL_ITEM(handle).m_count) % CHANNEL_POOL_ITEM(handle).m_allocated_count;
        anon_scalar_clone(&CHANNEL_POOL_ITEM(handle).m_items[index], value);
        CHANNEL_POOL_ITEM(handle).m_count++;
        pthread_mutex_unlock(&CHANNEL_POOL_ITEM(handle).m_mutex);
        
        pthread_cond_signal(&CHANNEL_POOL_ITEM(handle).m_has_items);
        return 0;        
    }
    else {
        return -1;
    }
}

static void _channel_pool_add_to_free_list(channel_handle_t handle) {
    assert(handle != 0);
    assert(handle <= g_channel_pool.m_allocated_count);
    
    channel_handle_t prev = handle;
    
    if (0 == pthread_mutex_lock(&g_channel_pool.m_free_list_mutex)) {
        for ( ; prev > 0 && !(CHANNEL_POOL_ITEM(prev).m_next_free == CHANNEL_FLAG_INUSE); --prev) ;
        
        if (prev > 0) {
            CHANNEL_POOL_ITEM(handle).m_next_free = CHANNEL_POOL_ITEM(prev).m_next_free;
            CHANNEL_POOL_ITEM(prev).m_next_free = handle;
        }
        else {
            CHANNEL_POOL_ITEM(handle).m_next_free = g_channel_pool.m_free_list_head;
            g_channel_pool.m_free_list_head = handle;
        }
        
        g_channel_pool.m_free_count++;
        
        pthread_mutex_unlock(&g_channel_pool.m_free_list_mutex);
    }
}

static int _channel_init(channel_t *self) {
    assert(self != NULL);
    
    if (0 == pthread_mutex_init(&self->m_mutex, NULL)) {    
        if (0 == pthread_mutex_lock(&self->m_mutex)) {
            if (0 == pthread_cond_init(&self->m_has_items, NULL)) {
                if (0 == pthread_cond_init(&self->m_has_space, NULL)) {
                    if (NULL != (self->m_items = calloc(_channel_initial_size, sizeof(anon_scalar_t)))) {
                        self->m_allocated_count = _channel_initial_size;
                        self->m_start = 0;
                        self->m_count = 0;
                        self->m_references = 0;
                        pthread_mutex_unlock(&self->m_mutex);
                        return 0;
                    }
                }
            }
        }
    }
    
    debug("couldn't initialise channel");
    return -1;
}

static int _channel_destroy(channel_t *self) { // FIXME implement this
    assert(self != NULL);
    return 0;
}

static int _channel_reserve_unlocked(channel_t *self, size_t new_size) {
    assert(self != NULL);
    assert(new_size != 0);

    if (self->m_allocated_count >= new_size)  return 0;
    
    anon_scalar_t *new_ringbuf = calloc(new_size, sizeof(*new_ringbuf));
    if (new_ringbuf == NULL)  return -1;
    
    size_t straight_count = self->m_allocated_count - self->m_start > self->m_count ? self->m_count : self->m_allocated_count - self->m_start;
    size_t rotated_count = self->m_count - straight_count;
    memcpy(&new_ringbuf[0], &self->m_items[self->m_start], straight_count * sizeof(anon_scalar_t));
    memcpy(&new_ringbuf[straight_count], &self->m_items[0], rotated_count * sizeof(anon_scalar_t));
    free(self->m_items);
    self->m_allocated_count = new_size;
    self->m_items = new_ringbuf;
    self->m_start = 0;
    return 0;
}
