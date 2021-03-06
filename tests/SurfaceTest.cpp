/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkCanvas.h"
#include "SkData.h"
#include "SkDevice.h"
#include "SkImage_Base.h"
#include "SkPath.h"
#include "SkRRect.h"
#include "SkSurface.h"
#include "SkUtils.h"
#include "Test.h"

#if SK_SUPPORT_GPU
#include "GrContext.h"
#include "GrGpu.h"
#endif

#include <initializer_list>

static void release_direct_surface_storage(void* pixels, void* context) {
    SkASSERT(pixels == context);
    sk_free(pixels);
}
static SkSurface* create_surface(SkAlphaType at = kPremul_SkAlphaType,
                                 SkImageInfo* requestedInfo = nullptr) {
    const SkImageInfo info = SkImageInfo::MakeN32(10, 10, at);
    if (requestedInfo) {
        *requestedInfo = info;
    }
    return SkSurface::NewRaster(info);
}
static SkSurface* create_direct_surface(SkAlphaType at = kPremul_SkAlphaType,
                                        SkImageInfo* requestedInfo = nullptr) {
    const SkImageInfo info = SkImageInfo::MakeN32(10, 10, at);
    if (requestedInfo) {
        *requestedInfo = info;
    }
    const size_t rowBytes = info.minRowBytes();
    void* storage = sk_malloc_throw(info.getSafeSize(rowBytes));
    return SkSurface::NewRasterDirectReleaseProc(info, storage, rowBytes,
                                                 release_direct_surface_storage,
                                                 storage);
}
#if SK_SUPPORT_GPU
static SkSurface* create_gpu_surface(GrContext* context, SkAlphaType at = kPremul_SkAlphaType,
                                     SkImageInfo* requestedInfo = nullptr) {
    const SkImageInfo info = SkImageInfo::MakeN32(10, 10, at);
    if (requestedInfo) {
        *requestedInfo = info;
    }
    return SkSurface::NewRenderTarget(context, SkSurface::kNo_Budgeted, info, 0, nullptr);
}
static SkSurface* create_gpu_scratch_surface(GrContext* context,
                                             SkAlphaType at = kPremul_SkAlphaType,
                                             SkImageInfo* requestedInfo = nullptr) {
    const SkImageInfo info = SkImageInfo::MakeN32(10, 10, at);
    if (requestedInfo) {
        *requestedInfo = info;
    }
    return SkSurface::NewRenderTarget(context, SkSurface::kYes_Budgeted, info, 0, nullptr);
}
#endif

DEF_TEST(SurfaceEmpty, reporter) {
    const SkImageInfo info = SkImageInfo::Make(0, 0, kN32_SkColorType, kPremul_SkAlphaType);
    REPORTER_ASSERT(reporter, nullptr == SkSurface::NewRaster(info));
    REPORTER_ASSERT(reporter, nullptr == SkSurface::NewRasterDirect(info, nullptr, 0));

}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceEmpty_Gpu, reporter, context) {
    const SkImageInfo info = SkImageInfo::Make(0, 0, kN32_SkColorType, kPremul_SkAlphaType);
    REPORTER_ASSERT(reporter, nullptr ==
                    SkSurface::NewRenderTarget(context, SkSurface::kNo_Budgeted, info, 0, nullptr));
}
#endif

