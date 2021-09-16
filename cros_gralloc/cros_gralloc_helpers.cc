/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_gralloc_helpers.h"

#include <hardware/gralloc.h>
#include <sync/sync.h>

/* Define to match AIDL BufferUsage::VIDEO_DECODER. */
#define BUFFER_USAGE_VIDEO_DECODER (1 << 22)

/* Define to match AIDL BufferUsage::GPU_DATA_BUFFER. */
#define BUFFER_USAGE_GPU_DATA_BUFFER (1 << 24)

uint32_t cros_gralloc_convert_format(int format)
{
	/*
	 * Conversion from HAL to fourcc-based DRV formats based on
	 * platform_android.c in mesa.
	 */

	switch (format) {
	case HAL_PIXEL_FORMAT_BGRA_8888:
		return DRM_FORMAT_ARGB8888;
	case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
		return DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED;
	case HAL_PIXEL_FORMAT_RAW16:
		return DRM_FORMAT_R16;
	case HAL_PIXEL_FORMAT_RGB_565:
		return DRM_FORMAT_RGB565;
	case HAL_PIXEL_FORMAT_RGB_888:
		return DRM_FORMAT_BGR888;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		return DRM_FORMAT_ABGR8888;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		return DRM_FORMAT_XBGR8888;
	case HAL_PIXEL_FORMAT_YCbCr_420_888:
		return DRM_FORMAT_FLEX_YCbCr_420_888;
	case HAL_PIXEL_FORMAT_YV12:
		return DRM_FORMAT_YVU420_ANDROID;
	/*
	 * Choose DRM_FORMAT_R8 because <system/graphics.h> requires the buffers
	 * with a format HAL_PIXEL_FORMAT_BLOB have a height of 1, and width
	 * equal to their size in bytes.
	 */
	case HAL_PIXEL_FORMAT_BLOB:
		return DRM_FORMAT_R8;
#if ANDROID_API_LEVEL >= 26
	case HAL_PIXEL_FORMAT_RGBA_1010102:
		return DRM_FORMAT_ABGR2101010;
	case HAL_PIXEL_FORMAT_RGBA_FP16:
		return DRM_FORMAT_ABGR16161616F;
#endif
	}

	return DRM_FORMAT_NONE;
}

uint64_t cros_gralloc_convert_usage(uint64_t usage)
{
	uint64_t use_flags = BO_USE_NONE;

	if (usage & GRALLOC_USAGE_CURSOR)
		use_flags |= BO_USE_NONE;
	if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_RARELY)
		use_flags |= BO_USE_SW_READ_RARELY;
	if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN)
		use_flags |= BO_USE_SW_READ_OFTEN;
	if ((usage & GRALLOC_USAGE_SW_WRITE_MASK) == GRALLOC_USAGE_SW_WRITE_RARELY)
		use_flags |= BO_USE_SW_WRITE_RARELY;
	if ((usage & GRALLOC_USAGE_SW_WRITE_MASK) == GRALLOC_USAGE_SW_WRITE_OFTEN)
		use_flags |= BO_USE_SW_WRITE_OFTEN;
	if (usage & GRALLOC_USAGE_HW_TEXTURE)
		use_flags |= BO_USE_TEXTURE;
	if (usage & GRALLOC_USAGE_HW_RENDER)
		use_flags |= BO_USE_RENDERING;
	if (usage & GRALLOC_USAGE_HW_2D)
		use_flags |= BO_USE_RENDERING;
	if (usage & GRALLOC_USAGE_HW_COMPOSER)
		/* HWC wants to use display hardware, but can defer to OpenGL. */
		use_flags |= BO_USE_SCANOUT | BO_USE_TEXTURE;
	if (usage & GRALLOC_USAGE_HW_FB)
		use_flags |= BO_USE_NONE;
	if (usage & GRALLOC_USAGE_EXTERNAL_DISP)
		/*
		 * This flag potentially covers external display for the normal drivers (i915,
		 * rockchip) and usb monitors (evdi/udl). It's complicated so ignore it.
		 * */
		use_flags |= BO_USE_NONE;
	/* Map this flag to linear until real HW protection is available on Android. */
	if (usage & GRALLOC_USAGE_PROTECTED)
		use_flags |= BO_USE_LINEAR;
	if (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
		use_flags |= BO_USE_HW_VIDEO_ENCODER;
		/*HACK: See b/30054495 */
		use_flags |= BO_USE_SW_READ_OFTEN;
	}
	if (usage & GRALLOC_USAGE_HW_CAMERA_WRITE)
		use_flags |= BO_USE_CAMERA_WRITE;
	if (usage & GRALLOC_USAGE_HW_CAMERA_READ)
		use_flags |= BO_USE_CAMERA_READ;
	if (usage & GRALLOC_USAGE_RENDERSCRIPT)
		use_flags |= BO_USE_RENDERSCRIPT;
	if (usage & BUFFER_USAGE_VIDEO_DECODER)
		use_flags |= BO_USE_HW_VIDEO_DECODER;
	if (usage & BUFFER_USAGE_FRONT_RENDERING)
		use_flags |= BO_USE_FRONT_RENDERING;
	if (usage & BUFFER_USAGE_GPU_DATA_BUFFER)
		use_flags |= BO_USE_GPU_DATA_BUFFER;

	return use_flags;
}

cros_gralloc_handle_t cros_gralloc_convert_handle(buffer_handle_t handle)
{
	auto hnd = reinterpret_cast<cros_gralloc_handle_t>(handle);
	if (!hnd || hnd->magic != cros_gralloc_magic)
		return nullptr;

	return hnd;
}

int32_t cros_gralloc_sync_wait(int32_t fence, bool close_fence)
{
	if (fence < 0)
		return 0;

	/*
	 * Wait initially for 1000 ms, and then wait indefinitely. The SYNC_IOC_WAIT
	 * documentation states the caller waits indefinitely on the fence if timeout < 0.
	 */
	int err = sync_wait(fence, 1000);
	if (err < 0) {
		drv_log("Timed out on sync wait, err = %s\n", strerror(errno));
		err = sync_wait(fence, -1);
		if (err < 0) {
			drv_log("sync wait error = %s\n", strerror(errno));
			return -errno;
		}
	}

	if (close_fence) {
		err = close(fence);
		if (err) {
			drv_log("Unable to close fence fd, err = %s\n", strerror(errno));
			return -errno;
		}
	}

	return 0;
}

std::string get_drm_format_string(uint32_t drm_format)
{
	char *sequence = (char *)&drm_format;
	std::string s(sequence, 4);
	return "DRM_FOURCC_" + s;
}
