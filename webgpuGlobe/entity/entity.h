#pragma once

#include "renderable.h"

namespace wg {

    struct Entity : public Renderable { };

    std::shared_ptr<Entity> createSimpleTri(AppObjects& ao);
    std::shared_ptr<Entity> createSimpleTri2(AppObjects& ao);
    std::shared_ptr<Entity> createEllipsoid(AppObjects& ao, int rows, int cols);
    std::shared_ptr<Entity> createSky(AppObjects& ao);

}
