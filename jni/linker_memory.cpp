/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "linker_allocator.h"

#include <stdlib.h>
#include <pthread.h>

static LinkerMemoryAllocator& get_global() {
    static LinkerMemoryAllocator g_linker_allocator;
    return g_linker_allocator;
}

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

class AutoLock
{
public:
    explicit AutoLock(pthread_mutex_t&);
    ~AutoLock();
private:
    pthread_mutex_t& mutex_;
};

AutoLock::AutoLock(pthread_mutex_t& mutex)
    : mutex_(mutex)
{
    pthread_mutex_lock(&mutex);
}

AutoLock::~AutoLock()
{
    pthread_mutex_unlock(&mutex_);
}


void* malloc(size_t byte_count) {
  AutoLock lock(g_lock);
  return get_global().alloc(byte_count);
}

void* calloc(size_t item_count, size_t item_size) {
  AutoLock lock(g_lock);
  return get_global().alloc(item_count*item_size);
}

void* realloc(void* p, size_t byte_count) {
  AutoLock lock(g_lock);
  return get_global().realloc(p, byte_count);
}

void free(void* ptr) {
  AutoLock lock(g_lock);
  get_global().free(ptr);
}

void* memalign(size_t alignment, size_t bytes)
{
  if (alignment <= 16) {
      return malloc(bytes);
  }
  AutoLock lock(g_lock);
  void* ptr = get_global().alloc_mmap(bytes + alignment);
  uintptr_t ptrint = reinterpret_cast<uintptr_t>(ptr);
  ptrint = (ptrint + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<void*>(ptrint);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void* result = memalign(alignment, size);
    if (result == NULL) {
        return ENOMEM;
    }
    *memptr = result;
    return 0;
}
