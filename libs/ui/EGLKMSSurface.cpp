/*
 * Copyright 2009 0xlab.org
 * Authored by Chia-I Wu <olv@0xlab.org>
 *
 * Copyright 2007 The Android Open Source Project
 *
 * Licensed under the Apache License Version 2.0(the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "EGLKMSSurface"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cutils/log.h>

#include <ui/Rect.h>
#include <ui/Region.h>
#include <ui/EGLKMSSurface.h>
#include <ui/DisplayInfo.h>

#include <i915_drm.h>


// ----------------------------------------------------------------------------

egl_native_window_t* android_createKMSSurface()
{
    egl_native_window_t* s = new android::EGLKMSSurface();
    s->memory_type = NATIVE_MEMORY_TYPE_GPU;
    return s;
}

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------

/*
 * There are some complexities here.
 *
 * eagle always allocates another back buffer.  It does so such that it can
 * handle EGLKMSSurface and EGLNativeWindowSurface the same way  (The latter
 * switches front buffers, which is not supported by mesa.  Therefore, eagle
 * always allocates a back buffer, to be used by mesa as a front left buffer).
 * Under this condition, EGLKMSSurface should be single-buffered for better
 * performance and smaller footprint.  And eagle should (it isn't!) wait for
 * vsync before it swaps buffers.
 *
 * Another reason is that eagle reports EGL_BUFFER_PRESERVED as it should.
 * Because of this behavior, surfaceflinger will draw only the dirty region and
 * calls copyFrontToBack to fill the clean region.  As can be noted, our
 * copyFrontToBack is suboptimal.  If EGLKMSSurface is double-buffered, the
 * performance suffers badly becase of slow copy.
 */

/* ignore another buffer */
/* TODO do not allocate two buffers when enabled */
#define SINGLE_BUFFER_HACK 1

/* make libagl at least not crash */
#define AGL_SUPPORT 1

EGLKMSSurface::EGLKMSSurface()
    : EGLNativeSurface<EGLKMSSurface>()
{
    egl_native_window_t::version = sizeof(egl_native_window_t);
    egl_native_window_t::ident = 0;
    egl_native_window_t::incRef = &EGLKMSSurface::hook_incRef;
    egl_native_window_t::decRef = &EGLKMSSurface::hook_decRef;
    egl_native_window_t::swapBuffers = &EGLKMSSurface::hook_swapBuffers;
    egl_native_window_t::connect = 0;
    egl_native_window_t::disconnect = 0;

    egl_native_window_t::fd = mapFrameBuffer();
    if (egl_native_window_t::fd >= 0) {
	/* point to back buffer */
        egl_native_window_t::width  = mFb[1 - mIndex].width;
        egl_native_window_t::height = mFb[1 - mIndex].height;
        egl_native_window_t::stride =
		mFb[1 - mIndex].stride / (mFb[1 - mIndex].bpp / 8);
        egl_native_window_t::format = mFb[1 - mIndex].format;
#if AGL_SUPPORT
        egl_native_window_t::base   = intptr_t(mFb[0].base);
        egl_native_window_t::offset =
		intptr_t(mFb[1 - mIndex].base) - egl_native_window_t::base;
#else
        egl_native_window_t::base   = 0;
        egl_native_window_t::offset = 0;
#endif
        egl_native_window_t::flags  = 0;
	if (mConnector->mmWidth && mConnector->mmHeight) {
		egl_native_window_t::xdpi = mMode->hdisplay * 25.4f / mConnector->mmWidth;
		egl_native_window_t::ydpi = mMode->vdisplay * 25.4f / mConnector->mmHeight;
	} else {
		egl_native_window_t::xdpi = 160.0f;
		egl_native_window_t::ydpi = 160.0f;
	}
        egl_native_window_t::fps  = mMode->vrefresh / 1000;
        egl_native_window_t::memory_type = NATIVE_MEMORY_TYPE_GPU;
        // no error, set the magic word
        egl_native_window_t::magic = 0x600913;

        egl_native_window_t::oem[0] = mFb[1 - mIndex].name;
    }
    mPageFlipCount = 0;
}

EGLKMSSurface::~EGLKMSSurface()
{
	int i;

	free(mBuffer);

	for (i = 0; i < 2; i++) {
		struct drm_gem_close close;

		memset(&close, 0, sizeof(close));
		close.handle = mFb[i].handle;
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
	}

	drmModeFreeEncoder(mEncoder);
	drmModeFreeConnector(mConnector);

	close(egl_native_window_t::fd);
}

void EGLKMSSurface::hook_incRef(NativeWindowType window)
{
	EGLKMSSurface* that = static_cast<EGLKMSSurface*>(window);
	that->incStrong(that);
}