#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceWrappedTexture, reporter, context) {
    const GrGpu* gpu = context->getGpu();
    if (!gpu) {
        return;
    }

    // Test the wrapped factory for SkSurface by creating a backend texture and then wrap it in
    // a SkSurface.
    static const int kW = 100;
    static const int kH = 100;
    static const uint32_t kOrigColor = 0xFFAABBCC;
    SkAutoTArray<uint32_t> pixels(kW * kH);
    sk_memset32(pixels.get(), kOrigColor, kW * kH);
    GrBackendObject texHandle = gpu->createTestingOnlyBackendTexture(pixels.get(), kW, kH,
                                                                     kRGBA_8888_GrPixelConfig);

    GrBackendTextureDesc wrappedDesc;
    wrappedDesc.fConfig = kRGBA_8888_GrPixelConfig;
    wrappedDesc.fWidth = kW;
    wrappedDesc.fHeight = kH;
    wrappedDesc.fOrigin = kBottomLeft_GrSurfaceOrigin;
    wrappedDesc.fSampleCnt = 0;
    wrappedDesc.fFlags = kRenderTarget_GrBackendTextureFlag;
    wrappedDesc.fTextureHandle = texHandle;

    SkAutoTUnref<SkSurface> surface(
        SkSurface::NewWrappedRenderTarget(context, wrappedDesc, nullptr));
    REPORTER_ASSERT(reporter, surface);
    if (surface) {
        // Validate that we can draw to the canvas and that the original texture color is preserved
        // in pixels that aren't rendered to via the surface.
        SkPaint paint;
        static const SkColor kRectColor = ~kOrigColor | 0xFF000000;
        paint.setColor(kRectColor);
        surface->getCanvas()->drawRect(SkRect::MakeWH(SkIntToScalar(kW), SkIntToScalar(kH)/2),
                                       paint);
        SkImageInfo readInfo = SkImageInfo::MakeN32Premul(kW, kH);
        surface->readPixels(readInfo, pixels.get(), kW * sizeof(uint32_t), 0, 0);
        bool stop = false;
        SkPMColor origColorPM = SkPackARGB32((kOrigColor >> 24 & 0xFF),
                                             (kOrigColor >>  0 & 0xFF),
                                             (kOrigColor >>  8 & 0xFF),
                                             (kOrigColor >> 16 & 0xFF));
        SkPMColor rectColorPM = SkPackARGB32((kRectColor >> 24 & 0xFF),
                                             (kRectColor >> 16 & 0xFF),
                                             (kRectColor >>  8 & 0xFF),
                                             (kRectColor >>  0 & 0xFF));
        for (int y = 0; y < kH/2 && !stop; ++y) {
            for (int x = 0; x < kW && !stop; ++x) {
                REPORTER_ASSERT(reporter, rectColorPM == pixels[x + y * kW]);
                if (rectColorPM != pixels[x + y * kW]) {
                    stop = true;
                }
            }
        }
        stop = false;
        for (int y = kH/2; y < kH && !stop; ++y) {
            for (int x = 0; x < kW && !stop; ++x) {
                REPORTER_ASSERT(reporter, origColorPM == pixels[x + y * kW]);
                if (origColorPM != pixels[x + y * kW]) {
                    stop = true;
                }
            }
        }
    }
    gpu->deleteTestingOnlyBackendTexture(texHandle);
}
#endif

static void test_canvas_peek(skiatest::Reporter* reporter,
                             SkSurface* surface,
                             const SkImageInfo& requestInfo,
                             bool expectPeekSuccess) {
    const SkColor color = SK_ColorRED;
    const SkPMColor pmcolor = SkPreMultiplyColor(color);
    SkImageInfo info;
    size_t rowBytes;
    surface->getCanvas()->clear(color);

    const void* addr = surface->getCanvas()->peekPixels(&info, &rowBytes);
    bool success = SkToBool(addr);
    REPORTER_ASSERT(reporter, expectPeekSuccess == success);

    SkImageInfo info2;
    size_t rb2;
    const void* addr2 = surface->peekPixels(&info2, &rb2);

    if (success) {
        REPORTER_ASSERT(reporter, requestInfo == info);
        REPORTER_ASSERT(reporter, requestInfo.minRowBytes() <= rowBytes);
        REPORTER_ASSERT(reporter, pmcolor == *(const SkPMColor*)addr);

        REPORTER_ASSERT(reporter, addr2 == addr);
        REPORTER_ASSERT(reporter, info2 == info);
        REPORTER_ASSERT(reporter, rb2 == rowBytes);
    } else {
        REPORTER_ASSERT(reporter, nullptr == addr2);
    }
}
DEF_TEST(SurfaceCanvasPeek, reporter) {
    for (auto& surface_func : { &create_surface, &create_direct_surface }) {
        SkImageInfo requestInfo;
        SkAutoTUnref<SkSurface> surface(surface_func(kPremul_SkAlphaType, &requestInfo));
        test_canvas_peek(reporter, surface, requestInfo, true);
    }
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceCanvasPeek_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkImageInfo requestInfo;
        SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType, &requestInfo));
        test_canvas_peek(reporter, surface, requestInfo, false);
    }
}
#endif

