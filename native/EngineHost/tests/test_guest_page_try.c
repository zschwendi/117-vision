/* guest_page_try_alloc: the non-fatal entry to the production guest page
 * allocator. Build from native/EngineHost:
 * clang -O2 -I../EngineReuse -ffunction-sections -fdata-sections
 *   tests/test_guest_page_try.c -Wl,-dead_strip -o /tmp/test_guest_page_try
 * A failed try changes no allocator state; a successful one is the same
 * zeroed, exactly owned run guest_page_alloc returns; the fatal entry keeps
 * its exit; and concurrent try/fatal/free churn never shares a page. */
#include "../host.c"
#include <assert.h>
#include <stdatomic.h>

static void expect_fatal(uint32_t bytes) {
    host_is_main_context = 1;
    host_escape_ready = 1;
    int how = setjmp(host_escape);
    if (!how) { guest_page_alloc(bytes); assert(!"guest_page_alloc must stay fatal"); }
    assert(how == 2 && host_exit_code == 3);
    host_escape_ready = 0;
}
typedef struct { uint32_t allocations[PAGE_COUNT]; uint32_t live, top; } PageState;
static PageState *state_now(void) {
    static PageState snapshot;
    pthread_mutex_lock(&page_lock);
    memcpy(snapshot.allocations, page_allocations, sizeof page_allocations);
    snapshot.live = page_live_granules; snapshot.top = page_top;
    pthread_mutex_unlock(&page_lock);
    return &snapshot;
}
static void expect_try_fails(uint32_t bytes) {
    static PageState before; before = *state_now();
    assert(guest_page_try_alloc(bytes) == 0);
    PageState *after = state_now();
    assert(!memcmp(before.allocations, after->allocations, sizeof before.allocations));
    assert(before.live == after->live && before.top == after->top);
}

static _Atomic int churn_failures;
static void *churn(void *opaque) {
    uint32_t tag = (uint32_t)(uintptr_t)opaque;
    for (unsigned i = 0; i < 1500; i++) {
        /* Alternate the entries; large tries may legitimately fail while
         * the space is crowded, small ones never do here. */
        uint32_t size = i % 3 == 0 ? 96u * 1024u * 1024u : 70000u;
        uint32_t p = (i & 1) ? guest_page_try_alloc(size) : (size > 70000u ? guest_page_try_alloc(size) : guest_page_alloc(size));
        if (!p) { assert(size > 70000u); atomic_fetch_add(&churn_failures, 1); continue; }
        assert((p & 0xffffu) == 0 && G32(p) == 0 && G32(p + 131068) == 0);
        S32(p, tag); S32(p + 131068, tag);
        sched_yield();
        assert(G32(p) == tag && G32(p + 131068) == tag);   /* nobody else owns these pages */
        assert(guest_page_free(p));
    }
    return NULL;
}

int main(void) {
    engine_flat_base = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(engine_flat_base != MAP_FAILED);
    assert(guest_page_try_alloc(0) == 0);
    expect_try_fails(UINT32_MAX);                    /* rounding cannot wrap */
    expect_try_fails(PAGE_END - PAGE_START + 1u);

    /* Same placement, zeroing and exact ownership as the fatal entry. */
    uint32_t a = guest_page_try_alloc(65537), b = guest_page_alloc(1);
    assert(a == PAGE_START && b == a + 131072);
    memset(GPTR(a), 0xa5, 131072);
    assert(guest_page_free(a));
    uint32_t again = guest_page_try_alloc(131072);
    assert(again == a);
    for (unsigned i = 0; i < 131072; i++) assert(G8(a + i) == 0);   /* recycled pages arrive zeroed */
    assert(guest_page_free(again) && guest_page_free(b));

    /* The exact capacity edge: fill all but three granules. */
    const uint32_t total = PAGE_END - PAGE_START, gran = PAGE_GRANULARITY;
    uint32_t big = guest_page_try_alloc(total - 3u * gran);
    assert(big == PAGE_START);
    expect_try_fails(4u * gran);
    expect_try_fails(3u * gran + 1u);
    uint32_t edge = guest_page_try_alloc(3u * gran);
    assert(edge == PAGE_START + total - 3u * gran && G32(edge) == 0);
    expect_try_fails(1);
    /* The fatal entry still exits on the same exhaustion. */
    expect_fatal(1);
    expect_fatal(UINT32_MAX);
    assert(guest_page_free(edge));
    /* A failed try in a fragmented space leaves the free hole usable. */
    expect_try_fails(4u * gran);
    edge = guest_page_try_alloc(2u * gran);
    assert(edge == PAGE_START + total - 3u * gran);
    assert(guest_page_free(edge) && guest_page_free(big));
    for (uint32_t i = 0; i < PAGE_COUNT; i++) assert(page_allocations[i] == 0);

    /* Concurrent churn across both entries, with large tries that fail when
     * the space is crowded: no page is ever shared, nothing leaks. */
    pthread_t workers[8];
    for (unsigned i = 0; i < 8; i++) assert(!pthread_create(workers + i, NULL, churn, (void *)(uintptr_t)(0x1000u + i)));
    for (unsigned i = 0; i < 8; i++) assert(!pthread_join(workers[i], NULL));
    for (uint32_t i = 0; i < PAGE_COUNT; i++) assert(page_allocations[i] == 0);
    assert(page_live_granules == 0);
    assert(!munmap(engine_flat_base, GUEST_SIZE));
    printf("PASS: guest_page_try_alloc fails without changing allocator state (wrap, oversize, exact edge, fragmented), returns zeroed exactly owned runs, keeps guest_page_alloc fatal, and survives try/fatal/free churn (%d crowded large tries refused)\n",
           atomic_load(&churn_failures));
    return 0;
}
