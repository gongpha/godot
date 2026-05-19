/**************************************************************************/
/*  image_loader_ico.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "image_loader_ico.h"

#include "core/error/error_macros.h"
#include "core/io/file_access_memory.h"
#include "core/io/image.h"

// Helper functions for safe Little-Endian reading
static uint16_t read_u16_le(const uint8_t *ptr) {
	return ptr[0] | (ptr[1] << 8);
}
static uint32_t read_u32_le(const uint8_t *ptr) {
	return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

Error ImageLoaderICO::load_image(Ref<Image> p_image, Ref<FileAccess> p_fileaccess, BitField<ImageFormatLoader::LoaderFlags> p_flags, float p_scale) {
	uint64_t size = p_fileaccess->get_length();
	Vector<uint8_t> file_data;
	Error err = file_data.resize(size);
	if (err != OK) {
		return err;
	}

	uint64_t read_bytes = p_fileaccess->get_buffer(file_data.ptrw(), size);
	if (read_bytes != size) {
		return ERR_FILE_CORRUPT;
	}

	// ICO Header: 0=Reserved(2), 2=Type(2), 4=Count(2)
	if (file_data.size() < 6 || read_u16_le(&file_data[0]) != 0 || read_u16_le(&file_data[2]) != 1) {
		return ERR_FILE_UNRECOGNIZED;
	}

	int num_images = read_u16_le(&file_data[4]);
	if (file_data.size() < 6 + num_images * 16) {
		return ERR_FILE_CORRUPT;
	}

	// Find the largest, highest-quality icon entry
	int best_idx = -1;
	int best_score = -1;

	for (int i = 0; i < num_images; ++i) {
		size_t entry_offset = 6 + i * 16;
		int w = file_data[entry_offset];
		if (w == 0) {
			w = 256;
		}
		int h = file_data[entry_offset + 1];
		if (h == 0) {
			h = 256;
		}
		int bpp = read_u16_le(&file_data[entry_offset + 6]);
		if (bpp == 0) {
			bpp = 32; // Fallback estimate
		}

		int score = w * h * bpp;
		if (score > best_score) {
			best_score = score;
			best_idx = i;
		}
	}

	if (best_idx == -1) {
		return ERR_FILE_CORRUPT;
	}

	size_t entry_offset = 6 + best_idx * 16;
	uint32_t bytesInRes = read_u32_le(&file_data[entry_offset + 8]);
	uint32_t imageOffset = read_u32_le(&file_data[entry_offset + 12]);

	if (imageOffset + bytesInRes > file_data.size()) {
		return ERR_FILE_CORRUPT;
	}
	const uint8_t *payload = &file_data[imageOffset];

	// Check if the payload is a PNG (starts with standard PNG magic number)
	bool is_png = (bytesInRes >= 8 && memcmp(payload, "\x89PNG\r\n\x1a\n", 8) == 0);

	if (is_png) {
		// --- DECODE PNG PAYLOAD USING GODOT NATIVE LOADER ---
		Vector<uint8_t> png_buffer;
		png_buffer.resize(bytesInRes);
		memcpy(png_buffer.ptrw(), payload, bytesInRes);
		return p_image->load_png_from_buffer(png_buffer);
	} else {
		// --- DECODE DIB (BMP) PAYLOAD ---
		if (bytesInRes < 40) {
			return ERR_FILE_CORRUPT; // Minimum size for BITMAPINFOHEADER
		}

		uint32_t biSize = read_u32_le(payload);
		int32_t biWidth = static_cast<int32_t>(read_u32_le(payload + 4));
		int32_t biHeight = static_cast<int32_t>(read_u32_le(payload + 8));
		uint16_t biBitCount = read_u16_le(payload + 14);
		uint32_t biCompression = read_u32_le(payload + 16);

		// In ICO files, the BMP header height includes both XOR (color) and AND (mask) layers.
		// So the actual image height is halved.
		int w = biWidth;
		int h = biHeight / 2;

		if (biCompression != 0 || (biBitCount != 32 && biBitCount != 24)) {
			// Unsupported BMP format in ICO (Only 24/32-bit uncompressed supported).
			return ERR_FILE_UNRECOGNIZED;
		}

		Vector<uint8_t> image_data;
		image_data.resize(w * h * 4);
		image_data.fill(0);
		uint8_t *dst_data = image_data.ptrw();

		const uint8_t *pixels = payload + biSize; // Start of pixel data

		if (biBitCount == 32) {
			int stride = w * 4;
			for (int y = 0; y < h; ++y) {
				// BMP stores rows bottom-up
				const uint8_t *src_row = pixels + (h - 1 - y) * stride;
				uint8_t *dst_row = dst_data + y * w * 4;
				for (int x = 0; x < w; ++x) {
					dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R
					dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
					dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B
					dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
				}
			}
		} else if (biBitCount == 24) {
			// Strides in BMP are padded to multiples of 4 bytes
			int stride = ((w * 24 + 31) / 32) * 4;
			int and_stride = ((w * 1 + 31) / 32) * 4; // 1-bit transparency mask

			const uint8_t *and_mask = pixels + stride * h; // AND mask follows the XOR mask

			for (int y = 0; y < h; ++y) {
				const uint8_t *src_row = pixels + (h - 1 - y) * stride;
				const uint8_t *mask_row = and_mask + (h - 1 - y) * and_stride;
				uint8_t *dst_row = dst_data + y * w * 4;
				for (int x = 0; x < w; ++x) {
					dst_row[x * 4 + 0] = src_row[x * 3 + 2]; // R
					dst_row[x * 4 + 1] = src_row[x * 3 + 1]; // G
					dst_row[x * 4 + 2] = src_row[x * 3 + 0]; // B

					// Read 1-bit transparency mask (0 = opaque, 1 = transparent)
					bool transparent = (mask_row[x / 8] & (0x80 >> (x % 8))) != 0;
					dst_row[x * 4 + 3] = transparent ? 0 : 255;
				}
			}
		}

		p_image->set_data(w, h, false, Image::FORMAT_RGBA8, image_data);
		return OK;
	}
}

void ImageLoaderICO::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ico");
}

static Ref<Image> _ico_mem_loader_func(const uint8_t *p_ico, int p_size) {
	Ref<FileAccessMemory> memfile;
	memfile.instantiate();
	Error open_memfile_error = memfile->open_custom(p_ico, p_size);
	ERR_FAIL_COND_V_MSG(open_memfile_error, Ref<Image>(), "Could not create memfile for ICO image buffer.");

	Ref<Image> img;
	img.instantiate();
	Error load_error = ImageLoaderICO().load_image(img, memfile, ImageFormatLoader::FLAG_NONE, 1.0f);
	ERR_FAIL_COND_V_MSG(load_error, Ref<Image>(), "Failed to load ICO image.");
	return img;
}

ImageLoaderICO::ImageLoaderICO() {
	Image::_ico_mem_loader_func = _ico_mem_loader_func;
}