// For compatibility with clients that still call accessBitmap(), we need to ensure that we bump
// the bitmap's genID when we draw to it, else they won't know it has new values. When they are
// exclusively using surface/image, and we can hide accessBitmap from device, we can remove this
// test.
void test_access_pixels(skiatest::Reporter* reporter, SkSurface* surface) {
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(0);

    SkBaseDevice* device = canvas->getDevice_just_for_deprecated_compatibility_testing();
    SkBitmap bm = device->accessBitmap(false);
    uint32_t genID0 = bm.getGenerationID();
    // Now we draw something, which needs to "dirty" the genID (sorta like copy-on-write)
    canvas->drawColor(SK_ColorBLUE);
    // Now check that we get a different genID
    uint32_t genID1 = bm.getGenerationID();
    REPORTER_ASSERT(reporter, genID0 != genID1);
}
DEF_TEST(SurfaceAccessPixels, reporter) {
    for (auto& surface_func : { &create_surface, &create_direct_surface }) {
        SkAutoTUnref<SkSurface> surface(surface_func(kPremul_SkAlphaType, nullptr));
        test_access_pixels(reporter, surface);
    }
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceAccessPixels_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType, nullptr));
        test_access_pixels(reporter, surface);
    }
}
#endif

static void test_snapshot_alphatype(skiatest::Reporter* reporter, SkSurface* surface,
                                    bool expectOpaque) {
    REPORTER_ASSERT(reporter, surface);
    if (surface) {
        SkAutoTUnref<SkImage> image(surface->newImageSnapshot());
        REPORTER_ASSERT(reporter, image);
        if (image) {
            REPORTER_ASSERT(reporter, image->isOpaque() == SkToBool(expectOpaque));
        }
    }
}
DEF_TEST(SurfaceSnapshotAlphaType, reporter) {
    for (auto& surface_func : { &create_surface, &create_direct_surface }) {
        for (auto& isOpaque : { true, false }) {
            SkAlphaType alphaType = isOpaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
            SkAutoTUnref<SkSurface> surface(surface_func(alphaType, nullptr));
            test_snapshot_alphatype(reporter, surface, isOpaque);
        }
    }
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceSnapshotAlphaType_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        for (auto& isOpaque : { true, false }) {
            SkAlphaType alphaType = isOpaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
            SkAutoTUnref<SkSurface> surface(surface_func(context, alphaType, nullptr));
            test_snapshot_alphatype(reporter, surface, isOpaque);
        }
    }
}
#endif

static GrBackendObject get_surface_backend_texture_handle(
    SkSurface* s, SkSurface::BackendHandleAccess a) {
    return s->getTextureHandle(a);
}
static GrBackendObject get_surface_backend_render_target_handle(
    SkSurface* s, SkSurface::BackendHandleAccess a) {
    GrBackendObject result;
    if (!s->getRenderTargetHandle(&result, a)) {
        return 0;
    }
    return result;
}

static void test_backend_handle_access_copy_on_write(
    skiatest::Reporter* reporter, SkSurface* surface, SkSurface::BackendHandleAccess mode,
    GrBackendObject (*func)(SkSurface*, SkSurface::BackendHandleAccess)) {
    GrBackendObject obj1 = func(surface, mode);
    SkAutoTUnref<SkImage> snap1(surface->newImageSnapshot());

    GrBackendObject obj2 = func(surface, mode);
    SkAutoTUnref<SkImage> snap2(surface->newImageSnapshot());

    // If the access mode triggers CoW, then the backend objects should reflect it.
    REPORTER_ASSERT(reporter, (obj1 == obj2) == (snap1 == snap2));
}
DEF_TEST(SurfaceBackendHandleAccessCopyOnWrite, reporter) {
    const SkSurface::BackendHandleAccess accessModes[] = {
        SkSurface::kFlushRead_BackendHandleAccess,
        SkSurface::kFlushWrite_BackendHandleAccess,
        SkSurface::kDiscardWrite_BackendHandleAccess,
    };
    for (auto& handle_access_func :
            { &get_surface_backend_texture_handle, &get_surface_backend_render_target_handle }) {
        for (auto& accessMode : accessModes) {
            SkAutoTUnref<SkSurface> surface(create_surface());
            test_backend_handle_access_copy_on_write(reporter, surface, accessMode,
                                                     handle_access_func);
        }
    }
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceBackendHandleAccessCopyOnWrite_Gpu, reporter, context) {
        const SkSurface::BackendHandleAccess accessModes[] = {
        SkSurface::kFlushRead_BackendHandleAccess,
        SkSurface::kFlushWrite_BackendHandleAccess,
        SkSurface::kDiscardWrite_BackendHandleAccess,
    };
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        for (auto& handle_access_func :
                { &get_surface_backend_texture_handle, &get_surface_backend_render_target_handle }) {
            for (auto& accessMode : accessModes) {
                SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType,
                                                             nullptr));
                test_backend_handle_access_copy_on_write(reporter, surface, accessMode,
                                                         handle_access_func);
            }
        }
    }
}
#endif