void EGLKMSSurface::hook_decRef(NativeWindowType window)
{
	EGLKMSSurface* that = static_cast<EGLKMSSurface*>(window);
	that->decStrong(that);
}

uint32_t EGLKMSSurface::hook_swapBuffers(NativeWindowType window)
{
	EGLKMSSurface* that = static_cast<EGLKMSSurface*>(window);
	return that->swapBuffers();
}

void EGLKMSSurface::setSwapRectangle(int l, int t, int w, int h)
{
}

int EGLKMSSurface::setCrtc()
{
	int ret;

	ret = drmModeSetCrtc(egl_native_window_t::fd, mEncoder->crtc_id,
			mFb[1 - mIndex].id, 0, 0, &mConnector->connector_id, 1,
			mMode);
	if (ret)
		LOGE("drmModeSetCrtc failed");
	return ret;
}

int EGLKMSSurface::setCPUDomain()
{
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	memset(&set_domain, 0, sizeof(set_domain));
	set_domain.handle = mFb[1 - mIndex].handle;
	set_domain.read_domains = I915_GEM_DOMAIN_CPU;
	set_domain.write_domain = I915_GEM_DOMAIN_CPU;

	ret = ioctl(egl_native_window_t::fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
	if (ret)
		LOGE("failed to set back buffer to CPU domain");
	return ret;
}

uint32_t EGLKMSSurface::swapBuffers()
{
	int ret;

#if SINGLE_BUFFER_HACK
	if (!mPageFlipCount)
		setCrtc();
#else
	// do the actual flip
	setCrtc();
	mIndex = 1 - mIndex;

        egl_native_window_t::oem[0] = mFb[1 - mIndex].name;
#if AGL_SUPPORT
        egl_native_window_t::offset =
		intptr_t(mFb[1 - mIndex].base) - egl_native_window_t::base;
	setCPUDomain();
#endif
#endif /* SINGLE_BUFFER_HACK */

	mPageFlipCount++;

	// We don't support screen-size changes for now
	return 0;
}

void EGLKMSSurface::acquireScreen()
{
	setCrtc();
}

void EGLKMSSurface::releaseScreen()
{
}

int32_t EGLKMSSurface::getPageFlipCount() const
{
	return mPageFlipCount;
}

void EGLKMSSurface::copyFrontToBack(const Region& copyback)
{
#if !SINGLE_BUFFER_HACK
	Region::iterator iterator(copyback);
	if (iterator) {
		Rect r;
		const size_t bpp = bytesPerPixel(egl_native_window_t::format);
		const size_t bpr = egl_native_window_t::stride * bpp;
		struct drm_i915_gem_pread pread;
		struct drm_i915_gem_pwrite pwrite;

		memset(&pread, 0, sizeof(pread));
		pread.handle = mFb[mIndex].handle;
		pread.data_ptr = (long) mBuffer;

		memset(&pwrite, 0, sizeof(pwrite));
		pwrite.handle = mFb[1 - mIndex].handle;
		pwrite.data_ptr = (long) mBuffer;

		while (iterator.iterate(&r)) {
			size_t offset = (r.left + egl_native_window_t::stride * r.top) * bpp;
			size_t size = (r.right - r.left) * bpp;
			ssize_t h = r.bottom - r.top;

			pread.offset = offset;
			pread.size = size;

			pwrite.offset = offset;
			pwrite.size = size;

			while (h-- > 0) {
				if (ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &pread) != 0) {
					LOGE("failed to pread");
					break;
				}
				if (ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite) != 0) {
					LOGE("failed to pwrite");
					break;
				}

				pread.offset += bpr;
				pwrite.offset += bpr;
			}
		}
	}
#endif /* !SINGLE_BUFFER_HACK */
}

void EGLKMSSurface::copyFrontToImage(const copybit_image_t& dst)
{
	const size_t bpp = bytesPerPixel(egl_native_window_t::format);
	const size_t bpr = egl_native_window_t::stride * bpp;
	struct drm_i915_gem_pread pread;

	memset(&pread, 0, sizeof(pread));
	pread.handle = mFb[mIndex].handle;
	pread.size = mFb[mIndex].size;
	pread.data_ptr = (long) dst.base + dst.offset;

	if (ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &pread) != 0)
		LOGE("failed to copy front to image");
}

void EGLKMSSurface::copyBackToImage(const copybit_image_t& dst)
{
	const size_t bpp = bytesPerPixel(egl_native_window_t::format);
	const size_t bpr = egl_native_window_t::stride * bpp;
	struct drm_i915_gem_pread pread;

	memset(&pread, 0, sizeof(pread));
	pread.handle = mFb[1 - mIndex].handle;
	pread.size = mFb[1 - mIndex].size;
	pread.data_ptr = (long) dst.base + dst.offset;

	if (ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &pread) != 0)
		LOGE("failed to copy back to image");
}

