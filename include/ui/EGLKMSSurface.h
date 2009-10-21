/*
 * Copyright 2009 0xlab.org
 * Authored by Chia-I Wu <olv@0xlab.org>
 *
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef _EGL_KMS_SURFACE_
#define _EGL_KMS_SURFACE_

#include <stdint.h>
#include <sys/types.h>

#include <pixelflinger/pixelflinger.h>
#include <ui/EGLNativeSurface.h>

#include <EGL/egl.h>

extern "C" {
#include <drm.h>
#include <xf86drmMode.h>
};

struct copybit_image_t;

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

class Region;
class Rect;

class EGLKMSSurface : public EGLNativeSurface<EGLKMSSurface>
{
public:
    EGLKMSSurface();
    ~EGLKMSSurface();

    int32_t getPageFlipCount() const;
    void    copyFrontToBack(const Region& copyback);
    void    copyFrontToImage(const copybit_image_t& dst);
    void    copyBackToImage(const copybit_image_t& dst);
    void    acquireScreen();
    void    releaseScreen();

    void        setSwapRectangle(int l, int t, int w, int h);

    int authMagic(drm_magic_t magic);

private:
    static void         hook_incRef(NativeWindowType window);
    static void         hook_decRef(NativeWindowType window);
    static uint32_t     hook_swapBuffers(NativeWindowType window);

            int         setCrtc();
            int         setCPUDomain();
            uint32_t    swapBuffers();

	    status_t    addFb(int fd);
            status_t    mapFrameBuffer();

            enum {
                PAGE_FLIP = 0x00000001
            };
    int                 mIndex;
    uint32_t            mFlags;
    int32_t             mPageFlipCount;

    drmModeConnector *mConnector;
    drmModeEncoder *mEncoder;
    drmModeModeInfo *mMode;
    char *mBuffer;

    struct {
	    uint32_t id;
	    uint32_t name;
	    uint32_t handle;
	    size_t size;
	    int width, height;
	    int stride;
	    int bpp;
	    int format;
	    char *base;
	    uint32_t reserved[2];
    } mFb[2];
};

// ---------------------------------------------------------------------------
}; // namespace android
// ---------------------------------------------------------------------------

#endif // _EGL_KMS_SURFACE_

