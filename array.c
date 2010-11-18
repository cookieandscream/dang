/*
 *  array.c
 *  dang
 *
 *  Created by Ellie on 26/09/10.
 *  Copyright 2010 Ellie. All rights reserved.
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "scalar.h"

#include "array.h"

#define ARRAY(handle)   POOL_OBJECT(array_t, handle)

POOL_SOURCE_CONTENTS(array_t);

static const size_t _array_initial_reserve_size = 16;

static int _array_reserve_unlocked(array_t *, size_t);
static int _array_grow_back_unlocked(array_t *, size_t);
static int _array_grow_front_unlocked(array_t *, size_t);

static inline int _array_lock(array_handle_t);
static inline int _array_unlock(array_handle_t);

/*
=head1 NAME

array

=head1 INTRODUCTION

=head1 PUBLIC INTERFACE

=over

=cut
*/

/*
=item array_pool_init()

=item array_pool_destroy()

Setup and teardown functions for the array pool.

=cut
 */
int array_pool_init(void) {
    return POOL_INIT(array_t);
}

int array_pool_destroy(void) {
    return POOL_DESTROY(array_t);
}

/*
=item array_allocate()

=item array_allocate_many()

=item array_reference()

=item array_release()

Functions for managing allocation of arrays.

=cut
 */
array_handle_t array_allocate(flags_t flags) {
    return POOL_ALLOCATE(array_t, flags);
}

array_handle_t array_allocate_many(size_t n, flags_t flags) {
    return POOL_ALLOCATE_MANY(array_t, n, flags);
}

array_handle_t array_reference(array_handle_t handle) {
    return POOL_REFERENCE(array_t, handle);
}

int array_release(array_handle_t handle) {
    return POOL_RELEASE(array_t, handle);
}

/*
=item array_size()

Returns the number of items in the array.

=cut
*/
size_t array_size(array_handle_t handle) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    if (0 == _array_lock(handle)) {
        size_t size = ARRAY(handle).m_count;
        _array_unlock(handle);
        return size;
    }
    else {
        debug("couldn't lock array handle %"PRIuPTR"\n", handle);
        return 0;
    }
}

/*
=item array_item_at()

Gets a handle to the item in the array at a given index.  If the index is beyond the current 
bounds of the array, the array is grown to accommodate the index, with new items being set
to undefined.

Returns a handle to the item, or 0 on error.  The caller must release the handle when they are
done with it.

=cut
*/
scalar_handle_t array_item_at(array_handle_t handle, size_t index) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    if (0 == _array_lock(handle)) {
        if (index >= ARRAY(handle).m_first + ARRAY(handle).m_count) {
            if (0 != _array_reserve_unlocked(&ARRAY(handle), index + 1))  return 0;
            
            size_t need = index - (ARRAY(handle).m_first + ARRAY(handle).m_count - 1);
            scalar_handle_t handle = scalar_allocate_many(need, 0);  FIXME("handle flags\n");
            
            while (ARRAY(handle).m_count <= index - ARRAY(handle).m_first) {
                ARRAY(handle).m_items[ARRAY(handle).m_first + ARRAY(handle).m_count++] = handle + 1;
                ++handle;
            }
        }

        scalar_handle_t s = scalar_reference(ARRAY(handle).m_items[ARRAY(handle).m_first + index]);
        _array_unlock(handle);
        return s;
    }
    else {
        return 0;
    }
}