#if SK_SUPPORT_GPU
// May we (soon) eliminate the need to keep testing this, by hiding the bloody device!
static uint32_t get_legacy_gen_id(SkSurface* surface) {
    SkBaseDevice* device =
            surface->getCanvas()->getDevice_just_for_deprecated_compatibility_testing();
    return device->accessBitmap(false).getGenerationID();
}
/*
 *  Test legacy behavor of bumping the surface's device's bitmap's genID when we access its
 *  texture handle for writing.
 *
 *  Note: this needs to be tested separately from checking newImageSnapshot, as calling that
 *  can also incidentally bump the genID (when a new backing surface is created).
 */
static void test_backend_handle_gen_id(
    skiatest::Reporter* reporter, SkSurface* surface,
    GrBackendObject (*func)(SkSurface*, SkSurface::BackendHandleAccess)) {
    const uint32_t gen0 = get_legacy_gen_id(surface);
    func(surface, SkSurface::kFlushRead_BackendHandleAccess);
    const uint32_t gen1 = get_legacy_gen_id(surface);
    REPORTER_ASSERT(reporter, gen0 == gen1);

    func(surface, SkSurface::kFlushWrite_BackendHandleAccess);
    const uint32_t gen2 = get_legacy_gen_id(surface);
    REPORTER_ASSERT(reporter, gen0 != gen2);

    func(surface, SkSurface::kDiscardWrite_BackendHandleAccess);
    const uint32_t gen3 = get_legacy_gen_id(surface);
    REPORTER_ASSERT(reporter, gen0 != gen3);
    REPORTER_ASSERT(reporter, gen2 != gen3);
}
static void test_backend_handle_unique_id(
    skiatest::Reporter* reporter, SkSurface* surface,
    GrBackendObject (*func)(SkSurface*, SkSurface::BackendHandleAccess)) {
    SkAutoTUnref<SkImage> image0(surface->newImageSnapshot());
    GrBackendObject obj = func(surface, SkSurface::kFlushRead_BackendHandleAccess);
    REPORTER_ASSERT(reporter, obj != 0);
    SkAutoTUnref<SkImage> image1(surface->newImageSnapshot());
    // just read access should not affect the snapshot
    REPORTER_ASSERT(reporter, image0->uniqueID() == image1->uniqueID());

    obj = func(surface, SkSurface::kFlushWrite_BackendHandleAccess);
    REPORTER_ASSERT(reporter, obj != 0);
    SkAutoTUnref<SkImage> image2(surface->newImageSnapshot());
    // expect a new image, since we claimed we would write
    REPORTER_ASSERT(reporter, image0->uniqueID() != image2->uniqueID());

    obj = func(surface, SkSurface::kDiscardWrite_BackendHandleAccess);
    REPORTER_ASSERT(reporter, obj != 0);
    SkAutoTUnref<SkImage> image3(surface->newImageSnapshot());
    // expect a new(er) image, since we claimed we would write
    REPORTER_ASSERT(reporter, image0->uniqueID() != image3->uniqueID());
    REPORTER_ASSERT(reporter, image2->uniqueID() != image3->uniqueID());
}
// No CPU test.
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceBackendHandleAccessIDs_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        for (auto& test_func : { &test_backend_handle_unique_id, &test_backend_handle_gen_id }) {
            for (auto& handle_access_func :
                { &get_surface_backend_texture_handle, &get_surface_backend_render_target_handle}) {
                SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType,
                                                             nullptr));
                test_func(reporter, surface, handle_access_func);
            }
        }
    }
}
#endif

