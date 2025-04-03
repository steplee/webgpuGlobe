#pragma once
#include "../globe.h"

namespace wg {

	struct OctreeCoordinate;

	struct EncodedOctreeCoordinate {
		char key[23];
		uint8_t keyLen = 0;

		EncodedOctreeCoordinate(const OctreeCoordinate& c);
		inline EncodedOctreeCoordinate() {
			memset(this, 0, sizeof(*this));
		}
	};

    struct OctreeCoordinate {
		std::string s;
		constexpr static int MaxChildren = 4;

        inline OctreeCoordinate() {}
        inline OctreeCoordinate(std::string_view ss) : s(ss) {
        }
        inline OctreeCoordinate(const EncodedOctreeCoordinate& c) {
			s.resize(c.keyLen);
			for (uint8_t i=0; i<c.keyLen; i++) {
				s[i] = c.key[i];
			}
		}

		inline bool isBaseLevel() const { return s.length() == 0; }

        inline OctreeCoordinate parent() const {
            if (s.length() == 0) return OctreeCoordinate { "" };
			std::string ss = s;
			ss.pop_back();
            return OctreeCoordinate {ss};
        }

        inline OctreeCoordinate child(uint32_t childIndex) const {
            assert(childIndex >= 0 and childIndex < 8);
			return OctreeCoordinate{fmt::format("{}{}", s, childIndex)};
        }

		inline Vector4d getWmTlbr() const {
			throw std::runtime_error("todo");
		}

		inline bool operator==(const OctreeCoordinate& o) const {
			return s == o.s;
		}

		using EncodedCoordinate = EncodedOctreeCoordinate;
    };

	inline EncodedOctreeCoordinate::EncodedOctreeCoordinate(const OctreeCoordinate& c) {
		memset(key, 0, sizeof(key));
		strcpy(key, c.s.c_str());
		keyLen = c.s.length();
	}

}

template <> struct fmt::formatter<wg::OctreeCoordinate> : fmt::formatter<std::string_view> {
    auto format(wg::OctreeCoordinate c, fmt::format_context& ctx) const -> format_context::iterator {
        fmt::format_to(ctx.out(), "'{}'", c.s);
        return ctx.out();
    }
};

template<>
struct std::hash<wg::OctreeCoordinate>
{
    std::size_t operator()(const wg::OctreeCoordinate& s) const noexcept
    {
        return std::hash<std::string>{}(s.s);
    }
};

