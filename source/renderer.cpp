#include <rasterizer/renderer.hpp>

#include <algorithm>
#include <cmath>

namespace rasterizer
{

	namespace
	{

		struct vertex
		{
			vector4f position;
			vector4f color;
		};

		vertex clip_intersect_edge(vertex const & v0, vertex const & v1, float value0, float value1)
		{
			// f(t) = at+b
			// f(0) = v0 = b
			// f(1) = v1 = a+v0 => a = v1 - v0
			// f(t) = v0 + (v1 - v0) * t
			// f(t) = 0 => t = -v0 / (v1 - v0) = v0 / (v0 - v1)

			float t = value0 / (value0 - value1);

			vertex v;
			v.position = (1.f - t) * v0.position + t * v1.position;
			v.color = (1.f - t) * v0.color + t * v1.color;

			return v;
		}

		vertex * clip_triangle(vertex * triangle, vector4f equation, vertex * result)
		{
			float values[3] =
			{
				dot(triangle[0].position, equation),
				dot(triangle[1].position, equation),
				dot(triangle[2].position, equation),
			};

			std::uint8_t mask = (values[0] < 0.f ? 1 : 0) | (values[1] < 0.f ? 2 : 0) | (values[2] < 0.f ? 4 : 0);

			switch (mask)
			{
			case 0b000:
				// All vertices are inside allowed half-space
				// No clipping required, copy the triangle to output
				*result++ = triangle[0];
				*result++ = triangle[1];
				*result++ = triangle[2];
				break;
			case 0b001:
				// Vertex 0 is outside allowed half-space
				// Replace it with points on edges 01 and 02
				// And re-triangulate
				{
					auto v01 = clip_intersect_edge(triangle[0], triangle[1], values[0], values[1]);
					auto v02 = clip_intersect_edge(triangle[0], triangle[2], values[0], values[2]);
					*result++ = v01;
					*result++ = triangle[1];
					*result++ = triangle[2];
					*result++ = v01;
					*result++ = triangle[2];
					*result++ = v02;
				}
				break;
			case 0b010:
				// Vertex 1 is outside allowed half-space
				// Replace it with points on edges 10 and 12
				// And re-triangulate
				{
					auto v10 = clip_intersect_edge(triangle[1], triangle[0], values[1], values[0]);
					auto v12 = clip_intersect_edge(triangle[1], triangle[2], values[1], values[2]);
					*result++ = triangle[0];
					*result++ = v10;
					*result++ = triangle[2];
					*result++ = triangle[2];
					*result++ = v10;
					*result++ = v12;
				}
				break;
			case 0b011:
				// Vertices 0 and 1 are outside allowed half-space
				// Replace them with points on edges 02 and 12
				*result++ = clip_intersect_edge(triangle[0], triangle[2], values[0], values[2]);
				*result++ = clip_intersect_edge(triangle[1], triangle[2], values[1], values[2]);
				*result++ = triangle[2];
				break;
			case 0b100:
				// Vertex 2 is outside allowed half-space
				// Replace it with points on edges 20 and 21
				// And re-triangulate
				{
					auto v20 = clip_intersect_edge(triangle[2], triangle[0], values[2], values[0]);
					auto v21 = clip_intersect_edge(triangle[2], triangle[1], values[2], values[1]);
					*result++ = triangle[0];
					*result++ = triangle[1];
					*result++ = v20;
					*result++ = v20;
					*result++ = triangle[1];
					*result++ = v21;
				}
				break;
			case 0b101:
				// Vertices 0 and 2 are outside allowed half-space
				// Replace them with points on edges 01 and 21
				*result++ = clip_intersect_edge(triangle[0], triangle[1], values[0], values[1]);
				*result++ = triangle[1];
				*result++ = clip_intersect_edge(triangle[2], triangle[1], values[2], values[1]);
				break;
			case 0b110:
				// Vertices 1 and 2 are outside allowed half-space
				// Replace them with points on edges 10 and 20
				*result++ = triangle[0];
				*result++ = clip_intersect_edge(triangle[1], triangle[0], values[1], values[0]);
				*result++ = clip_intersect_edge(triangle[2], triangle[0], values[2], values[0]);
				break;
				break;
			case 0b111:
				// All vertices are outside allowed half-space
				// Clip the whole triangle, result is empty
				break;
			}

			return result;
		}

		vertex * clip_triangle(vertex * begin, vertex * end)
		{
			static vector4f const equations[2] =
			{
				{0.f, 0.f,  1.f, 1.f}, // Z > -W  =>   Z + W > 0
				{0.f, 0.f, -1.f, 1.f}, // Z <  W  => - Z + W > 0
			};

			vertex result[12];

			for (auto equation : equations)
			{
				auto result_end = result;

				for (vertex * triangle = begin; triangle != end; triangle += 3)
					result_end = clip_triangle(triangle, equation, result_end);

				end = std::copy(result, result_end, begin);
			}

			return end;
		}

	}

