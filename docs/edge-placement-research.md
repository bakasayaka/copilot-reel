# Radial-menu edge placement research

Reviewed on 2026-10-19. This note replaces the half-orbit and staggered-corner-fan proposal from VI v0.3. It changes the specification, not the running application's input behavior.

## Verified precedents

| Product | Observed or documented behavior | Relevant trade-off |
| --- | --- | --- |
| Kando | Clamps the entire menu to the monitor using its maximum radius. When enabled, pointer warping applies the same offset; the documented default is enabled. | Preserves radial geometry and aligns the pointer with the relocated center, but moves the system pointer. |
| Fly-Pie | Moves the full menu inside the monitor, relocates its selection geometry, and can move the pointer to the new center. | Maintains a coherent center at the cost of pointer movement. |
| Blender | Repositions the full pie inside window bounds. A recenter timeout temporarily retains the original direction center before using the displayed center. | Supports rapid gestures, but a time-dependent origin transition needs care for release-to-launch. |
| Maya | Official documentation emphasizes familiar item positions and fast directional gestures. | Supports stable spatial mapping; no specific edge algorithm is attributed without evidence. |

Primary sources:

- [Kando configuration: warpMouse](https://kando.menu/config-files/#warpmouse)
- [Kando theme radius](https://kando.menu/create-menu-themes/)
- [Kando positioning source, pinned revision](https://github.com/kando-menu/kando/blob/76f07d8a5cfd0c59e8d1ab8afccff94cd4090ef9/src/menu-renderer/menu.ts#L713-L744)
- [Fly-Pie positioning source, pinned revision](https://github.com/Schneegans/Fly-Pie/blob/28ea7f4c810fa692eaa2f4629f87b227a0c37a19/src/extension/Menu.js#L1024-L1045)
- [Blender manual: pie menus and recenter timeout](https://docs.blender.org/manual/en/2.91/editors/preferences/interface.html#pie-menus)
- [Blender popup placement source](https://github.com/blender/blender/blob/959865fd4c2db90c88b26cc55d8f169b300587b2/source/blender/editors/interface/regions/interface_region_popup.cc#L784-L824)
- [Blender input-origin selection source](https://github.com/blender/blender/blob/959865fd4c2db90c88b26cc55d8f169b300587b2/source/blender/editors/interface/interface_handlers.cc#L11458-L11474)
- [Maya marking-menu documentation](https://help.autodesk.com/cloudhelp/2023/ENU/Maya-Animation/files/GUID-8BA1A3AA-4C44-4779-8B22-0AAE3627E8EB.htm)

## Geometry constraint

At a physical screen corner, an absolute system pointer cannot move outward in every direction. Keeping the original origin alone does not make all radial targets reachable. A design must choose among pointer recentering, an explicit entry movement, relative input, or a layout change. Merely translating the drawing while retaining an unrelated hit-test center is not a complete solution.

## Proposed default for Copilot Reel

Preserve the complete circle and translate it only enough to fit the current monitor work area, including its shadow footprint. Do not rotate, compress, reorder, or stagger individual slots.

Let `P` be the pointer at opening, `C` the nearest legal visual center, and `R` the cancellation radius. Both rendering and armed hit testing use `C`.

1. If `distance(P, C) <= R`, begin in the normal armed state with no selection.
2. Otherwise enter `AwaitCenter`, show a restrained center-entry hint, and leave all items unselected.
3. While waiting, passing over an icon does not select it. Releasing cancels.
4. Entering the visible center zone arms the menu. Subsequent direction selection uses `C`; releasing inside the center still cancels.
5. Time alone never arms the menu or switches its origin. The system pointer is not warped.

This center-entry guard is this project's proposal, not a claim about Kando, Fly-Pie, or Blender. Copilot Reel shipped it for a pointer left outside the plate from 0.3.5.1 to 0.3.10; 0.3.11 removed it at the user's request, so every opening selects by direction once the pointer moves, and only the opening position itself never selects. Its cost is an extra inward movement near a boundary. It deliberately favors predictable release behavior over a single blind flick at every screen position.

An explicitly enabled pointer-recentering option can be evaluated later. Relative virtual-pointer navigation requires separate input/feedback and restoration design and is not the default.

## Required validation before implementation approval

- Release immediately after opening: no launch, including when the pointer begins over a displaced item.
- Cross an item while approaching the center: no premature highlight or activation.
- Enter the center, select any slot, and release: exactly one activation.
- Back into the center and release: cancellation.
- All four edges and corners, 1-8 targets, taskbar work-area bounds, and mixed DPI.
- A work area too small for the full footprint: evaluate a compact fallback explicitly.
- Compare error rate and selection time with optional pointer recentering; do not infer usability from static artwork alone.