// Verify that the right canvas commands trigger a copy on write.
static void test_copy_on_write(skiatest::Reporter* reporter, SkSurface* surface) {
    SkCanvas* canvas = surface->getCanvas();

    const SkRect testRect =
        SkRect::MakeXYWH(SkIntToScalar(0), SkIntToScalar(0),
                         SkIntToScalar(4), SkIntToScalar(5));
    SkPath testPath;
    testPath.addRect(SkRect::MakeXYWH(SkIntToScalar(0), SkIntToScalar(0),
                                      SkIntToScalar(2), SkIntToScalar(1)));

    const SkIRect testIRect = SkIRect::MakeXYWH(0, 0, 2, 1);

    SkRegion testRegion;
    testRegion.setRect(testIRect);


    const SkColor testColor = 0x01020304;
    const SkPaint testPaint;
    const SkPoint testPoints[3] = {
        {SkIntToScalar(0), SkIntToScalar(0)},
        {SkIntToScalar(2), SkIntToScalar(1)},
        {SkIntToScalar(0), SkIntToScalar(2)}
    };
    const size_t testPointCount = 3;

    SkBitmap testBitmap;
    testBitmap.allocN32Pixels(10, 10);
    testBitmap.eraseColor(0);

    SkRRect testRRect;
    testRRect.setRectXY(testRect, SK_Scalar1, SK_Scalar1);

    SkString testText("Hello World");
    const SkPoint testPoints2[] = {
        { SkIntToScalar(0), SkIntToScalar(1) },
        { SkIntToScalar(1), SkIntToScalar(1) },
        { SkIntToScalar(2), SkIntToScalar(1) },
        { SkIntToScalar(3), SkIntToScalar(1) },
        { SkIntToScalar(4), SkIntToScalar(1) },
        { SkIntToScalar(5), SkIntToScalar(1) },
        { SkIntToScalar(6), SkIntToScalar(1) },
        { SkIntToScalar(7), SkIntToScalar(1) },
        { SkIntToScalar(8), SkIntToScalar(1) },
        { SkIntToScalar(9), SkIntToScalar(1) },
        { SkIntToScalar(10), SkIntToScalar(1) },
    };

#define EXPECT_COPY_ON_WRITE(command)                               \
    {                                                               \
        SkImage* imageBefore = surface->newImageSnapshot();         \
        SkAutoTUnref<SkImage> aur_before(imageBefore);              \
        canvas-> command ;                                          \
        SkImage* imageAfter = surface->newImageSnapshot();          \
        SkAutoTUnref<SkImage> aur_after(imageAfter);                \
        REPORTER_ASSERT(reporter, imageBefore != imageAfter);       \
    }

    EXPECT_COPY_ON_WRITE(clear(testColor))
    EXPECT_COPY_ON_WRITE(drawPaint(testPaint))
    EXPECT_COPY_ON_WRITE(drawPoints(SkCanvas::kPoints_PointMode, testPointCount, testPoints, \
        testPaint))
    EXPECT_COPY_ON_WRITE(drawOval(testRect, testPaint))
    EXPECT_COPY_ON_WRITE(drawRect(testRect, testPaint))
    EXPECT_COPY_ON_WRITE(drawRRect(testRRect, testPaint))
    EXPECT_COPY_ON_WRITE(drawPath(testPath, testPaint))
    EXPECT_COPY_ON_WRITE(drawBitmap(testBitmap, 0, 0))
    EXPECT_COPY_ON_WRITE(drawBitmapRect(testBitmap, testRect, nullptr))
    EXPECT_COPY_ON_WRITE(drawBitmapNine(testBitmap, testIRect, testRect, nullptr))
    EXPECT_COPY_ON_WRITE(drawSprite(testBitmap, 0, 0, nullptr))
    EXPECT_COPY_ON_WRITE(drawText(testText.c_str(), testText.size(), 0, 1, testPaint))
    EXPECT_COPY_ON_WRITE(drawPosText(testText.c_str(), testText.size(), testPoints2, \
        testPaint))
    EXPECT_COPY_ON_WRITE(drawTextOnPath(testText.c_str(), testText.size(), testPath, nullptr, \
        testPaint))
}
DEF_TEST(SurfaceCopyOnWrite, reporter) {
    SkAutoTUnref<SkSurface> surface(create_surface());
    test_copy_on_write(reporter, surface);
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceCopyOnWrite_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType, nullptr));
        test_copy_on_write(reporter, surface);
    }
}
#endif

