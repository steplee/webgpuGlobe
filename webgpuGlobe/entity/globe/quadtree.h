#pragma once
#include "globe.h"

namespace wg {

    struct QuadtreeCoordinate {
		uint64_t c;
		constexpr static int MaxChildren = 4;

        inline QuadtreeCoordinate() {}
        inline QuadtreeCoordinate(uint32_t z, uint32_t y, uint32_t x) {
            c = (static_cast<uint64_t>(z) << 58) | (static_cast<uint64_t>(y) << 29) | x;
        }

        uint32_t z() const {
            return (c >> 58) & 0b11111;
        }
        uint32_t y() const {
            return (c >> 29) & ((1 << 29) - 1);
        }
        uint32_t x() const {
            return (c >> 0) & ((1 << 29) - 1);
        }

        inline QuadtreeCoordinate parent() const {
            if (this->z() == 0) return QuadtreeCoordinate { 0, 0, 0 };
            return QuadtreeCoordinate { z() - 1, y() / 2, x() / 2 };
        }

        inline QuadtreeCoordinate child(uint32_t childIndex) const {
            assert(childIndex >= 0 and childIndex < 4);
            static constexpr uint32_t dx[4] = { 0, 1, 1, 0 };
            static constexpr uint32_t dy[4] = { 0, 0, 1, 1 };
            return QuadtreeCoordinate {
                z() + 1,
                y() * 2 + dy[childIndex],
                x() * 2 + dx[childIndex],
            };
        }

		inline Vector4d getWmTlbr() const {
			constexpr double WmDiameter    = Earth::WebMercatorScale * 2;
			uint32_t xx = x(), yy = y(), zz = z();
			return {
				(static_cast<double>(xx    ) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
				(static_cast<double>(yy    ) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
				(static_cast<double>(xx + 1) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
				(static_cast<double>(yy + 1) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
			};
		}

		inline bool operator==(const QuadtreeCoordinate& o) const {
			return c == o.c;
		}

    };

}

template <> struct fmt::formatter<wg::QuadtreeCoordinate> : fmt::formatter<std::string_view> {
    auto format(wg::QuadtreeCoordinate c, fmt::format_context& ctx) const -> format_context::iterator {
        fmt::format_to(ctx.out(), "<{}: {}, {}>", c.z(), c.y(), c.x());
        return ctx.out();
    }
};

template<>
struct std::hash<wg::QuadtreeCoordinate>
{
    std::size_t operator()(const wg::QuadtreeCoordinate& s) const noexcept
    {
        return std::hash<uint64_t>{}(s.c);
    }
};