	void clear(image_view const & color_buffer, vector4f const & color)
	{
		auto ptr = color_buffer.pixels;
		auto size = color_buffer.width * color_buffer.height;
		std::fill(ptr, ptr + size, to_color4ub(color));
	}

	void draw(image_view const & color_buffer, viewport const & viewport, draw_command const & command)
	{
		for (std::uint32_t vertex_index = 0; vertex_index + 2 < command.mesh.count; vertex_index += 3)
		{
			std::uint32_t i0 = vertex_index + 0;
			std::uint32_t i1 = vertex_index + 1;
			std::uint32_t i2 = vertex_index + 2;

			if (command.mesh.indices)
			{
				i0 = command.mesh.indices[i0];
				i1 = command.mesh.indices[i1];
				i2 = command.mesh.indices[i2];
			}

			vertex clipped_vertices[12];

			clipped_vertices[0].position = command.transform * as_point(command.mesh.positions[i0]);
			clipped_vertices[1].position = command.transform * as_point(command.mesh.positions[i1]);
			clipped_vertices[2].position = command.transform * as_point(command.mesh.positions[i2]);

			clipped_vertices[0].color = command.mesh.colors[i0];
			clipped_vertices[1].color = command.mesh.colors[i1];
			clipped_vertices[2].color = command.mesh.colors[i2];

			auto clipped_vertices_end = clip_triangle(clipped_vertices, clipped_vertices + 3);

			for (auto triangle_begin = clipped_vertices; triangle_begin != clipped_vertices_end; triangle_begin += 3)
			{
				auto v0 = triangle_begin[0];
				auto v1 = triangle_begin[1];
				auto v2 = triangle_begin[2];

				v0.position = perspective_divide(v0.position);
				v1.position = perspective_divide(v1.position);
				v2.position = perspective_divide(v2.position);

				v0.position = apply(viewport, v0.position);
				v1.position = apply(viewport, v1.position);
				v2.position = apply(viewport, v2.position);

				float det012 = det2D(v1.position - v0.position, v2.position - v0.position);

				bool const ccw = det012 < 0.f;

				switch (command.cull_mode)
				{
				case cull_mode::none:
					if (ccw)
					{
						std::swap(v1, v2);
						det012 = -det012;
					}
					break;
				case cull_mode::cw:
					if (!ccw)
						continue;
					std::swap(v1, v2);
					det012 = -det012;
					break;
				case cull_mode::ccw:
					if (ccw)
						continue;
					break;
				}

				std::int32_t xmin = std::max<std::int32_t>(viewport.xmin, 0);
				std::int32_t xmax = std::min<std::int32_t>(viewport.xmax, color_buffer.width) - 1;
				std::int32_t ymin = std::max<std::int32_t>(viewport.ymin, 0);
				std::int32_t ymax = std::min<std::int32_t>(viewport.ymax, color_buffer.height) - 1;

				xmin = std::max<float>(xmin, std::min({std::floor(v0.position.x), std::floor(v1.position.x), std::floor(v2.position.x)}));
				xmax = std::min<float>(xmax, std::max({std::floor(v0.position.x), std::floor(v1.position.x), std::floor(v2.position.x)}));
				ymin = std::max<float>(ymin, std::min({std::floor(v0.position.y), std::floor(v1.position.y), std::floor(v2.position.y)}));
				ymax = std::min<float>(ymax, std::max({std::floor(v0.position.y), std::floor(v1.position.y), std::floor(v2.position.y)}));

				for (std::int32_t y = ymin; y <= ymax; ++y)
				{
					for (std::int32_t x = xmin; x <= xmax; ++x)
					{
						vector4f p{x + 0.5f, y + 0.5f, 0.f, 0.f};

						float det01p = det2D(v1.position - v0.position, p - v0.position);
						float det12p = det2D(v2.position - v1.position, p - v1.position);
						float det20p = det2D(v0.position - v2.position, p - v2.position);

						if (det01p >= 0.f && det12p >= 0.f && det20p >= 0.f)
						{
							float l0 = det12p / det012 * v0.position.w;
							float l1 = det20p / det012 * v1.position.w;
							float l2 = det01p / det012 * v2.position.w;

							float lsum = l0 + l1 + l2;

							l0 /= lsum;
							l1 /= lsum;
							l2 /= lsum;

							color_buffer.at(x, y) = to_color4ub(l0 * v0.color + l1 * v1.color + l2 * v2.color);
						}
					}
				}
			}
		}
	}

}
