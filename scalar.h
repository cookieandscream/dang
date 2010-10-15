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

#include "floatptr_t.h"

#define SCALAR_UNALLOC      0x00u
#define SCALAR_INT          0x01u
#define SCALAR_FLOAT        0x02u
#define SCALAR_STRING       0x03u
// ...
#define SCALAR_SCAREF       0x81u
#define SCALAR_ARRREF       0x82u
#define SCALAR_HASHREF      0x83u

#define SCALAR_UNDEF        0xFFu
#define SCALAR_TYPE_MASK    0xFFu

#define SCALAR_FLAG_REF     0x80u
#define SCALAR_FLAG_PTR     0x40000000u
#define SCALAR_FLAG_SHARED  0x80000000u

#define SCALAR_GUTS             \
    uint32_t m_flags;           \
    union {                     \
        intptr_t as_int;        \
        floatptr_t as_float;    \
        char     *as_string;    \
        intptr_t next_free;     \
    } m_value

typedef struct scalar_t {
    SCALAR_GUTS;
} anon_scalar_t;

typedef struct pooled_scalar_t {
    SCALAR_GUTS;
    uint32_t m_references;
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

typedef uintptr_t scalar_handle_t;

/*
=head1 scalar.h

=over
 
=item scalar_pool_init()

=item scalar_pool_destroy()

Scalar pool setup and teardown functions

=cut
 */
int scalar_pool_init(void);
int scalar_pool_destroy(void);

/*
=item handle scalar_pool_allocate_scalar(int flags)

=item scalar_pool_release_scalar(handle)

=item scalar_pool_increase_refcount(handle)

Functions for managing allocation of scalars

=cut
 */
scalar_handle_t scalar_pool_allocate_scalar(uint32_t);
void scalar_pool_release_scalar(scalar_handle_t);
void scalar_pool_increase_refcount(scalar_handle_t);

/*
=item scalar_reset(handle)

Resets pooled scalar back to "undefined" state

=cut
 */
void scalar_reset(scalar_handle_t);

/*
=item scalar_set_int_value(handle, value)
 
=item scalar_set_float_value(handle, value)

=item scalar_set_string_value(handle, value)

=item scalar_set_value(handle, value)

Functions for setting values on a pooled scalar

=cut
 */
void scalar_set_int_value(scalar_handle_t, intptr_t);
void scalar_set_float_value(scalar_handle_t, floatptr_t);
void scalar_set_string_value(scalar_handle_t, const char *);
void scalar_set_value(scalar_handle_t, const anon_scalar_t *);

/*
=item scalar_get_int_value(handle)

=item scalar_get_float_value(handle)
 
=item scalar_get_string_value(handle, result)

=item scalar_get_value(handle, result)

Functions for getting values from a pooled scalar

=cut
 */
intptr_t scalar_get_int_value(scalar_handle_t);
floatptr_t scalar_get_float_value(scalar_handle_t);
void scalar_get_string_value(scalar_handle_t, char **);
void scalar_get_value(scalar_handle_t, anon_scalar_t *);

/*
=item anon_scalar_init(anon_scalar)

=item anon_scalar_destroy(anon_scalar)
 
Setup and teardown functions for anon_scalar_t objects

=cut
 */
void anon_scalar_init(anon_scalar_t *);
void anon_scalar_destroy(anon_scalar_t *);

/*
=item anon_scalar_clone(clone, original)

Deep-copy clone of an anon_scalar_t object.  The resulting clone needs to be destroyed independently of the original.

=cut
 */
void anon_scalar_clone(anon_scalar_t * restrict, const anon_scalar_t * restrict);

/*
=item anon_scalar_assign(dest, original)

Shallow-copy of an anon_scalar_t object.  Only one of dest and original should be destroyed.

=cut
 */
void anon_scalar_assign(anon_scalar_t * restrict, const anon_scalar_t * restrict);

/*
=item anon_scalar_set_int_value(anon_scalar, value)

=item anon_scalar_set_float_value(anon_scalar, value)
 
=item anon_scalar_set_string_value(anon_scalar, value)

Functions for setting the value of anon_scalar_t objects
 
=cut
 */
void anon_scalar_set_int_value(anon_scalar_t *, intptr_t);
void anon_scalar_set_float_value(anon_scalar_t *, floatptr_t);
void anon_scalar_set_string_value(anon_scalar_t *, const char *);

/*
=item anon_scalar_get_int_value(anon_scalar)
 
=item anon_scalar_get_float_value(anon_scalar)
 
=item anon_scalar_get_string_value(anon_scalar, result)

Functions for getting values from anon_scalar_t objects

=cut
 */
intptr_t anon_scalar_get_int_value(const anon_scalar_t *);
floatptr_t anon_scalar_get_float_value(const anon_scalar_t *);
void anon_scalar_get_string_value(const anon_scalar_t *, char **);


#endif
/*
=back

=cut
 */