/*
=item array_slice()

Takes an array of n scalar elements, each containing an index.  Replaces each element with a reference to
the item in the array at that index.  Negative indices are treated as starting from the end of the array,
thus -1 is the last item in the array.

If an index specified is greater that the current size of the array, the array is grown to accomodate it,
with the new items added set to undefined.

If the array of elements consists of both negative indices and indices that cause the array to grow, the
behaviour of the negative indices is undefined.

=cut
*/
int array_slice(array_handle_t handle, struct scalar_t *elements, size_t n) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    
    if (n == 0)  return 0;
    
    if (0 == _array_lock(handle)) {
        assert(POOL_HANDLE_IN_USE(array_t, handle));
        for (size_t i = 0; i < n; i++) {
            int index = anon_scalar_get_int_value(&elements[i]);
            
            // grow if out of range
            if (index >= ARRAY(handle).m_first + ARRAY(handle).m_count) {
                if (0 != _array_reserve_unlocked(&ARRAY(handle), index + 1))  return 0;
                
                size_t need = index - (ARRAY(handle).m_first + ARRAY(handle).m_count - 1);
                scalar_handle_t handle = scalar_allocate_many(need, 0);  FIXME("handle flags\n");
                
                while (ARRAY(handle).m_count <= index - ARRAY(handle).m_first) {
                    ARRAY(handle).m_items[ARRAY(handle).m_first + ARRAY(handle).m_count++] = handle + 1;
                    ++handle;
                }
            }

            // negative indices are relative to the end
            if (index < 0)  index += ARRAY(handle).m_count;

            anon_scalar_set_scalar_reference(&elements[i], ARRAY(handle).m_items[ARRAY(handle).m_first + index]);
        }
        _array_unlock(handle);
        return 0;
    }
    else {
        debug("couldn't lock array handle %"PRIuPTR"\n", handle);
        return -1;
    }
}

/*
=item array_list()

...

=cut
*/
int array_list(array_handle_t, struct scalar_t **restrict, size_t *restrict);

/*
=item array_fill()

...

=cut
*/
int array_fill(array_handle_t, const struct scalar_t *, size_t);

/*
=item array_push()

Adds an item at the end of the array.

=cut
*/
int array_push(array_handle_t handle, const scalar_t *value) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    if (0 == _array_lock(handle)) {
        if (ARRAY(handle).m_first + ARRAY(handle).m_count == ARRAY(handle).m_allocated_count) {
            if (0 != _array_grow_back_unlocked(&ARRAY(handle), ARRAY(handle).m_count))  return -1;
        }

        scalar_handle_t s = scalar_allocate(0); FIXME("handle flags\n");
        scalar_set_value(s, value);
        ARRAY(handle).m_items[ARRAY(handle).m_count++] = s;
        _array_unlock(handle);
        return 0;
    }
    else {
        return -1;
    }
}

/*
=item array_unshift()

Adds an item at the start of the array.

=cut
*/
int array_unshift(array_handle_t handle, const scalar_t *value) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    int status = 0;
    if (0 == _array_lock(handle)) {
        scalar_handle_t s = scalar_allocate(0);  FIXME("handle flags\n");
        scalar_set_value(s, value);

        if (ARRAY(handle).m_count == 0) {
            ARRAY(handle).m_items[ARRAY(handle).m_first] = s;
            ++ARRAY(handle).m_count;
        }
        else {
            if (ARRAY(handle).m_first == 0) {
                if (0 != _array_grow_front_unlocked(&ARRAY(handle), ARRAY(handle).m_count)) {
                    scalar_release(s);
                    status = -1;
                }
            }
            
            ARRAY(handle).m_items[--ARRAY(handle).m_first] = s;
            ++ARRAY(handle).m_count;
        }
        _array_unlock(handle);
    }
    else {
        status = -1;
    }
    
    return status;
}

/*
=item array_pop()

Removes an item from the end of the array and returns it.  The caller must release the returned item when they are
done with it.

=cut
*/
int array_pop(array_handle_t handle, scalar_t *result) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    int status = 0;
    
    if (0 == _array_lock(handle)) {
        if (ARRAY(handle).m_count > 0) {
            scalar_handle_t s = ARRAY(handle).m_items[ARRAY(handle).m_first + --ARRAY(handle).m_count];
            if (result != NULL)  scalar_get_value(s, result);
            scalar_release(s);
            status = 0;
        }
        else {
            status = -1;
        }
        _array_unlock(handle);
    }
    else {
        status = -1;
    }
    
    return status;
}

