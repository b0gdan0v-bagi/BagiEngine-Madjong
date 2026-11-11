#include "TableLayoutSpecXml.h"

#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TableSolver.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace BECore::Mahjong {

    namespace {

        constexpr const char* kRootName = "TableLayout";
        constexpr const char* kShapeElement = "Shape";
        constexpr const char* kSeatElement = "Seat";
        constexpr const char* kWallElement = "Wall";
        constexpr const char* kPhoneElement = "Phone";
        constexpr const char* kHandElement = "Hand";
        constexpr const char* kMeldsElement = "Melds";
        constexpr const char* kBonusElement = "Bonus";
        constexpr const char* kDiscardsElement = "Discards";
        constexpr const char* kPlateElement = "Plate";
        constexpr const char* kTileSolve = "solve";
        constexpr const char* kTileProfile = "profile";
        /// A pile of 100 tiles is 17 rows; more rows than this in a zone only mean a typo.
        constexpr int kMaxZoneDiscardRows = 24;

        /// One pass over the file: the messages collect in `errors`, each starting with the place (`where`) it was found.
        class SpecReader {
        public:
            eastl::vector<eastl::string> errors;

            void Fail(const eastl::string& where, const eastl::string& what) {
                errors.push_back(Format("TableLayout: {}: {}", where, what));
            }

            /// Every attribute of the element must be one of `allowed`: a typo is an error, not a setting that is ignored.
            void CheckAttributes(const pugi::xml_node& node, const eastl::string& where, std::initializer_list<const char*> allowed) {
                for (const pugi::xml_attribute attribute : node.attributes()) {
                    bool known = false;
                    for (const char* name : allowed) {
                        known = known || eastl::string_view(attribute.name()) == name;
                    }
                    if (!known) {
                        Fail(where, Format("unknown attribute '{}'", attribute.name()));
                    }
                }
            }

            std::optional<float> ReadFloat(const pugi::xml_node& node, const eastl::string& where, const char* name, bool required) {
                const pugi::xml_attribute attribute = node.attribute(name);
                if (!attribute) {
                    if (required) {
                        Fail(where, Format("missing attribute '{}'", name));
                    }
                    return std::nullopt;
                }
                const char* text = attribute.value();
                char* end = nullptr;
                const float value = std::strtof(text, &end);
                if (end == text || *end != '\0' || !std::isfinite(value)) {
                    Fail(where, Format("attribute '{}': '{}' is not a number", name, text));
                    return std::nullopt;
                }
                return value;
            }

            std::optional<float> ReadFraction(const pugi::xml_node& node, const eastl::string& where, const char* name, bool required) {
                const std::optional<float> value = ReadFloat(node, where, name, required);
                if (value && !(*value >= 0.0f && *value <= 1.0f)) {
                    Fail(where, Format("attribute '{}': {} is outside 0..1", name, attribute(node, name)));
                    return std::nullopt;
                }
                return value;
            }

            std::optional<int> ReadInt(const pugi::xml_node& node, const eastl::string& where, const char* name, int least, int most) {
                const pugi::xml_attribute attribute = node.attribute(name);
                if (!attribute) {
                    return std::nullopt;
                }
                const char* text = attribute.value();
                char* end = nullptr;
                errno = 0;
                const long value = std::strtol(text, &end, 10);
                if (end == text || *end != '\0' || errno == ERANGE) {
                    Fail(where, Format("attribute '{}': '{}' is not a whole number", name, text));
                    return std::nullopt;
                }
                if (value < least || value > most) {
                    Fail(where, Format("attribute '{}': {} is outside {}..{}", name, value, least, most));
                    return std::nullopt;
                }
                return static_cast<int>(value);
            }

            std::optional<bool> ReadBool(const pugi::xml_node& node, const eastl::string& where, const char* name) {
                const pugi::xml_attribute attribute = node.attribute(name);
                if (!attribute) {
                    return std::nullopt;
                }
                const eastl::string_view text = attribute.value();
                if (text == "true" || text == "false") {
                    return text == "true";
                }
                Fail(where, Format("attribute '{}': '{}' is not true or false", name, attribute.value()));
                return std::nullopt;
            }

            /// x, y, w and h: fractions of the design view, all four required.
            ZoneRect ReadRect(const pugi::xml_node& node, const eastl::string& where) {
                ZoneRect rect;
                rect.x = ReadFraction(node, where, "x", true).value_or(0.0f);
                rect.y = ReadFraction(node, where, "y", true).value_or(0.0f);
                rect.w = ReadFraction(node, where, "w", true).value_or(0.0f);
                rect.h = ReadFraction(node, where, "h", true).value_or(0.0f);
                return rect;
            }

            StripZone ReadStrip(const pugi::xml_node& node, const eastl::string& where) {
                CheckAttributes(node, where, {"x", "y", "w", "h", "align", "spread", "tile", "minTileW"});
                StripZone zone;
                zone.rect = ReadRect(node, where);
                if (const pugi::xml_attribute align = node.attribute("align")) {
                    const eastl::string_view text = align.value();
                    if (text == "start") {
                        zone.align = ZoneAlign::Start;
                    } else if (text == "center") {
                        zone.align = ZoneAlign::Center;
                    } else if (text == "end") {
                        zone.align = ZoneAlign::End;
                    } else {
                        Fail(where, Format("attribute 'align': '{}' is not start, center or end", align.value()));
                    }
                }
                zone.spread = ReadBool(node, where, "spread").value_or(false);
                if (const pugi::xml_attribute tile = node.attribute("tile")) {
                    const eastl::string_view text = tile.value();
                    if (text == kTileProfile) {
                        zone.profileTile = true;
                    } else if (text != kTileSolve) {
                        Fail(where, Format("attribute 'tile': '{}' is not {} or {}", tile.value(), kTileSolve, kTileProfile));
                    }
                }
                zone.minTileW = ReadInt(node, where, "minTileW", 0, kSolverMaxTileW).value_or(0);
                return zone;
            }

            DiscardZone ReadDiscards(const pugi::xml_node& node, const eastl::string& where) {
                CheckAttributes(node, where, {"x", "y", "w", "h", "rows"});
                DiscardZone zone;
                zone.rect = ReadRect(node, where);
                zone.rows = ReadInt(node, where, "rows", 1, kMaxZoneDiscardRows).value_or(kDiscardRowCapacity);
                return zone;
            }

            ZoneRect ReadPlain(const pugi::xml_node& node, const eastl::string& where) {
                CheckAttributes(node, where, {"x", "y", "w", "h"});
                return ReadRect(node, where);
            }

            /// The children of a `Seat` (or of the `Phone` override): Hand, Melds, Bonus, Discards and Plate, each once.
            void ReadSeatZones(const pugi::xml_node& node, const eastl::string& where, SeatSpec& seat) {
                unsigned seen = 0;
                for (const pugi::xml_node child : node.children()) {
                    if (child.type() != pugi::node_element) {
                        continue;
                    }
                    const eastl::string_view name = child.name();
                    const eastl::string childWhere = Format("{}, {}", where, child.name());
                    unsigned bit = 0;
                    if (name == kHandElement) {
                        bit = 1U;
                        seat.hand = ReadStrip(child, childWhere);
                    } else if (name == kMeldsElement) {
                        bit = 1U << 1;
                        seat.melds = ReadStrip(child, childWhere);
                    } else if (name == kBonusElement) {
                        bit = 1U << 2;
                        seat.bonus = ReadStrip(child, childWhere);
                    } else if (name == kDiscardsElement) {
                        bit = 1U << 3;
                        seat.discards = ReadDiscards(child, childWhere);
                    } else if (name == kPlateElement) {
                        bit = 1U << 4;
                        seat.plate = ReadPlain(child, childWhere);
                    } else {
                        Fail(where, Format("unknown element '{}'", child.name()));
                        continue;
                    }
                    if ((seen & bit) != 0U) {
                        Fail(where, Format("'{}' is given twice", child.name()));
                    }
                    seen |= bit;
                }
                constexpr const char* kNames[] = {kHandElement, kMeldsElement, kBonusElement, kDiscardsElement, kPlateElement};
                for (unsigned index = 0; index < std::size(kNames); ++index) {
                    if ((seen & (1U << index)) == 0U) {
                        Fail(where, Format("'{}' is missing", kNames[index]));
                    }
                }
            }

            void ReadShape(const pugi::xml_node& node, TableShape shape, ShapeSpec& out) {
                const eastl::string where = Format("shape '{}'", EnumUtils<TableShape>::ToString(shape));
                CheckAttributes(node, where, {"name", "like", "inset"});
                out.present = true;
                const pugi::xml_attribute like = node.attribute("like");
                if (like) {
                    ReadLikeShape(node, where, like.value(), out);
                    return;
                }
                if (node.attribute("inset")) {
                    Fail(where, "attribute 'inset' is for a shape that is 'like' another");
                }
                unsigned seatsSeen = 0;
                bool wallSeen = false;
                bool phoneSeen = false;
                for (const pugi::xml_node child : node.children()) {
                    if (child.type() != pugi::node_element) {
                        continue;
                    }
                    const eastl::string_view name = child.name();
                    if (name == kSeatElement) {
                        ReadSeat(child, where, out, seatsSeen);
                    } else if (name == kWallElement) {
                        const eastl::string wallWhere = Format("{}, {}", where, kWallElement);
                        if (wallSeen) {
                            Fail(where, "'Wall' is given twice");
                        }
                        wallSeen = true;
                        out.wall = ReadPlain(child, wallWhere);
                    } else if (name == kPhoneElement) {
                        const eastl::string phoneWhere = Format("{}, {}", where, kPhoneElement);
                        if (phoneSeen) {
                            Fail(where, "'Phone' is given twice");
                        }
                        if (!IsLandscapeLike(shape)) {
                            Fail(phoneWhere, "the phone profile is for the landscape shapes only");
                        }
                        phoneSeen = true;
                        out.hasPhone = true;
                        CheckAttributes(child, phoneWhere, {});
                        ReadSeatZones(child, phoneWhere, out.phone);
                    } else {
                        Fail(where, Format("unknown element '{}'", child.name()));
                    }
                }
                for (int position = 0; position < kSeatCount; ++position) {
                    if ((seatsSeen & (1U << position)) == 0U) {
                        Fail(where, Format("seat {} is missing", position));
                    }
                }
                if (!wallSeen) {
                    Fail(where, "'Wall' is missing");
                }
            }

            void ReadLikeShape(const pugi::xml_node& node, const eastl::string& where, const char* likeName, ShapeSpec& out) {
                const std::optional<TableShape> source = EnumUtils<TableShape>::Cast(likeName);
                if (!source) {
                    Fail(where, Format("attribute 'like': '{}' is not a shape", likeName));
                } else {
                    out.like = true;
                    out.likeShape = *source;
                }
                out.inset = ReadFraction(node, where, "inset", false).value_or(0.0f);
                for (const pugi::xml_node child : node.children()) {
                    if (child.type() == pugi::node_element) {
                        Fail(where, "a shape that is 'like' another has no zones of its own");
                        break;
                    }
                }
            }

            void ReadSeat(const pugi::xml_node& node, const eastl::string& shapeWhere, ShapeSpec& shape, unsigned& seatsSeen) {
                CheckAttributes(node, shapeWhere, {"position"});
                const std::optional<int> position = ReadInt(node, shapeWhere, "position", 0, kSeatCount - 1);
                if (!position) {
                    if (!node.attribute("position")) {
                        Fail(shapeWhere, "a seat has no attribute 'position'");
                    }
                    return;
                }
                const eastl::string where = Format("{}, seat {}", shapeWhere, *position);
                if ((seatsSeen & (1U << *position)) != 0U) {
                    Fail(where, "the seat is given twice");
                }
                seatsSeen |= 1U << *position;
                ReadSeatZones(node, where, shape.seats[static_cast<size_t>(*position)]);
            }

        private:
            static const char* attribute(const pugi::xml_node& node, const char* name) {
                return node.attribute(name).value();
            }
        };

    }  // namespace

    TableLayoutSpecParse ParseTableLayoutSpec(const pugi::xml_node& root) {
        TableLayoutSpecParse parse;
        SpecReader reader;
        pugi::xml_node table = root;
        if (eastl::string_view(root.name()) != kRootName) {
            table = root.child(kRootName);
        }
        if (!table) {
            reader.Fail("file", Format("there is no '{}' element", kRootName));
            parse.errors = eastl::move(reader.errors);
            return parse;
        }
        reader.CheckAttributes(table, "TableLayout", {});
        for (const pugi::xml_node child : table.children()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            if (eastl::string_view(child.name()) != kShapeElement) {
                reader.Fail("TableLayout", Format("unknown element '{}'", child.name()));
                continue;
            }
            const char* name = child.attribute("name").value();
            const std::optional<TableShape> shape = EnumUtils<TableShape>::Cast(name);
            if (!shape) {
                reader.Fail("TableLayout", Format("shape '{}' is not a shape (Landscape, Portrait, Wide or Narrow)", name));
                continue;
            }
            ShapeSpec& slot = parse.spec.Shape(*shape);
            if (slot.present) {
                reader.Fail("TableLayout", Format("shape '{}' is given twice", name));
                continue;
            }
            reader.ReadShape(child, *shape, slot);
        }
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            const ShapeSpec& own = parse.spec.Shape(shape);
            const eastl::string where = Format("shape '{}'", EnumUtils<TableShape>::ToString(shape));
            if (!own.present) {
                reader.Fail("TableLayout", Format("{} is missing", where));
            } else if (own.like) {
                const ShapeSpec& source = parse.spec.Shape(own.likeShape);
                if (own.likeShape == shape) {
                    reader.Fail(where, "a shape cannot be 'like' itself");
                } else if (source.present && source.like) {
                    reader.Fail(where, Format("attribute 'like': '{}' is itself 'like' another shape", EnumUtils<TableShape>::ToString(own.likeShape)));
                }
            }
        }
        parse.errors = eastl::move(reader.errors);
        return parse;
    }

    TableLayoutOfferResult OfferTableLayoutSpec(const pugi::xml_node& root) {
        static TableLayoutSpec offered;
        TableLayoutOfferResult result;
        TableLayoutSpecParse parse = ParseTableLayoutSpec(root);
        if (!parse.Ok()) {
            result.outcome = TableLayoutOffer::Rejected;
            result.errors = eastl::move(parse.errors);
            return result;
        }
        const TableLayoutSpec* active = ActiveTableLayoutSpec();
        if (active != nullptr && *active == parse.spec) {
            return result;
        }
        result.errors = CheckTableLayoutSpec(parse.spec);
        if (!result.errors.empty()) {
            result.outcome = TableLayoutOffer::Rejected;
            return result;
        }
        offered = eastl::move(parse.spec);
        SetActiveTableLayoutSpec(&offered);
        result.outcome = TableLayoutOffer::Applied;
        return result;
    }

}  // namespace BECore::Mahjong
