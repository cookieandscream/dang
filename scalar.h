/*
 *  scalar.h
 *  dang
 *
 *  Created by Ellie on 8/10/10.
 *  Copyright 2010 Ellie. All rights reserved.
 *
 */


#ifndef SCALAR_H
#define SCALAR_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define SCALAR_UNALLOC      0x00u
#define SCALAR_INT          0x01u
#define SCALAR_FLOAT        0x02u
#define SCALAR_STRING       0x03u
// ...
#define SCALAR_UNDEF        0xFFu
#define SCALAR_TYPE_MASK    0xFFu

#define SCALAR_FLAG_PTR     0x40000000u
#define SCALAR_FLAG_SHARED  0x80000000u

typedef struct scalar_t {
    uint32_t m_flags;
    union {
        intptr_t as_int;
        float    as_float;
        char     *as_string;
    } m_value;    
} scalar_t;

typedef struct pooled_scalar_t {
    uint32_t m_flags;
    uint32_t m_references;
    union {
        intptr_t as_int;
        float    as_float;
        char     *as_string;
        intptr_t next_free;
    } m_value;
    pthread_mutex_t *m_mutex;
} pooled_scalar_t;

typedef struct scalar_pool_t {
    size_t m_allocated_count;
    size_t m_count;
    pooled_scalar_t *m_items;
    size_t m_free_count;
    intptr_t m_free_list_head;
    pthread_mutex_t m_free_list_mutex;
} scalar_pool_t;

typedef size_t scalar_handle_t;

int scalar_pool_init(void);
int scalar_pool_destroy(void);

scalar_handle_t scalar_pool_allocate_scalar(uint32_t);
void scalar_pool_release_scalar(scalar_handle_t);
void scalar_pool_increase_refcount(scalar_handle_t);


void scalar_reset(scalar_handle_t);

void scalar_set_int_value(scalar_handle_t, intptr_t);
void scalar_set_float_value(scalar_handle_t, float);
void scalar_set_string_value(scalar_handle_t, const char *);

intptr_t scalar_get_int_value(scalar_handle_t);
float scalar_get_float_value(scalar_handle_t);
void scalar_get_string_value(scalar_handle_t, char **);


#endif