/*
=item array_shift()

Removes an item from the start of the array and returns it.  The caller must release the returned itme when they're
done with it.

=cut
*/
int array_shift(array_handle_t handle, scalar_t *result) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    assert(POOL_HANDLE_IN_USE(array_t, handle));
    
    int status = 0;

    if (0 == _array_lock(handle)) {
        if (ARRAY(handle).m_count > 0) {
            --ARRAY(handle).m_count;
            scalar_handle_t s = ARRAY(handle).m_items[ARRAY(handle).m_first++];
            if (result != NULL)  scalar_get_value(s, result);
            scalar_release(s);
            status = 0;
        }
        else {
            status = -1;
        }
        _array_unlock(handle);
    }
    else {
        status = -1;
    }
    
    return status;
}

/*
=back

=head1 PRIVATE INTERFACE

=over

=cut
*/

/*
=item _array_lock()

=item _array_unlock()

Lock and unlock arrays

=cut
 */
static inline int _array_lock(array_handle_t handle) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    return POOL_LOCK(array_t, handle);
}

static inline int _array_unlock(array_handle_t handle) {
    assert(POOL_HANDLE_VALID(array_t, handle));
    return POOL_UNLOCK(array_t, handle);
}

/*
=item _array_init()

=item _array_destroy()

Setup and teardown functions for array_t objects

=cut
 */
int _array_init(array_t *self) {
    assert(self != NULL);
    
    memset(self, 0, sizeof(*self));
    self->m_items = calloc(_array_initial_reserve_size, sizeof(*self->m_items));
    
    if (self->m_items != NULL) {
        self->m_allocated_count = _array_initial_reserve_size;
        return 0;
    }
    else {
        return -1;
    }
}

int _array_destroy(array_t *self) {
    assert(self != NULL);
    
    for (size_t i = self->m_first; i < self->m_first + self->m_count; i++) {
        scalar_release(self->m_items[i]);
    }
    free(self->m_items);
    
    memset(self, 0, sizeof(*self));
    return 0;
}

/*
=item _array_reserve_unlocked()

Ensure an array has space for a certain number of items.  Additional space, if any, is allocated at the end of the array.

=cut
 */
static int _array_reserve_unlocked(array_t *self, size_t target_size) {
    assert(self != NULL);
    
    size_t effective_size = self->m_allocated_count - self->m_first;
    
    if (effective_size < target_size) {
        return _array_grow_back_unlocked(self, target_size - effective_size);
    }
    else {
        return 0;
    }
}

/*
=item _array_grow_back_unlocked()

Allocates space for an additional n items at the end of the current allocation.

=cut
*/
static int _array_grow_back_unlocked(array_t *self, size_t n) {
    assert(self != NULL);
    scalar_handle_t *new_items = calloc(self->m_allocated_count + n, sizeof(*new_items));
    if (new_items != NULL) {
        memcpy(new_items, self->m_items, self->m_allocated_count * sizeof(*self->m_items));
        scalar_handle_t *tmp = self->m_items;
        self->m_items = new_items;
        self->m_allocated_count += n;
        free(tmp);
        return 0;
    }
    else {
        return -1;
    }
}

/*
=item _array_grow_front_unlocked()

Allocates space for an additional n items at the front of the current allocation.

=cut
*/
static int _array_grow_front_unlocked(array_t *self, size_t n) {
    assert(self != NULL);
    scalar_handle_t *new_items = calloc(self->m_allocated_count + n, sizeof(*new_items));
    if (new_items != NULL) {
        memcpy(&new_items[n], self->m_items, self->m_allocated_count * sizeof(*self->m_items));
        scalar_handle_t *tmp = self->m_items;
        self->m_items = new_items;
        self->m_first += n;
        self->m_allocated_count += n;
        free(tmp);
        return 0;
    }
    else {
        return -1;
    }
}

/*
=back

=cut
*/
