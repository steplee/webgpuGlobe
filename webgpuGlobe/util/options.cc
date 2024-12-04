#include "options.h"

#include <regex>
#include <stdexcept>

namespace wg {

	GlobeOptions parseArgs(const char* argv[], int argc) {
		GlobeOptions opts;

		auto push = [&opts](const std::string& key, GlobeOption&& o) {
			opts.opts[key] = o;
		};

		for (int i=0; i<argc; i++) {
			std::string_view s(argv[i]);
			auto equalSign = s.find("=");
			if (equalSign == std::string::npos) continue;

			std::string key { s.substr(0, equalSign) };
			std::string val { s.substr(equalSign+1) };

			if (val.length() == 0) {
				throw std::runtime_error(fmt::format("failed to parse value '{}' in arg '{}'", val, s));
			}

			auto tryNumber = [&](const std::string& k) {
				double d=0;
				int n = sscanf(k.c_str(), "%lf ", &d);
				spdlog::get("wg")->info("tryNumber scan '{}', n={} -> {}", k, n, d);
				if (n > 0) return std::make_pair(n, d);
				return std::make_pair(0, 0.);
			};
			auto tryString = [&](const std::string& k) {
				char buf[128] = {0};

				// std::regex adds 2mb to binary...
				{
					std::smatch mr;
					std::regex pat ("\"(.*)\"");
					if (std::regex_match(k, mr, pat)) {
						int32_t left = mr.position() + 1;
						int32_t right = mr.position() + 1 + mr.length() - 2;
						spdlog::get("wg")->info("tryString scan n={} -> {}", mr.length(), std::string{k.substr(left,right-left)});
						return std::make_pair((int)mr.length(), std::string{k.substr(left,right-left)});
					}
				}

				{
					std::smatch mr;
					std::regex pat ("'(.*)'");
					if (std::regex_match(k, mr, pat)) {
						int32_t left = mr.position() + 1;
						int32_t right = mr.position() + 1 + mr.length() - 2;
						spdlog::get("wg")->info("tryString scan n={} -> {}", mr.length(), std::string{k.substr(left,right-left)});
						return std::make_pair((int)mr.length(), std::string{k.substr(left,right-left)});
					}
				}

				{
					std::smatch mr;
					std::regex pat ("([^,]+)");
					if (std::regex_search(k, mr, pat)) {
						int32_t left = mr.position();
						int32_t right = mr.position() + mr.length();
						spdlog::get("wg")->info("tryString scan n={} -> {}", mr.length(), std::string{k.substr(left,right-left)});
						return std::make_pair((int)mr.length(), std::string{k.substr(left,right-left)});
					}
				}
				return std::make_pair(0, std::string());
			};

			if (val[0] == '[') {
				assert(val.back() == ']');

				if (val.length() == 2) {
					push(key, std::vector<std::string>{});
					continue;
				}

				std::vector<std::string> strings;
				std::vector<double> numbers;

				for (uint32_t i=1; i<val.size()-1; i++) {
					if (auto pair = tryNumber(val.substr(i, val.size()-1-i)); pair.first > 0) {
						uint32_t ii = i + pair.first;
						while (val[ii] != ',' and val[ii] != ']') ii++;
						i = ii;
						numbers.push_back(pair.second);
					} else if (auto pair = tryString(val.substr(i, val.size()-1-i)); pair.first > 0) {
						uint32_t ii = i + pair.first;
						while (val[ii] == ' ') ii++;
						assert(val[ii] == ',');
						i = ii;
						strings.push_back(pair.second);
					} else {
						throw std::runtime_error(fmt::format("failed to parse value '{}' in arg '{}'", val, s));
					}
				}

				assert((strings.size() > 0) ^ (numbers.size() > 0));
				if (strings.size()) push(key, strings);
				else if (numbers.size()) push(key, numbers);
				else {
					spdlog::get("wg")->warn("parseArgs encounted empty list. Unsure if vector<string> or vector<number>. Defaulting to string.");
					push(key,strings);
				}
			} else if (auto pair = tryNumber(val); pair.first > 0) {
				push(key, pair.second);
			} else if (auto pair = tryString(val); pair.first > 0) {
				push(key, pair.second);
			} else {
				spdlog::get("wg")->warn("parseArgs failed to parse val '{}' in arg '{}'", val, s);
			}
		}



		std::string keys;
		int i = 0;
		for (const auto &kv : opts.opts) {
			keys += kv.first;
			i++;
			if (i < opts.opts.size()) keys += ", ";
		}
		spdlog::get("wg")->info("parseArgs returning GlobeOptions with keys [{}]", keys);

		return opts;
	}

}
