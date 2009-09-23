/*
 * Copyright (C) 2009 0xlab.org
 * Authored by Chia-I Wu <olv@0xlab.org>
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "SurfaceFlinger"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include <utils/IBinder.h>
#include <utils/MemoryDealer.h>
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapPmem.h>
#include <utils/MemoryHeapBase.h>
#include <utils/IPCThreadState.h>
#include <utils/StopWatch.h>

#include <ui/ISurfaceComposer.h>

#include "VRamHeap.h"
#include "GEMHardware.h"

#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif

#include <utils/MemoryGem.h>
#include <i915_drm.h>

namespace android {

class MemoryHeapGem : public virtual BnMemoryHeap, public HeapInterface
{
public:
    MemoryHeapGem(int fd, size_t size, uint32_t flags = 0);
    virtual ~MemoryHeapGem();

    virtual int         getHeapID() const;
    virtual void*       getBase() const;
    virtual size_t      getSize() const;
    virtual uint32_t    getFlags() const;
    virtual uint32_t    getGemName() const;
    virtual uint32_t    getGemHandle() const;

    const char*         getDevice() const { return NULL; }
    
    /* HeapInterface */
    virtual sp<IMemory> mapMemory(size_t offset, size_t size);

private:
    void       assertBase();

    int mFd;
    size_t mSize;
    uint32_t mFlags;
    void *mBase;

    uint32_t mGemName;
    uint32_t mGemHandle;
};

class GemDealer : public MemoryDealer
{
public:
    GemDealer(const sp<HeapInterface>& heap);
    ~GemDealer();

    static sp<GemDealer> create();

private:
    static const char GEM_DEVICE[];
    static const size_t GEM_SIZE;
    static int mFd;
};

MemoryHeapGem::MemoryHeapGem(int fd, size_t size, uint32_t flags)
	: mFd(-1), mSize(size), mFlags(flags), mBase(MAP_FAILED), mGemName(0), mGemHandle(0)
{
    struct drm_i915_gem_create create;
    struct drm_gem_flink flink;

    mFd = dup(fd);

    memset(&create, 0, sizeof(create));
    memset(&flink, 0, sizeof(flink));

    create.size = size;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create)) {
        LOGE("failed to create buffer of size %d", size);
        return;
    }

    flink.handle = create.handle;
    if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink) < 0) {
        LOGE("failed to flink buffer");
        return;
    }

    LOGD("New GEM: handle %d name %d size %d", flink.handle, flink.name, size);

    mGemHandle = flink.handle;
    mGemName = flink.name;

    assertBase();
}

MemoryHeapGem::~MemoryHeapGem()
{
    if (mFd > 0) {
        if (mBase != MAP_FAILED)
            munmap(mBase, mSize);

        if (mGemHandle != 0) {
            struct drm_gem_close close_arg;
            close_arg.handle = mGemHandle;
            ioctl(mFd, DRM_IOCTL_GEM_CLOSE, &close_arg);
        }

        close(mFd);
    }
}

int MemoryHeapGem::getHeapID() const {
    return mFd;
}

void MemoryHeapGem::assertBase() {
	struct drm_i915_gem_mmap gmap;
	struct drm_i915_gem_mmap_gtt gtt;
	int ret;

	if (mBase != MAP_FAILED)
		return;

	memset(&gmap, 0, sizeof(gmap));
	memset(&gtt, 0, sizeof(gtt));

	gmap.handle = mGemHandle;
	gmap.size = mSize;
	ret = ioctl(mFd, DRM_IOCTL_I915_GEM_MMAP, &gmap);
	if (!ret) {
		mBase = (void *) gmap.addr_ptr;
	} else {
		LOGE("failed to mmap gem buffer: %s", strerror(errno));

		gtt.handle = mGemHandle;
		ret = ioctl(mFd, DRM_IOCTL_I915_GEM_MMAP_GTT, &gtt);
		if (ret) {
			LOGE("failed to gtt map gem buffer: %s", strerror(errno));
			return;
		}

		mBase = mmap(NULL, mSize, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, gtt.offset);
	}
}

