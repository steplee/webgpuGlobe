#include "../globe.h"

namespace wg {
    std::shared_ptr<Globe> make_gearth_globe(AppObjects& ao, const GlobeOptions& opts) {
		throw std::runtime_error("Tried to call make_gearth_globe, but compile without google earth support.");
    }
}