static void test_writable_after_snapshot_release(skiatest::Reporter* reporter,
                                                 SkSurface* surface) {
    // This test succeeds by not triggering an assertion.
    // The test verifies that the surface remains writable (usable) after
    // acquiring and releasing a snapshot without triggering a copy on write.
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(1);
    surface->newImageSnapshot()->unref();  // Create and destroy SkImage
    canvas->clear(2);  // Must not assert internally
}
DEF_TEST(SurfaceWriteableAfterSnapshotRelease, reporter) {
    SkAutoTUnref<SkSurface> surface(create_surface());
    test_writable_after_snapshot_release(reporter, surface);
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceWriteableAfterSnapshotRelease_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType, nullptr));
        test_writable_after_snapshot_release(reporter, surface);
    }
}
#endif

#if SK_SUPPORT_GPU
static void test_crbug263329(skiatest::Reporter* reporter,
                             SkSurface* surface1,
                             SkSurface* surface2) {
    // This is a regression test for crbug.com/263329
    // Bug was caused by onCopyOnWrite releasing the old surface texture
    // back to the scratch texture pool even though the texture is used
    // by and active SkImage_Gpu.
    SkCanvas* canvas1 = surface1->getCanvas();
    SkCanvas* canvas2 = surface2->getCanvas();
    canvas1->clear(1);
    SkAutoTUnref<SkImage> image1(surface1->newImageSnapshot());
    // Trigger copy on write, new backing is a scratch texture
    canvas1->clear(2);
    SkAutoTUnref<SkImage> image2(surface1->newImageSnapshot());
    // Trigger copy on write, old backing should not be returned to scratch
    // pool because it is held by image2
    canvas1->clear(3);

    canvas2->clear(4);
    SkAutoTUnref<SkImage> image3(surface2->newImageSnapshot());
    // Trigger copy on write on surface2. The new backing store should not
    // be recycling a texture that is held by an existing image.
    canvas2->clear(5);
    SkAutoTUnref<SkImage> image4(surface2->newImageSnapshot());
    REPORTER_ASSERT(reporter, as_IB(image4)->getTexture() != as_IB(image3)->getTexture());
    // The following assertion checks crbug.com/263329
    REPORTER_ASSERT(reporter, as_IB(image4)->getTexture() != as_IB(image2)->getTexture());
    REPORTER_ASSERT(reporter, as_IB(image4)->getTexture() != as_IB(image1)->getTexture());
    REPORTER_ASSERT(reporter, as_IB(image3)->getTexture() != as_IB(image2)->getTexture());
    REPORTER_ASSERT(reporter, as_IB(image3)->getTexture() != as_IB(image1)->getTexture());
    REPORTER_ASSERT(reporter, as_IB(image2)->getTexture() != as_IB(image1)->getTexture());
}
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceCRBug263329_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkAutoTUnref<SkSurface> surface1(surface_func(context, kPremul_SkAlphaType, nullptr));
        SkAutoTUnref<SkSurface> surface2(surface_func(context, kPremul_SkAlphaType, nullptr));
        test_crbug263329(reporter, surface1, surface2);
    }
}
#endif

DEF_TEST(SurfaceGetTexture, reporter) {
    SkAutoTUnref<SkSurface> surface(create_surface());
    SkAutoTUnref<SkImage> image(surface->newImageSnapshot());
    REPORTER_ASSERT(reporter, as_IB(image)->getTexture() == nullptr);
    surface->notifyContentWillChange(SkSurface::kDiscard_ContentChangeMode);
    REPORTER_ASSERT(reporter, as_IB(image)->getTexture() == nullptr);
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceGetTexture_Gpu, reporter, context) {
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        SkAutoTUnref<SkSurface> surface(surface_func(context, kPremul_SkAlphaType, nullptr));
        SkAutoTUnref<SkImage> image(surface->newImageSnapshot());
        GrTexture* texture = as_IB(image)->getTexture();
        REPORTER_ASSERT(reporter, texture);
        REPORTER_ASSERT(reporter, 0 != texture->getTextureHandle());
        surface->notifyContentWillChange(SkSurface::kDiscard_ContentChangeMode);
        REPORTER_ASSERT(reporter, as_IB(image)->getTexture() == texture);
    }
}
#endif

#if SK_SUPPORT_GPU
#include "GrGpuResourcePriv.h"
#include "SkGpuDevice.h"
#include "SkImage_Gpu.h"
#include "SkSurface_Gpu.h"