void *MemoryHeapGem::getBase() const {
	if (mBase != MAP_FAILED)
		return mBase;
	else
		return NULL;
}

size_t MemoryHeapGem::getSize() const {
    return mSize;
}

uint32_t MemoryHeapGem::getFlags() const {
    return mFlags;
}

uint32_t MemoryHeapGem::getGemName() const {
    return mGemName;
}

uint32_t MemoryHeapGem::getGemHandle() const {
    return mGemHandle;
}

sp<IMemory> MemoryHeapGem::mapMemory(size_t offset, size_t size)
{
	return new MemoryBase(this, offset, size);
}

const char GemDealer::GEM_DEVICE[] = "/dev/dri/card0";
/* 1024 * 600 * 4 is about 2.5M  */
const size_t GemDealer::GEM_SIZE = 5 * 1024 * 1024;
int GemDealer::mFd = -1;

GemDealer::GemDealer(const sp<HeapInterface>& heap)
    : MemoryDealer(heap)
{
}

GemDealer::~GemDealer()
{
}

sp<GemDealer> GemDealer::create()
{
    sp<HeapInterface> heap;

    if (mFd < 0) {
	    mFd = open(GEM_DEVICE, O_RDWR);
	    if (mFd < 0) {
		    LOGE("failed to open %s", GEM_DEVICE);
		    return NULL;
	    }
    }

    heap = new MemoryHeapGem::MemoryHeapGem(mFd, GEM_SIZE);

    return new GemDealer(heap);
}

GEMHardware::GEMHardware()
{
}

GEMHardware::~GEMHardware()
{
}

GEMHardware::Client& GEMHardware::getClientLocked(pid_t pid)
{
    ssize_t index = mClients.indexOfKey(pid);

    if (index < 0) {
        Client client;

        LOGI("Creating client for %d", pid);

        client.pid = pid;
        client.count = 0;
        index = mClients.add(pid, client);
    }

    Client& client(mClients.editValueAt(index));

    return client;
}

sp<MemoryDealer> GEMHardware::requestLocked(Client &client)
{
    LOGD("GemDealer is requested locally");
    client.count++;

    return GemDealer::create();
}

sp<MemoryDealer> GEMHardware::request(int pid)
{
    Mutex::Autolock _l(mLock);
    Client& client = getClientLocked(pid);

    return requestLocked(client);
}

status_t GEMHardware::request(int pid, const sp<IGPUCallback>& callback,
        ISurfaceComposer::gpu_info_t* gpu)
{
    Mutex::Autolock _l(mLock);
    Client& client = getClientLocked(pid);

    client.count++;

    registerCallbackLocked(callback, client);

    return NO_ERROR;
}

void GEMHardware::revoke(int pid)
{
    Mutex::Autolock _l(mLock);
    Client& client = getClientLocked(pid);

    if (!client.count) {
        LOGW("%d revokes GPU without requesting it first", pid);
        return;
    }

    client.count--;
}

status_t GEMHardware::friendlyRevoke()
{
    return NO_ERROR;
}

void GEMHardware::unconditionalRevoke()
{
}

void GEMHardware::registerCallbackLocked(const sp<IGPUCallback>& callback,
        Client& client)
{
    sp<IBinder> binder = callback->asBinder();
    if (mRegisteredClients.add(binder, client.pid) >= 0)
        binder->linkToDeath(this);
}

void GEMHardware::binderDied(const wp<IBinder>& who)
{
    Mutex::Autolock _l(mLock);

    pid_t pid = mRegisteredClients.valueFor(who);
    if (pid <= 0)
	    return;

    ssize_t index = mClients.indexOfKey(pid);
    if (index < 0)
	    return;

    LOGI("Removing client for %d", pid);
    mClients.removeItemsAt(index);
}

};
