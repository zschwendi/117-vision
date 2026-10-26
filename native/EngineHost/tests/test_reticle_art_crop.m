/* Real Metal validation: capture pixels, transparent crop edges, original
 * target preservation and the shared command-buffer completion boundary. */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main
static _Atomic int finished; static int complete_ok;
static void complete(void *arg,int ok){assert(arg==&finished);complete_ok=ok;atomic_store_explicit(&finished,1,memory_order_release);}
int main(void){@autoreleasepool {
    mr_context *c=mr_create(16,12);if(!c){puts("SKIP no Metal");return 77;}
    uint32_t source[16*12];for(int y=0;y<12;y++)for(int x=0;x<16;x++)source[y*16+x]=0x80000000u|(y<<8)|x;
    assert(!mr_write_framebuffer(c,source,sizeof source));
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:8 height:8 mipmapped:NO];
    d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    id<MTLTexture> a=[c->s->dev newTextureWithDescriptor:d],b=[c->s->dev newTextureWithDescriptor:d];assert(a&&b);
    assert(!mr_blit_target_region_to(c,(__bridge void*)a,4,3));
    assert(!mr_blit_target_region_to(c,(__bridge void*)b,-2,-3));
    assert(c->s->pending_cb);id<MTLCommandBuffer> submitted=c->s->pending_cb;
    mr_commit_async(complete,&finished);[submitted waitUntilCompleted];
    /* Completion handlers may finish just after waitUntilCompleted. */
    for(unsigned n=0;!atomic_load_explicit(&finished,memory_order_acquire)&&n<1000;n++)usleep(1000);assert(atomic_load_explicit(&finished,memory_order_acquire)&&complete_ok);
    uint32_t pa[64],pb[64],original[16*12];
    [a getBytes:pa bytesPerRow:32 fromRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0];
    [b getBytes:pb bytesPerRow:32 fromRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0];
    for(int y=0;y<8;y++)for(int x=0;x<8;x++){
        assert(pa[y*8+x]==source[(y+3)*16+x+4]);
        assert(pb[y*8+x]==(x<2||y<3?0:source[(y-3)*16+x-2]));
    }
    assert(!mr_read_framebuffer(c,original,sizeof original));assert(!memcmp(original,source,sizeof source));
    /* A new crop clears pixels left over from a previous weapon. */
    assert(!mr_blit_target_region_to(c,(__bridge void*)a,15,11));
    assert(!mr_read_framebuffer(c,original,sizeof original));
    [a getBytes:pa bytesPerRow:32 fromRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0];
    assert(pa[0]==source[191]);for(int i=1;i<64;i++)assert(pa[i]==0);
    assert(mr_blit_target_region_to(c,(__bridge void*)a,-1025,0)==MR_ERR_ARGS);
    mr_destroy(c);puts("PASS original BGRA bytes and alpha, two independent crops, transparent bounds, old-weapon clearing, target preservation and shared GPU completion");
}}