static SkSurface::Budgeted is_budgeted(SkSurface* surf) {
    return ((SkSurface_Gpu*)surf)->getDevice()->accessRenderTarget()->resourcePriv().isBudgeted() ?
        SkSurface::kYes_Budgeted : SkSurface::kNo_Budgeted;
}

static SkSurface::Budgeted is_budgeted(SkImage* image) {
    return ((SkImage_Gpu*)image)->getTexture()->resourcePriv().isBudgeted() ?
        SkSurface::kYes_Budgeted : SkSurface::kNo_Budgeted;
}

DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceBudget, reporter, context) {
    SkImageInfo info = SkImageInfo::MakeN32Premul(8,8);
    for (int i = 0; i < 2; ++i) {
        SkSurface::Budgeted sbudgeted = i ? SkSurface::kYes_Budgeted : SkSurface::kNo_Budgeted;
        for (int j = 0; j < 2; ++j) {
            SkSurface::Budgeted ibudgeted = j ? SkSurface::kYes_Budgeted : SkSurface::kNo_Budgeted;
            SkAutoTUnref<SkSurface>
                surface(SkSurface::NewRenderTarget(context, sbudgeted, info, 0));
            SkASSERT(surface);
            REPORTER_ASSERT(reporter, sbudgeted == is_budgeted(surface));

            SkAutoTUnref<SkImage> image(surface->newImageSnapshot(ibudgeted));

            // Initially the image shares a texture with the surface, and the surface decides
            // whether it is budgeted or not.
            REPORTER_ASSERT(reporter, sbudgeted == is_budgeted(surface));
            REPORTER_ASSERT(reporter, sbudgeted == is_budgeted(image));

            // Now trigger copy-on-write
            surface->getCanvas()->clear(SK_ColorBLUE);

            // They don't share a texture anymore. They should each have made their own budget
            // decision.
            REPORTER_ASSERT(reporter, sbudgeted == is_budgeted(surface));
            REPORTER_ASSERT(reporter, ibudgeted == is_budgeted(image));
        }
    }
}
#endif

static void test_no_canvas1(skiatest::Reporter* reporter,
                            SkSurface* surface,
                            SkSurface::ContentChangeMode mode) {
    // Test passes by not asserting
    surface->notifyContentWillChange(mode);
    SkDEBUGCODE(surface->validate();)
}
static void test_no_canvas2(skiatest::Reporter* reporter,
                            SkSurface* surface,
                            SkSurface::ContentChangeMode mode) {
    // Verifies the robustness of SkSurface for handling use cases where calls
    // are made before a canvas is created.
    SkImage* image1 = surface->newImageSnapshot();
    SkAutoTUnref<SkImage> aur_image1(image1);
    SkDEBUGCODE(image1->validate();)
    SkDEBUGCODE(surface->validate();)
    surface->notifyContentWillChange(mode);
    SkDEBUGCODE(image1->validate();)
    SkDEBUGCODE(surface->validate();)
    SkImage* image2 = surface->newImageSnapshot();
    SkAutoTUnref<SkImage> aur_image2(image2);
    SkDEBUGCODE(image2->validate();)
    SkDEBUGCODE(surface->validate();)
    REPORTER_ASSERT(reporter, image1 != image2);
}
DEF_TEST(SurfaceNoCanvas, reporter) {
    SkSurface::ContentChangeMode modes[] =
            { SkSurface::kDiscard_ContentChangeMode, SkSurface::kRetain_ContentChangeMode};
    for (auto& test_func : { &test_no_canvas1, &test_no_canvas2 }) {
        for (auto& mode : modes) {
            SkAutoTUnref<SkSurface> surface(create_surface());
            test_func(reporter, surface, mode);
        }
    }
}
#if SK_SUPPORT_GPU
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SurfaceNoCanvas_Gpu, reporter, context) {
    SkSurface::ContentChangeMode modes[] =
            { SkSurface::kDiscard_ContentChangeMode, SkSurface::kRetain_ContentChangeMode};
    for (auto& surface_func : { &create_gpu_surface, &create_gpu_scratch_surface }) {
        for (auto& test_func : { &test_no_canvas1, &test_no_canvas2 }) {
            for (auto& mode : modes) {
                SkAutoTUnref<SkSurface> surface(
                    surface_func(context, kPremul_SkAlphaType, nullptr));
                test_func(reporter, surface, mode);
            }
        }
    }
}
#endif
