#pragma once
#include "app_types.h"
#include <d2d1_1.h>
#include <vector>

namespace reel {
// How an item's artwork relates to its 40 DIP tile container.
enum class ArtworkClass {
    Command,  // internal reel:* command glyph on the neutral tile
    Glyph,    // vendor, system or application mark without its own plate, on the neutral tile
    Plated,   // artwork carrying its own filled plate: full-bleed, clipped to the tile shape
    Generic   // no artwork available: neutral application glyph on the neutral tile
};

struct ArtworkStyle {
    float dpi = 96;            // device DPI of the surface being rasterized
    bool dark = false;         // plate theme; selects vendor light/dark variants
    bool highContrast = false;
    D2D1_COLOR_F glyph{0, 0, 0, 1};  // command and generic glyph color
};

// Native command glyphs: Segoe Fluent Icons, with a vector fallback.
// GenericApp stays last: it sizes the glyph cache.
enum class Glyph { Add, Manage, Preferred, Sun, Moon, Edit, Remove, Previous, Next,
                   ChevronLeftSmall, ChevronRightSmall, BackChevron, Check,
                   About, SwitchOn, SwitchOff, CheckUpdate, CheckFailed, Download, GenericApp };

// UI thread. Call when the configured targets or the DPI change. May perform file
// and Shell I/O (vendor artwork, Shell icons, per-DPI raster prescale). Drawing never
// performs file or Shell I/O; it only prescales a raster that missed the cache (after
// device loss, or at a DPI that was not prepared). Failed lookups are retried next time.
void PrepareArtwork(const std::vector<AppTarget>& items, float dpi);

// Cheap; a reel:* target is Command, except the reel's own mark (About's first tile and the color
// Tray icon tile), which is Plated, and GitHub's (About's second), which is Glyph. Uses the
// analysis cached by PrepareArtwork.
ArtworkClass ClassifyArtwork(const AppTarget& item);

// What DrawArtwork paints for the item: its command, vendor or Shell artwork source, or
// the generic glyph. Two items draw the same pixels exactly when this is equal, so it is
// the cache identity. A target is not: catalog entries that still need a target all have
// an empty one and are told apart by their catalog name.
std::wstring ArtworkIdentity(const AppTarget& item, bool dark);

// `tile` is the item's 40 x 40 DIP rectangle (corner radius vi::kTileRadius).
// Plated artwork fills and is clipped to the tile shape; other classes draw centered
// at their optical size, and the caller draws the neutral tile beneath them.
// reel:language is not drawn here (the renderer draws the EN/ZH/JA monogram).
bool DrawArtwork(ID2D1DeviceContext* context, const AppTarget& item,
                 const D2D1_RECT_F& tile, const ArtworkStyle& style);

// Draws a command glyph centered at `center` with an em size in DIP.
void DrawGlyph(ID2D1DeviceContext* context, Glyph glyph, D2D1_POINT_2F center,
               float emDip, const D2D1_COLOR_F& color, float dpi);

// Maps reel:* command targets to glyphs. False for reel:language and non-commands.
bool CommandGlyph(const AppTarget& item, bool darkTheme, Glyph& glyph);

// Releases device-dependent caches after device loss. nullptr releases all.
void ReleaseArtwork(ID2D1Device* device = nullptr);
}
