/*
 * Modern effects for a modern Streamer
 * Copyright (C) 2017 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "gs-vertexbuffer.hpp"
#include "obs/gs/gs-helper.hpp"

#include "warning-disable.hpp"
#include <stdexcept>
#include "warning-enable.hpp"

void streamfx::obs::gs::vertex_buffer::initialize(uint32_t capacity, uint8_t layers)
{
	finalize();

	if (capacity > MAXIMUM_VERTICES) {
		throw std::out_of_range("capacity");
	}
	if (layers > MAXIMUM_UVW_LAYERS) {
		throw std::out_of_range("layers");
	}

	// Allocate memory for data.
	_data          = std::make_shared<decltype(_data)::element_type>();
	_data->num     = _capacity;
	_data->num_tex = _layers;
	_data->points = _positions = static_cast<vec3*>(streamfx::util::malloc_aligned(16, sizeof(vec3) * _capacity));
	_data->normals = _normals = static_cast<vec3*>(streamfx::util::malloc_aligned(16, sizeof(vec3) * _capacity));
	_data->tangents = _tangents = static_cast<vec3*>(streamfx::util::malloc_aligned(16, sizeof(vec3) * _capacity));
	_data->colors = _colors = static_cast<uint32_t*>(streamfx::util::malloc_aligned(16, sizeof(uint32_t) * _capacity));

	// Clear the allocated memory of any data.
	memset(_positions, 0, sizeof(vec3) * _capacity);
	memset(_normals, 0, sizeof(vec3) * _capacity);
	memset(_tangents, 0, sizeof(vec3) * _capacity);
	memset(_colors, 0, sizeof(uint32_t) * _capacity);

	if (_layers == 0) {
		_data->tvarray = nullptr;
	} else {
		_data->tvarray = _uv_layers =
			static_cast<gs_tvertarray*>(streamfx::util::malloc_aligned(16, sizeof(gs_tvertarray) * _layers));
		for (uint8_t n = 0; n < _layers; n++) {
			_uv_layers[n].array = _uvs[n] =
				static_cast<vec4*>(streamfx::util::malloc_aligned(16, sizeof(vec4) * _capacity));
			_uv_layers[n].width = 4;
			memset(_uvs[n], 0, sizeof(vec4) * _capacity);
		}
	}

	// Allocate actual GPU vertex buffer.
	{
		auto gctx = streamfx::obs::gs::context();
		_buffer   = decltype(_buffer)(gs_vertexbuffer_create(_data.get(), GS_DYNAMIC | GS_DUP_BUFFER),
                                    [this](gs_vertbuffer_t* v) {
                                        try {
                                            auto gctx = streamfx::obs::gs::context();
                                            gs_vertexbuffer_destroy(v);
                                        } catch (...) {
                                            if (obs_get_version() < MAKE_SEMANTIC_VERSION(26, 0, 0)) {
                                                // Fixes a memory leak with OBS Studio versions older than 26.x.
                                                gs_vbdata_destroy(_obs_data);
                                            }
                                        }
                                    });
		_obs_data = gs_vertexbuffer_get_data(_buffer.get());
	}

	if (!_buffer) {
		throw std::runtime_error("Failed to create vertex buffer.");
	}
}

void streamfx::obs::gs::vertex_buffer::finalize()
{
	// Free data
	streamfx::util::free_aligned(_positions);
	streamfx::util::free_aligned(_normals);
	streamfx::util::free_aligned(_tangents);
	streamfx::util::free_aligned(_colors);
	streamfx::util::free_aligned(_uv_layers);
	for (std::size_t n = 0; n < _layers; n++) {
		streamfx::util::free_aligned(_uvs[n]);
	}

	_buffer.reset();
	_data.reset();
}

streamfx::obs::gs::vertex_buffer::~vertex_buffer()
{
	finalize();
}

streamfx::obs::gs::vertex_buffer::vertex_buffer(uint32_t size, uint8_t layers)
	: _capacity(size), _size(size), _layers(layers),

	  _buffer(nullptr), _data(nullptr),

	  _positions(nullptr), _normals(nullptr), _tangents(nullptr), _colors(nullptr), _uv_layers(nullptr), _uvs(),

	  _obs_data(nullptr)
{
	initialize(_size, _layers);
}

streamfx::obs::gs::vertex_buffer::vertex_buffer(gs_vertbuffer_t* vb)
	: _capacity(0), _size(0), _layers(0),

	  _buffer(nullptr), _data(nullptr),

	  _positions(nullptr), _normals(nullptr), _tangents(nullptr), _colors(nullptr), _uv_layers(nullptr), _uvs(),

	  _obs_data(nullptr)
{
	auto        gctx = streamfx::obs::gs::context();
	gs_vb_data* vbd  = gs_vertexbuffer_get_data(vb);
	if (!vbd)
		throw std::runtime_error("vertex buffer with no data");

	initialize(static_cast<uint32_t>(vbd->num), static_cast<uint8_t>(vbd->num_tex));

	if (_positions && vbd->points)
		memcpy(_positions, vbd->points, vbd->num * sizeof(vec3));
	if (_normals && vbd->normals)
		memcpy(_normals, vbd->normals, vbd->num * sizeof(vec3));
	if (_tangents && vbd->tangents)
		memcpy(_tangents, vbd->tangents, vbd->num * sizeof(vec3));
	if (_colors && vbd->colors)
		memcpy(_colors, vbd->colors, vbd->num * sizeof(uint32_t));
	if (vbd->tvarray != nullptr) {
		for (std::size_t n = 0; n < vbd->num_tex; n++) {
			if (vbd->tvarray[n].array != nullptr && vbd->tvarray[n].width <= 4 && vbd->tvarray[n].width > 0) {
				if (vbd->tvarray[n].width == 4) {
					memcpy(_uvs[n], vbd->tvarray[n].array, vbd->num * sizeof(vec4));
				} else if (vbd->tvarray[n].width < 4) {
					for (std::size_t idx = 0; idx < _capacity; idx++) {
						float* mem = reinterpret_cast<float*>(vbd->tvarray[n].array) + (idx * vbd->tvarray[n].width);
						memset(&_uvs[n][idx], 0, sizeof(vec4));
						memcpy(&_uvs[n][idx], mem, vbd->tvarray[n].width);
					}
				}
			}
		}
	}
}

streamfx::obs::gs::vertex_buffer::vertex_buffer(vertex_buffer const& other)
	: vertex_buffer(other._capacity, other._layers)
{ // Copy Constructor
	memcpy(_positions, other._positions, _capacity * sizeof(vec3));
	memcpy(_normals, other._normals, _capacity * sizeof(vec3));
	memcpy(_tangents, other._tangents, _capacity * sizeof(vec3));
	memcpy(_colors, other._colors, _capacity * sizeof(vec3));
	for (std::size_t n = 0; n < other._layers; n++) {
		memcpy(_uvs[n], other._uvs[n], _capacity * sizeof(vec4));
	}
}

void streamfx::obs::gs::vertex_buffer::operator=(vertex_buffer const& other)
{ // Copy operator
	initialize(other._capacity, other._layers);
	_size = other._size;

	// Copy actual data over.
	memcpy(_positions, other._positions, other._capacity * sizeof(vec3));
	memcpy(_normals, other._normals, other._capacity * sizeof(vec3));
	memcpy(_tangents, other._tangents, other._capacity * sizeof(vec3));
	memcpy(_colors, other._colors, other._capacity * sizeof(uint32_t));
	memcpy(_uv_layers, other._uv_layers, sizeof(gs_tvertarray));
	for (std::size_t n = 0; n < other._layers; n++) {
		memcpy(_uvs[n], other._uvs[n], _capacity * sizeof(vec4));
	}
}

streamfx::obs::gs::vertex_buffer::vertex_buffer(vertex_buffer const&& other) noexcept
{ // Move Constructor
	_capacity  = other._capacity;
	_size      = other._size;
	_layers    = other._layers;
	_buffer    = other._buffer;
	_data      = other._data;
	_positions = other._positions;
	_normals   = other._normals;
	_tangents  = other._tangents;
	_colors    = other._colors;
	_uv_layers = other._uv_layers;
	for (std::size_t n = 0; n < MAXIMUM_UVW_LAYERS; n++) {
		_uvs[n] = other._uvs[n];
	}
	_obs_data = other._obs_data;
}

void streamfx::obs::gs::vertex_buffer::operator=(vertex_buffer const&& other) noexcept
{ // Move Assignment
	finalize();

	_capacity  = other._capacity;
	_size      = other._size;
	_layers    = other._layers;
	_buffer    = other._buffer;
	_data      = other._data;
	_positions = other._positions;
	_normals   = other._normals;
	_tangents  = other._tangents;
	_colors    = other._colors;
	_uv_layers = other._uv_layers;
	for (std::size_t n = 0; n < MAXIMUM_UVW_LAYERS; n++) {
		_uvs[n] = other._uvs[n];
	}
	_obs_data = other._obs_data;
}

void streamfx::obs::gs::vertex_buffer::resize(uint32_t size)
{
	if (size > _capacity) {
		throw std::out_of_range("size larger than capacity");
	}
	_size = size;
}

uint32_t streamfx::obs::gs::vertex_buffer::size()
{
	return _size;
}

uint32_t streamfx::obs::gs::vertex_buffer::capacity()
{
	return _capacity;
}

bool streamfx::obs::gs::vertex_buffer::empty()
{
	return _size == 0;
}

const streamfx::obs::gs::vertex streamfx::obs::gs::vertex_buffer::at(uint32_t idx)
{
	if (idx >= _size) {
		throw std::out_of_range("idx out of range");
	}

	streamfx::obs::gs::vertex vtx(&_positions[idx], &_normals[idx], &_tangents[idx], &_colors[idx], nullptr);
	for (std::size_t n = 0; n < _layers; n++) {
		vtx.uv[n] = &_uvs[n][idx];
	}
	return vtx;
}

const streamfx::obs::gs::vertex streamfx::obs::gs::vertex_buffer::operator[](uint32_t const pos)
{
	return at(pos);
}

void streamfx::obs::gs::vertex_buffer::set_uv_layers(uint8_t layers)
{
	_layers = layers;
}

uint8_t streamfx::obs::gs::vertex_buffer::get_uv_layers()
{
	return _layers;
}

vec3* streamfx::obs::gs::vertex_buffer::get_positions()
{
	return _positions;
}

vec3* streamfx::obs::gs::vertex_buffer::get_normals()
{
	return _normals;
}

vec3* streamfx::obs::gs::vertex_buffer::get_tangents()
{
	return _tangents;
}

uint32_t* streamfx::obs::gs::vertex_buffer::get_colors()
{
	return _colors;
}

vec4* streamfx::obs::gs::vertex_buffer::get_uv_layer(uint8_t idx)
{
	if (idx >= _layers) {
		throw std::out_of_range("idx out of range");
	}
	return _uvs[idx];
}

gs_vertbuffer_t* streamfx::obs::gs::vertex_buffer::update(bool refreshGPU)
{
	if (refreshGPU) {
		auto gctx = streamfx::obs::gs::context();
		gs_vertexbuffer_flush_direct(_buffer.get(), _data.get());
		_obs_data = gs_vertexbuffer_get_data(_buffer.get());
	}
	return _buffer.get();
}

gs_vertbuffer_t* streamfx::obs::gs::vertex_buffer::update()
{
	return update(true);
}
