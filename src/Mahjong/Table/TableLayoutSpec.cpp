#include "TableLayoutSpec.h"

namespace BECore::Mahjong {

    const ShapeSpec* ZonesOf(const TableLayoutSpec& spec, TableShape shape) noexcept {
        const ShapeSpec& own = spec.Shape(shape);
        if (!own.present) {
            return nullptr;
        }
        if (!own.like) {
            return &own;
        }
        const ShapeSpec& source = spec.Shape(own.likeShape);
        return source.present && !source.like ? &source : nullptr;
    }

}  // namespace BECore::Mahjong
