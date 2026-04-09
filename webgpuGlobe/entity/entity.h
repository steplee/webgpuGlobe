#pragma once

#include "renderable.h"

namespace wg {

    struct Entity : public Renderable { };

    std::shared_ptr<Entity> createSimpleTri(AppObjects& ao);
    std::shared_ptr<Entity> createSimpleTri2(AppObjects& ao);
    std::shared_ptr<Entity> createEllipsoid(AppObjects& ao, int rows, int cols, bool wgs84=true);
    std::shared_ptr<Entity> createSky(AppObjects& ao);

    struct TransformedEntity : public Renderable {
        virtual void setModelTransform(float model[16]) =0;
        virtual void setModelColor(float color[4]) =0;
    };

    std::shared_ptr<TransformedEntity> createTransformedSphere(AppObjects& ao, int rows, int cols);

}
