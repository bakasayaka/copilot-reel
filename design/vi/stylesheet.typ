// Copilot Reel visual identity: token reference sheet (editable English source).
// Reads tokens.json directly, so no value is duplicated here; the Python generator in
// source/ draws the VI PDF from the same file. Compile with Typst 0.11 or later:
//   typst compile design/vi/stylesheet.typ
#let t = json("tokens.json")
#let g = t.geometry
#let ev = t.motion.events
#let pal = t.palettes
#let n(v) = if type(v) == float or type(v) == int { str(v) } else { v }
#let col(v) = if type(v) == dictionary {
  if "hex" in v { v.hex + " · " + str(calc.round(v.alpha * 100)) + "%" } else { v.ramp + " (fallback " + v.fallback + ")" }
} else { n(v) }

#set document(title: "Copilot Reel - VI token sheet v" + t.schemaVersion)
#set page(paper: "a4", flipped: true, margin: 16mm)
#set text(font: ("Segoe UI Variable Text", "Segoe UI"), size: 8.5pt, lang: "en")
#show heading: set text(font: ("Segoe UI Variable Display", "Segoe UI"), weight: 600)
#show raw: set text(font: "Consolas")
#set table(stroke: 0.4pt + luma(210), inset: 4pt)

= Copilot Reel / Soft Orbit token sheet v#t.schemaVersion
Native build #t.appVersion. Units: DIP; durations in ms. Source of truth: `design/vi/tokens.json` (hand-edited);
`src/vi_tokens.h` is generated from it. The illustrated specification is #raw("output/pdf/copilot-reel-vi-v" + t.schemaVersion + ".pdf").

