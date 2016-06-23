#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <android/log.h>
#define ROUNDUP(a) \
    (((a) + 0x1000 - 1) & ~(0x1000 - 1))
#define ROUNDDOWN(a) \
    ((void*)(((uintptr_t)a) & ~(0x1000 - 1)))
#define TOHEADER(a) \
    ((struct Header*)ROUNDDOWN(a))

#define TAG "LINZJ"
#define LOGD(...) \
    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
static uintptr_t hint = 0x10000;

static intptr_t gethint(size_t s)
{
    intptr_t r;
    while (1) {
        r = __atomic_add_fetch(&hint, s, __ATOMIC_SEQ_CST);
        if (r >= 0xc0000000) {
            int expect = 0;
            __atomic_compare_exchange_n(&hint, &expect, r, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            continue;
        }
        return r;
    }
}

struct Header
{
    size_t pagesize;
    size_t contentsize;
    uintptr_t padding[2];
};

void* malloc(size_t bytes)
{
    size_t s = ROUNDUP(bytes + sizeof(struct Header));
    void* r;
    struct Header* r2;
    uintptr_t myhint;
    myhint = gethint(s);
    r = mmap((void*)myhint, s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) {
        return NULL;
    }
    r2 = (struct Header*)r;
    r2->pagesize = s;
    r2->contentsize = bytes;
    LOGD("allocated %zd bytes, %p", bytes, r2 + 1);
    return r2 + 1;
}

void free(void* data)
{
    struct Header* r2;
    if (data == NULL) {
        return;
    }
    r2 = TOHEADER(data);
    LOGD("deallocated %zd bytes, %p", r2->pagesize, r2);
    munmap(r2, r2->pagesize);
}

void* calloc(size_t n_elements, size_t elem_size)
{
    return malloc(n_elements * elem_size);
}

void* realloc(void* oldMem, size_t bytes)
{
    void* newRegion;
    struct Header* r2;
    int pagesize;
    if (oldMem == NULL) {
        return malloc(bytes);
    }
    r2 = TOHEADER(oldMem);
    pagesize = r2->pagesize;
    if (pagesize >= (bytes + sizeof(struct Header))) {
        r2->contentsize = bytes;
        return oldMem;
    }
    newRegion = malloc(bytes);
    if (!newRegion) {
        return NULL;
    }
    memcpy(newRegion, oldMem, r2->contentsize);
    free(oldMem);
    return newRegion;
}

__attribute__((visibility("default"))) void* memalign(size_t alignment, size_t bytes);
__attribute__((visibility("default"))) size_t malloc_usable_size(const void* ptr);

void* memalign(size_t alignment, size_t bytes)
{
    size_t s;
    void* r;
    struct Header* r2;
    uintptr_t myhint;
    if (alignment >= 0x1000) {
        return NULL;
    }
    alignment = alignment > sizeof(struct Header) ? alignment : sizeof(struct Header);
    s = ROUNDUP(bytes + alignment);
    myhint = gethint(s);
    r = mmap((void*)myhint, s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) {
        return NULL;
    }
    r2 = (struct Header*)r;
    r2->pagesize = s;
    r2->contentsize = s;
    LOGD("allocated %zd bytes, %p", bytes, r2);
    return (char*)r2 + alignment;
}

size_t malloc_usable_size(const void* ptr)
{
    struct Header* r2;
    r2 = TOHEADER(ptr);
    return r2->contentsize;
}

void __attribute__((constructor)) myconstructor(void);

static void resetsignal(int sig)
{
    struct sigaction myaction;
    memset(&myaction, 0, sizeof(myaction));
    myaction.sa_handler = SIG_DFL;
    sigaction(sig, &myaction, NULL);
}

static void handlerprinter(int sig, siginfo_t* info, void* context)
{
    LOGD("received signal %d, aborting.\n", sig);
    if (sig == SIGSEGV) {
        LOGD("fault address: %p", info->si_addr);
    }
    sleep(20);
    resetsignal(sig);
}

void myconstructor(void)
{
    struct sigaction myaction;
    memset(&myaction, 0, sizeof(myaction));
    myaction.sa_sigaction = handlerprinter;
    myaction.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &myaction, NULL);
    sigaction(SIGBUS, &myaction, NULL);
}