int EGLKMSSurface::authMagic(drm_magic_t magic)
{
	drm_auth_t auth;

	auth.magic = magic;
	if (ioctl(egl_native_window_t::fd, DRM_IOCTL_AUTH_MAGIC, &auth))
		return -errno;

	return 0;
}

status_t EGLKMSSurface::addFb(int fd)
{
	int i;

	for (i = 0; i < 2; i++) {
		struct drm_i915_gem_create create;
		struct drm_gem_flink flink;
#if AGL_SUPPORT
		struct drm_i915_gem_mmap gmap;
#endif
		int ret;

		mFb[i].width = mMode->hdisplay;
		mFb[i].height = mMode->vdisplay;
		mFb[i].format = GGL_PIXEL_FORMAT_RGB_565;
		mFb[i].bpp = bytesPerPixel(mFb[i].format) * 8;
		mFb[i].stride = mFb[i].width * mFb[i].bpp / 8;
		mFb[i].size = mFb[i].stride * mFb[i].height;

		memset(&create, 0, sizeof(create));
		memset(&flink, 0, sizeof(flink));

		create.size = mFb[i].size;
		if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create)) {
			LOGE("failed to create GEM object");
			break;
		}

		flink.handle = create.handle;
		if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink) < 0) {
			LOGE("failed to flink GEM object");
			break;
		}

		mFb[i].name = flink.name;
		mFb[i].handle = flink.handle;

#if AGL_SUPPORT
		memset(&gmap, 0, sizeof(gmap));
		gmap.handle = create.handle;
		gmap.size = create.size;
		if (ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &gmap) < 0) {
			LOGE("failed to mmap GEM object");
			break;
		}
		mFb[i].base = (char *) (long) gmap.addr_ptr;
#else
		mFb[i].base = NULL;
#endif

		ret = drmModeAddFB(fd, mFb[i].width, mFb[i].height,
				mFb[i].bpp, mFb[i].bpp, mFb[i].stride,
				mFb[i].handle, &mFb[i].id);
		if (ret) {
			LOGE("failed to add frame buffer");
			break;
		}
	}

	return (i == 2) ? 0 : -1;
}

status_t EGLKMSSurface::mapFrameBuffer()
{
	char const * const device_template[] = {
		"/dev/dri/card%u",
		"/dev/card%u",
		0 };
	int fd = -1;
	int i=0;
	char name[64];
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModeModeInfo *mode = NULL;
	status_t ret;

	while ((fd==-1) && device_template[i]) {
		snprintf(name, 64, device_template[i], 0);
		fd = open(name, O_RDWR, 0);
		i++;
	}
	if (fd < 0) {
		LOGE("failed to open DRI device");
		return -errno;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		LOGE("failed to get resources");
		goto fail;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
				connector->count_modes > 0)
			break;

		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (!connector) {
		LOGE("failed to find an active connector");
		goto fail;
	}

	mode = &connector->modes[0];

	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);
		if (encoder == NULL)
			continue;

		if (encoder->encoder_id == connector->encoder_id)
			break;

		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (!encoder) {
		LOGE("failed to find an matching encoder");
		goto fail;
	}

	mConnector = connector;
	mEncoder = encoder;
	mMode = mode;

	drmModeFreeResources(resources);

	LOGI("using (fd=%d)\n"
	     "connector = 0x%x\n"
	     "encoder   = 0x%x\n"
	     "crtc      = 0x%x\n"
	     "mode      = %s\n",
	     fd,
	     mConnector->connector_id,
	     mEncoder->encoder_id,
	     mEncoder->crtc_id,
	     mMode->name);

	ret = addFb(fd);
	if (ret)
		goto fail;

	LOGI("id        = %d, %d\n"
	     "name      = %d, %d\n"
	     "handle    = %d, %d\n"
	     "size      = %d\n"
	     "width     = %d\n"
	     "height    = %d\n"
	     "stride    = %d\n"
	     "bpp       = %d\n",
	     mFb[0].id, mFb[1].id,
	     mFb[0].name, mFb[1].name,
	     mFb[0].handle, mFb[1].handle,
	     mFb[0].size, mFb[0].width, mFb[0].height,
	     mFb[0].stride, mFb[0].bpp);

	mFlags = PAGE_FLIP;
	mIndex = 0;

	mBuffer = (char *) malloc(mFb[0].size);

	return fd;

fail:
	if (encoder)
		drmModeFreeEncoder(encoder);
	if (connector)
		drmModeFreeConnector(connector);
	if (resources)
		drmModeFreeResources(resources);
	if (fd >= 0)
		close(fd);

	return -1;
}

// ----------------------------------------------------------------------------
}; // namespace android
// ----------------------------------------------------------------------------