== Geometry
#table(columns: (auto, auto, 1fr),
  [*Token*], [*Value*], [*Use*],
  [Canvas], [#n(g.canvas)], [complete footprint including the #n(t.elevation.canvasMargin) DIP shadow margin],
  [Plate radius / rim], [#n(g.plate.radius) / #n(g.plate.stroke), inset #n(g.plate.strokeInset)], [high contrast rim #n(g.plate.strokeHighContrast)],
  [Orbit radius], [#n(g.orbit.radius)], [#g.orbit.direction from #g.orbit.firstSlot, step #g.orbit.step\; slots are numbered #g.orbit.numbering],
  [Center zone], [r #n(g.centerZone.radius)], [idle ring #n(g.centerZone.ringStroke) in `ringIdle`\; content: #t.osd.centerContent\; #t.interaction.centerCancel\; no gate or center entry (see Edge)],
  [Edge], [offset #n(t.edgeLayout.pointerOffset)], [#t.edgeLayout.interior\; at an edge: #t.edgeLayout.edge\; #t.edgeLayout.selection],
  [Tile], [#n(g.tile.size), r #n(g.tile.radius)], [hairline #n(g.tile.stroke), inset #n(g.tile.strokeInset)],
  [Seat], [#n(g.seat.size), r #n(g.seat.radius)], [stroke #n(g.seat.stroke), inset #n(g.seat.strokeInset)\; high contrast #n(g.seat.strokeHighContrast)\; #g.seat.relation],
  [Command glyph], [em #n(g.glyph.em)], [#g.glyph.alignment\; vector fallback stroke #n(g.glyph.fallbackStroke) (high contrast #n(g.glyph.fallbackStrokeHighContrast))],
  [Artwork keyline], [#n(g.artwork.keyline)], [#g.artwork.note],
  [Preferred badge], [r #n(g.badge.radius) at (±#n(g.badge.offset))], [ring r #n(g.badge.ringRadius) (#n(g.badge.ring)); check #n(g.badge.checkStroke)\; #g.badge.check],
  [Language chevrons], [±#n(g.chevrons.offset), y #n(g.chevrons.orbitY)], [em #n(g.chevrons.em)\; hit #n(g.chevrons.hit)\; back chevron em #n(g.chevrons.backEm)],
  [Disabled], [#n(g.disabledOpacity)], [tile and artwork opacity; never seated; still named on hover],
  [Shadow], [blur #n(t.elevation.shadowBlur), y #n(t.elevation.shadowOffsetY)], [#t.elevation.recipe],
  [Pixel grid], [], [#g.pixelGrid],
)

== Color roles
#table(columns: (auto, 1fr, 1fr, 1fr),
  [*Role*], [*Light*], [*Dark*], [*High contrast*],
  ..("plate", "foreground", "secondary", "outline", "tile", "tileStroke", "plateStroke", "ringIdle", "accent",
     "accentText", "textOnAccent", "selection", "selectionText", "shadowOpacity").map(r =>
    (raw(r), col(pal.light.at(r)), col(pal.dark.at(r)), n(pal.highContrast.at(r)))).flatten()
)
Seat tint: light #n(pal.light.seatTint), dark #n(pal.dark.seatTint). #pal.mix. #pal.accentRule.
Frosted tile alpha: light #n(pal.light.frostedTileAlpha), dark #n(pal.dark.frostedTileAlpha).

== Typography
#table(columns: (auto, 1fr),
  [Latin], [#t.typography.preferred (#t.typography.familyModel), weight #t.typography.weight\; fallback #t.typography.fallback],
  [Chinese], [#t.typography.cjk.family, #t.typography.cjk.locale\; ranges #t.typography.cjk.ranges.join(", ")\; then #t.typography.cjk.then],
  [Japanese], [#t.typography.japanese.family 400 / 600 (#t.typography.japanese.locale), then #t.typography.japanese.systemFamily\; kana #t.typography.japanese.ranges.join(", ")\; #t.typography.japanese.rule],
  [Label], [#n(t.typography.applicationLabel.size) / #t.typography.applicationLabel.weight, #t.typography.applicationLabel.lines line, #n(t.typography.applicationLabel.width) × #n(t.typography.applicationLabel.height), #t.typography.applicationLabel.trimming\; #t.typography.applicationLabel.verticalCenter],
  [Monogram], [#t.typography.iconMonogram.states.join(" / "), #n(t.typography.iconMonogram.size) / #t.typography.iconMonogram.weight, #t.typography.iconMonogram.verticalCenter],
  [Never drawn], [#t.osd.neverDrawn.join(", ")],
)

== Iconography
Command glyphs (#t.iconography.commandFont, fallback #t.iconography.commandFontFallback):
#table(columns: (auto, auto, 1fr),
  [*Glyph*], [*Code point*], [*Note*],
  ..t.iconography.commandGlyphs.pairs().map(p => (p.at(0), raw("U+" + p.at(1)),
    t.iconography.commandGlyphNotes.at(p.at(0), default: ""))).flatten()
)
#table(columns: (auto, auto, auto, 1fr),
  [*Brand key*], [*Class*], [*Optical scale*], [*Content rect (normalized)*],
  ..t.iconography.brands.map(b => (raw(b.key), b.class, n(b.opticalScale), b.content.map(str).join(", "))).flatten()
)
Classes: plated: #t.iconography.classes.plated. Glyph: #t.iconography.classes.glyph. Generic: #t.iconography.classes.generic.

== Motion (#t.motion.tier tier)
#table(columns: (auto, 1fr),
  [Curves], [#t.motion.curves.pairs().map(p => p.at(0) + " (" + p.at(1).map(str).join(", ") + ")").join("; ")],
  [Open], [opacity #ev.open.fadeMs ms #ev.open.fadeCurve\; scale #n(ev.open.scaleFrom) → 1 in #ev.open.scaleMs ms #ev.open.scaleCurve],
  [Close], [opacity #ev.close.fadeMs ms; scale → #n(ev.close.scaleTo) in #ev.close.scaleMs ms #ev.close.scaleCurve (#ev.close.use)],
  [Commit], [#ev.commit.detail\; total #ev.commit.totalMs ms; #ev.commit.launch],
  [Selection], [#ev.selection.noneToItem. Item to item: #ev.selection.travel.path\; spring damping #n(ev.selection.travel.dampingRatio), period #ev.selection.travel.periodMs ms, overshoot #ev.selection.travel.overshoot],
  [Name], [crossfade #ev.centerLabel.crossfadeMs ms #ev.centerLabel.curve\; the ring hides in #ev.centerLabel.ringHideMs ms],
  [Drill], [exit #ev.drill.exitMs ms; enter #ev.drill.enterMs ms #ev.drill.enterCurve\; fade #ev.drill.enterFadeMs ms after #ev.drill.enterDelayMs ms; in #n(ev.drillIn.exitScale) / #n(ev.drillIn.enterScaleFrom), out #n(ev.drillOut.exitScale) / #n(ev.drillOut.enterScaleFrom)],
  [Handoff], [#ev.handoff.detail\; #ev.handoff.travelMs ms, scale → #n(ev.handoff.scaleTo), fade #ev.handoff.fadeMs ms; name clears in #ev.handoff.labelFadeMs ms],
  [Swap], [out #ev.swap.exitMs ms; in #ev.swap.enterMs ms from #n(ev.swap.enterScaleFrom) #ev.swap.enterCurve],
  [Reduced motion], [#ev.reducedMotion.appliesTo: every duration #ev.reducedMotion.durationMs ms; #ev.reducedMotion.completion],
  [Never], [#t.motion.notAllowed.join("; ")],
)

== Material
Solid: plate at alpha #n(t.systemAppearance.solid.surfaceAlpha), no backdrop sampling.
Frosted: #t.systemAppearance.frosted.graph. Light tint #t.systemAppearance.frosted.light.tint at #n(t.systemAppearance.frosted.light.tintOpacity), luminosity #n(t.systemAppearance.frosted.light.luminosityOpacity)\;
dark tint #t.systemAppearance.frosted.dark.tint at #n(t.systemAppearance.frosted.dark.tintOpacity), luminosity #n(t.systemAppearance.frosted.dark.luminosityOpacity)\;
noise #t.systemAppearance.frosted.noise.size px at #n(t.systemAppearance.frosted.noise.opacity).
Solid whenever: #t.systemAppearance.frosted.fallbackToSolidWhen.join("; ").
