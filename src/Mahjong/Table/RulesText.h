#pragma once

#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Table/UiPalette.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * The rules screen (the nodes `Rules` and `RulesAction` of `config/SceneConfig.xml`): the pages of text and tile pictures that
     * explain the game, as plain data. Pure like `ScoreText` / `MoveText`: no scene, no renderer, the component only puts the page into
     * variables. Every word is a key `rules.*` of `config/UIStrings.xml`; **every number a page prints is a `{placeholder}`** that is
     * filled from the named constants of the rules (`kDealtTiles`, `kWinningHandTiles`, `kLooseTiles`, ...), so a rule that changes in the
     * code changes on the page (the test `RulesTextTest` fails on a digit in any `rules.*` text). The words of the buttons come from
     * `WordOf`, so a page names a button as the table shows it in each language.
     *
     * A page is a title, up to `kRulesMaxLines` lines of prose (one label each, up to two rows of text), up to `kRulesMaxItems` short items
     * (shown in two columns) and up to `kRulesMaxTileRows` rows of up to `kRulesMaxRowTiles` tile pictures. The screen is a plate of a
     * fixed height and cannot scroll, so a page that does not fit (`PageHeight`) is split in two; a font is never made smaller.
     */

    /// What one page may hold; the scene file has exactly that many elements (`MahjongRulesTest` compares).
    inline constexpr int kRulesMaxLines = 6;
    inline constexpr int kRulesMaxItems = 12;
    inline constexpr int kRulesMaxTileRows = 2;
    /// A whole winning hand, the longest row a page shows.
    inline constexpr int kRulesMaxRowTiles = kWinningHandTiles;
    /// The longest line of prose in code points, after the numbers and the words of the buttons are put in (two rows of text on the plate).
    inline constexpr size_t kRulesMaxLineLength = 160;

    /// The plate of the screen in view units (the scene file repeats them; `MahjongRulesTest` compares): the height of the plate and of
    /// what is shown inside it, the gap between the blocks and the padding. The blocks are the title, the pager, the lines, the items
    /// and the tile rows; a block that a page does not use is hidden and takes no room.
    inline constexpr int kRulesPlateHeight = 588;
    inline constexpr int kRulesPlatePadding = static_cast<int>(kModalPadding);
    inline constexpr int kRulesBlockGap = static_cast<int>(kModalGap);
    inline constexpr int kRulesInnerHeight = kRulesPlateHeight - 2 * kRulesPlatePadding;
    inline constexpr int kRulesTitleHeight = static_cast<int>(kModalTitleHeight);
    inline constexpr int kRulesPagerHeight = 28;
    /// A line is a label of two rows of the 18 font (a row of that font is 23.5 units high, so two rows need 47). In the scene the lines are children of the plate itself (a stack that hides what a
    /// page does not use needs no fixed height that way), so they are `kRulesLineGap` = the gap of the plate apart.
    inline constexpr int kRulesLineHeight = 48;
    inline constexpr int kRulesLineGap = kRulesBlockGap;
    /// An item is one row of the 18 font in half of the plate's width (the scene has a row of two labels per `kRulesItemColumns` items, so
    /// a row with no item takes no room); the rows are `kRulesItemGap` = the gap of the plate apart.
    inline constexpr int kRulesItemHeight = 28;
    inline constexpr int kRulesItemGap = kRulesBlockGap;
    inline constexpr int kRulesItemColumns = 2;
    /// A tile picture is the 3:4 of an atlas cell; a row of them is one block.
    inline constexpr int kRulesTileWidth = 36;
    inline constexpr int kRulesTileHeight = 48;
    static_assert(kRulesTileWidth * 4 == kRulesTileHeight * 3, "a tile picture keeps the shape of the atlas cell");

    /// One page of the rules in the current language.
    struct RulesPage {
        eastl::string title;
        /// The prose, one label each, at most `kRulesMaxLines` of at most `kRulesMaxLineLength` code points.
        eastl::vector<eastl::string> lines;
        /// Short items with a number ("Dragon set: x2"), at most `kRulesMaxItems`, shown in `kRulesItemColumns` columns.
        eastl::vector<eastl::string> items;
        /// The examples: at most `kRulesMaxTileRows` rows of at most `kRulesMaxRowTiles` playable tiles each (no text, so no language).
        eastl::vector<eastl::vector<Tile>> tileRows;
    };

    /// How many pages the rules have.
    [[nodiscard]] int RulesPageCount() noexcept;

    /// The page `page` (0-based) of the rules in the current language of `strings`. An index out of range gives an empty page, never a crash.
    [[nodiscard]] RulesPage FormatRulesPage(const UIStrings& strings, int page);

    /// "Page 3 of 13" (key `rules.pager`, `{n}` is `page + 1`, `{total}` the number of pages); the page is clamped into the range.
    [[nodiscard]] eastl::string RulesPagerText(const UIStrings& strings, int page);

    /// The height in view units that a page needs inside the plate: the blocks it uses (the title and the pager always, the lines, the items
    /// in `kRulesItemColumns` columns, each tile row) with `kRulesBlockGap` between them. A page whose height is above `kRulesInnerHeight`
    /// does not fit the plate. Language-free: it counts the elements, not the rows of text a line needs.
    [[nodiscard]] int PageHeight(const RulesPage& page) noexcept;

}  // namespace BECore::Mahjong
