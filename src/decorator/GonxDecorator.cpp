/*
 * Copyright 2026, Gonx contributors.
 * Distributed under the terms of the MIT License.
 *
 * Window decorator after the "Gonx" BeOS concept artwork. See
 * GonxDecorator.h for what it looks like.
 *
 * Unlike the stock decorators this one derives from Decorator rather than
 * TabDecorator: TabDecorator keeps re-running its private (non-virtual)
 * button layout on resize, focus changes and tab drags, which would put the
 * buttons back at the tab's ends. Here every change goes through _DoLayout().
 *
 * Shapes are drawn row by row with solid colours and the footprint is built
 * from the same rows, so the rounded corners are exact to the pixel without
 * relying on anti-aliasing against whatever is behind the window.
 */


#include "GonxDecorator.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdio.h>

#include <Rect.h>
#include <View.h>

#include <WindowPrivate.h>

#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "PatternHandler.h"


//#define DEBUG_DECORATOR
#ifdef DEBUG_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


static const rgb_color kFrameColor = { 158, 178, 202, 255 };
static const rgb_color kInactiveFrameColor = { 166, 183, 204, 255 };
static const rgb_color kOutlineColor = { 79, 91, 107, 255 };

// The frame is lit from the top left, as in the concept's screenshots (and
// in Dano's frame, which shares its shape): a mid-blue outline and a light
// line along the top and left, a near-black outline with a second dark line
// along the right and bottom, and a dark line round the content.
static const rgb_color kFrameOutlineLight = { 95, 120, 150, 255 };
static const rgb_color kFrameOutlineDark = { 14, 26, 42, 255 };
static const rgb_color kFrameEdgeDark = { 92, 112, 136, 255 };
static const rgb_color kFrameInnerDark = { 91, 102, 124, 255 };
static const rgb_color kResizeHighlightColor = { 64, 96, 255, 255 };
static const rgb_color kWhite = { 255, 255, 255, 255 };
static const rgb_color kBlack = { 0, 0, 0, 255 };
static const rgb_color kGlyphShadowColor = { 52, 59, 68, 255 };

static const float kResizeKnobSize = 18.0;
static const float kBorderResizeLength = 22.0;


static rgb_color
mix(rgb_color a, rgb_color b, float amount)
{
	rgb_color result;
	result.red = (uint8)roundf(a.red + (b.red - a.red) * amount);
	result.green = (uint8)roundf(a.green + (b.green - a.green) * amount);
	result.blue = (uint8)roundf(a.blue + (b.blue - a.blue) * amount);
	result.alpha = 255;
	return result;
}


/*!	How far row \a row (0 = top) of a corner with radius \a radius is pushed
	in from the straight edge.
*/
static float
corner_inset(float radius, int32 row)
{
	if (row >= radius)
		return 0;

	float distance = radius - row - 0.5f;
	return floorf(radius
		- sqrtf(std::max(radius * radius - distance * distance, 0.0f)) + 0.5f);
}


static void
offset_rect(BRect& rect, BPoint offset)
{
	if (rect.IsValid())
		rect.OffsetBy(offset);
}


//	#pragma mark - GonxDecorAddOn


GonxDecorAddOn::GonxDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
GonxDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) GonxDecorator(settings, rect, desktop);
}


//	#pragma mark - GonxDecorator


GonxDecorator::GonxDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
	:
	Decorator(settings, rect, desktop),
	fFrameColor(kFrameColor),
	fInactiveFrameColor(kInactiveFrameColor),
	fOutlineColor(kOutlineColor)
{
	STRACE(("GonxDecorator: frame (%.1f,%.1f,%.1f,%.1f)\n",
		rect.left, rect.top, rect.right, rect.bottom));
	UpdateColors(settings);
}


GonxDecorator::~GonxDecorator()
{
}


void
GonxDecorator::Draw(BRect updateRect)
{
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(updateRect & fBorderRect);

	if (IsOutlineResizing())
		_DrawOutlineFrame(updateRect & fOutlineBorderRect);

	_DrawTabs(updateRect & fTitleBarRect);
	_DrawFlap(updateRect);
}


void
GonxDecorator::Draw()
{
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(fBorderRect);

	if (IsOutlineResizing())
		_DrawOutlineFrame(fOutlineBorderRect);

	_DrawTabs(fTitleBarRect);
	_DrawFlap(fBorderRect | _FlapRect(_Geometry()));
}


Decorator::Region
GonxDecorator::RegionAt(BPoint where, int32& tabIndex) const
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		if (fTabList.ItemAt(i)->minimizeRect.Contains(where)) {
			tabIndex = i;
			return REGION_MINIMIZE_BUTTON;
		}
	}

	// close, zoom and the tab itself
	Region region = Decorator::RegionAt(where, tabIndex);
	if (region != REGION_NONE)
		return region;

	if (_FlapRect(_Geometry()).Contains(where)) {
		tabIndex = 0;
		return REGION_TAB;
	}

	if (fTopTab->look == B_DOCUMENT_WINDOW_LOOK && fResizeRect.Contains(where))
		return REGION_RIGHT_BOTTOM_CORNER;

	if (fLeftBorder.Contains(where))
		return REGION_LEFT_BORDER;
	if (fTopBorder.Contains(where))
		return REGION_TOP_BORDER;

	if (fRightBorder.Contains(where))
		region = REGION_RIGHT_BORDER;
	else if (fBottomBorder.Contains(where))
		region = REGION_BOTTOM_BORDER;
	else
		return REGION_NONE;

	if ((fTopTab->flags & B_NOT_RESIZABLE) == 0
		&& (fTopTab->look == B_TITLED_WINDOW_LOOK
			|| fTopTab->look == B_FLOATING_WINDOW_LOOK
			|| fTopTab->look == B_MODAL_WINDOW_LOOK
			|| fTopTab->look == kLeftTitledWindowLook)) {
		const float length = kBorderResizeLength * _Geometry().scale;
		BRect resizeRect(BPoint(fBottomBorder.right - length,
			fBottomBorder.bottom - length), fBottomBorder.RightBottom());
		if (resizeRect.Contains(where))
			return REGION_RIGHT_BOTTOM_CORNER;
	}

	return region;
}


void
GonxDecorator::UpdateColors(DesktopSettings& settings)
{
	// The frame keeps the Gonx blue-grey whatever the colour scheme; the tab
	// follows the window tab colours, whose defaults (yellow when active,
	// light grey when not) already match the concept.
	fFocusTabColor = settings.UIColor(B_WINDOW_TAB_COLOR);
	fFocusTextColor = settings.UIColor(B_WINDOW_TEXT_COLOR);
	fNonFocusTabColor = settings.UIColor(B_WINDOW_INACTIVE_TAB_COLOR);
	fNonFocusTextColor = settings.UIColor(B_WINDOW_INACTIVE_TEXT_COLOR);
}


//	#pragma mark - Protected methods


Decorator::Tab*
GonxDecorator::_AllocateNewTab()
{
	Tab* tab = new (std::nothrow) Tab;
	if (tab == NULL)
		return NULL;

	tab->titleRect.Set(0, 0, -1, -1);
	tab->shelfRect.Set(0, 0, -1, -1);
	tab->slope = 0;

	_SetFocus(tab);
	return tab;
}


void
GonxDecorator::_DoLayout()
{
	const Geometry geometry = _Geometry();

	float topBand = 0;
	switch ((int)fTopTab->look) {
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
			fBorderWidth = (int32)geometry.sideBorder;
			topBand = geometry.topBand;
			break;

		case B_MODAL_WINDOW_LOOK:
			fBorderWidth = (int32)geometry.sideBorder;
			topBand = fBorderWidth;
			break;

		case B_BORDERED_WINDOW_LOOK:
			fBorderWidth = 1;
			topBand = 1;
			break;

		default:
			fBorderWidth = 0;
			break;
	}

	if (fBorderWidth > 0) {
		// no overlapping: the left and right borders don't include corners
		fLeftBorder.Set(fFrame.left - fBorderWidth, fFrame.top,
			fFrame.left - 1, fFrame.bottom);
		fRightBorder.Set(fFrame.right + 1, fFrame.top,
			fFrame.right + fBorderWidth, fFrame.bottom);
		fTopBorder.Set(fFrame.left - fBorderWidth, fFrame.top - topBand,
			fFrame.right + fBorderWidth, fFrame.top - 1);
		fBottomBorder.Set(fFrame.left - fBorderWidth, fFrame.bottom + 1,
			fFrame.right + fBorderWidth, fFrame.bottom + fBorderWidth);
	} else {
		fLeftBorder.Set(0, 0, -1, -1);
		fRightBorder.Set(0, 0, -1, -1);
		fTopBorder.Set(0, 0, -1, -1);
		fBottomBorder.Set(0, 0, -1, -1);
	}

	fBorderRect = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());

	if (fBorderWidth > 1) {
		const float knob = roundf(kResizeKnobSize * geometry.scale);
		fResizeRect.Set(fBottomBorder.right - knob,
			fBottomBorder.bottom - knob, fBottomBorder.right,
			fBottomBorder.bottom);
	} else
		fResizeRect.Set(0, 0, -1, -1);

	if (_HasTab()) {
		_LayoutTabs(geometry);
		return;
	}

	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(i));
		tab->tabRect.Set(0, 0, -1, -1);
		tab->titleRect.Set(0, 0, -1, -1);
		tab->shelfRect.Set(0, 0, -1, -1);
		tab->closeRect.Set(0, 0, -1, -1);
		tab->zoomRect.Set(0, 0, -1, -1);
		tab->minimizeRect.Set(0, 0, -1, -1);
	}
	fTitleBarRect.Set(0, 0, -1, -1);
}


void
GonxDecorator::_DoOutlineLayout()
{
	fOutlineBorderWidth = 1;

	fLeftOutlineBorder.Set(fFrame.left - fOutlineBorderWidth, fFrame.top,
		fFrame.left - 1, fFrame.bottom);
	fRightOutlineBorder.Set(fFrame.right + 1, fFrame.top,
		fFrame.right + fOutlineBorderWidth, fFrame.bottom);
	fTopOutlineBorder.Set(fFrame.left - fOutlineBorderWidth,
		fFrame.top - fOutlineBorderWidth, fFrame.right + fOutlineBorderWidth,
		fFrame.top - 1);
	fBottomOutlineBorder.Set(fFrame.left - fOutlineBorderWidth,
		fFrame.bottom + 1, fFrame.right + fOutlineBorderWidth,
		fFrame.bottom + fOutlineBorderWidth);

	fOutlineBorderRect = BRect(fTopOutlineBorder.LeftTop(),
		fBottomOutlineBorder.RightBottom());
}


void
GonxDecorator::_DrawFrame(BRect rect)
{
	if (fTopTab->look == B_NO_BORDER_WINDOW_LOOK || fBorderWidth <= 0
		|| !rect.IsValid())
		return;

	if (fTopTab->look == B_BORDERED_WINDOW_LOOK) {
		fDrawingEngine->StrokeRect(fBorderRect, fOutlineColor);
		return;
	}

	const Geometry geometry = _Geometry();
	const BRect& r = fBorderRect;
	const BRect& c = fFrame;
	const int32 border = fBorderWidth;

	const rgb_color base = _FrameColor(REGION_TOP_BORDER);
	const rgb_color highlight = mix(base, kWhite, 0.4f);
	const rgb_color shade = mix(base, kFrameInnerDark, 0.25f);

	// body first, then the lines over it
	fDrawingEngine->FillRect(fTopBorder, base);
	fDrawingEngine->FillRect(fLeftBorder, _FrameColor(REGION_LEFT_BORDER));
	fDrawingEngine->FillRect(fRightBorder, _FrameColor(REGION_RIGHT_BORDER));
	fDrawingEngine->FillRect(fBottomBorder,
		_FrameColor(REGION_BOTTOM_BORDER));

	// outer edges: light top and left, dark right and bottom
	fDrawingEngine->FillRect(BRect(r.left, r.top, r.right, r.top),
		kFrameOutlineLight);
	fDrawingEngine->FillRect(BRect(r.left, r.top, r.left, r.bottom),
		kFrameOutlineLight);
	fDrawingEngine->FillRect(BRect(r.right, r.top + 1, r.right, r.bottom),
		kFrameOutlineDark);
	fDrawingEngine->FillRect(BRect(r.left + 1, r.bottom, r.right, r.bottom),
		kFrameOutlineDark);

	// the lines just inside them
	if (border > 2) {
		fDrawingEngine->FillRect(BRect(r.left + 1, r.top + 1, r.right - 2,
			r.top + 1), highlight);
		fDrawingEngine->FillRect(BRect(r.left + 1, r.top + 1, r.left + 1,
			r.bottom - 2), highlight);
	}
	if (border > 3) {
		fDrawingEngine->FillRect(BRect(r.right - 1, r.top + 1, r.right - 1,
			r.bottom - 1), kFrameEdgeDark);
		fDrawingEngine->FillRect(BRect(r.left + 1, r.bottom - 1, r.right - 1,
			r.bottom - 1), kFrameEdgeDark);
	}

	// round the content: a shading line, then the dark line next to it
	if (border > 3) {
		fDrawingEngine->FillRect(BRect(c.left - 2, c.top - 2, c.right + 2,
			c.top - 2), shade);
		fDrawingEngine->FillRect(BRect(c.left - 2, c.bottom + 2, c.right + 2,
			c.bottom + 2), shade);
	}
	fDrawingEngine->StrokeRect(BRect(c.left - 1, c.top - 1, c.right + 1,
		c.bottom + 1), kFrameInnerDark);

	_DrawInnerCorners(base);
	_DrawCornerOutlines(geometry.frameRadius, highlight);

	if ((fTopTab->flags & B_NOT_RESIZABLE) == 0) {
		if (fTopTab->look == B_DOCUMENT_WINDOW_LOOK) {
			if (rect.Intersects(fResizeRect))
				_DrawResizeKnob(_FrameColor(REGION_RIGHT_BOTTOM_CORNER));
		} else
			_DrawResizeGrip(geometry, highlight);
	}
}


void
GonxDecorator::_DrawOutlineFrame(BRect rect)
{
	drawing_mode oldMode;

	fDrawingEngine->SetDrawingMode(B_OP_ALPHA, oldMode);
	fDrawingEngine->SetPattern(B_MIXED_COLORS);
	fDrawingEngine->StrokeRect(rect);

	fDrawingEngine->SetDrawingMode(oldMode);
}


/*!	The tab after Dano's: a rounded tab sitting on the content's top edge,
	whose left end turns down the window's side as a flap (the first tab
	only), with a hatched grip along its left edge. A vertical gradient runs
	from the top of the tab to the bottom of the flap.
*/
void
GonxDecorator::_DrawTab(Decorator::Tab* _tab, BRect invalid)
{
	Tab* tab = static_cast<Tab*>(_tab);
	const bool first = tab == fTabList.ItemAt(0);
	if (!tab->tabRect.IsValid() || !invalid.Intersects(tab->tabRect)) {
		if (first)
			_DrawFlap(invalid);
		return;
	}

	const Geometry geometry = _Geometry();
	const bool focus = tab->isFocused;

	if (tab->shelfRect.IsValid()) {
		const rgb_color base = _FrameColor(REGION_TOP_BORDER);
		_DrawShelf(tab, base, fOutlineColor, mix(base, kWhite, 0.35f));
	}

	const rgb_color tabColor = focus ? fFocusTabColor : fNonFocusTabColor;
	const rgb_color outline = _TabOutline(tab);
	const BRect& title = tab->titleRect;
	// the left end is Dano's; the right end is the concept's, rounded at
	// the top and curving into the band at the bottom
	_FillTopRounded(title, geometry.tabRadius, geometry.tabRightRadius,
		_TabColorAt(tab, title.top), _TabColorAt(tab, title.bottom), outline,
		mix(tabColor, kWhite, 0.6f), geometry.tabFootRadius);

	// the tab's foot is the content's top edge; on the first tab the left
	// end carries on down as the flap
	const BRect flap = first ? _FlapRect(geometry) : BRect(0, 0, -1, -1);
	fDrawingEngine->FillRect(BRect(
		flap.IsValid() ? flap.right + 1 : title.left, title.bottom,
		title.right - corner_inset(geometry.tabFootRadius, 0) - 1,
		title.bottom), outline);

	_DrawHatch(tab, BRect(title.left + 2, title.top + 3,
		title.left + geometry.flapWidth - 2, title.bottom - 1));

	if (flap.IsValid())
		_DrawFlap(flap);

	_DrawTitle(tab, title);
	_DrawButtons(tab, invalid);
}


void
GonxDecorator::_DrawTitle(Decorator::Tab* _tab, BRect rect)
{
	Tab* tab = static_cast<Tab*>(_tab);
	const BRect& title = tab->titleRect;

	fDrawingEngine->SetDrawingMode(B_OP_OVER);
	fDrawingEngine->SetHighColor(tab->isFocused
		? fFocusTextColor : fNonFocusTextColor);
	fDrawingEngine->SetFont(fDrawState.Font());

	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint position;
	position.x = title.left + tab->textOffset;
	position.y = floorf(((title.top + 2.0f) + title.bottom
		+ fontHeight.ascent + fontHeight.descent) / 2.0f
		- fontHeight.descent + 0.5f);

	fDrawingEngine->DrawString(tab->truncatedTitle, tab->truncatedTitleLength,
		position);

	fDrawingEngine->SetDrawingMode(B_OP_COPY);
}


void
GonxDecorator::_DrawButtons(Decorator::Tab* tab, const BRect& invalid)
{
	if (invalid.Intersects(tab->closeRect))
		_DrawClose(tab, false, tab->closeRect);
	if (invalid.Intersects(tab->zoomRect))
		_DrawZoom(tab, false, tab->zoomRect);
	if (invalid.Intersects(tab->minimizeRect))
		_DrawMinimize(tab, false, tab->minimizeRect);
}


// Button glyphs from Momo's IceWM "Gonx" theme (store.kde.org/p/1119093,
// 2004): 'W' white, 'S' its shadow, 'B' the pressed glyph, which drops one
// pixel down and right and loses its shadow. Floating windows' buttons are
// too small for them, and get 8 x 8 versions drawn the same way.

void
GonxDecorator::_DrawClose(Decorator::Tab* tab, bool direct, BRect rect)
{
	static const char* const kClose[] = {
		"WWW...WWW.",
		"WWWW.WWWWS",
		"WWWWWWWWWS",
		".WWWWWWWSS",
		"..WWWWWSS.",
		".WWWWWWW..",
		"WWWWWWWWW.",
		"WWWWSWWWWS",
		"WWWSS.WWWS",
		".SSS...SSS"
	};
	static const char* const kClosePressed[] = {
		"BBB...BBB.",
		"BBBB.BBBB.",
		"BBBBBBBBB.",
		".BBBBBBB..",
		"..BBBBB...",
		".BBBBBBB..",
		"BBBBBBBBB.",
		"BBBB.BBBB.",
		"BBB...BBB.",
		".........."
	};
	static const char* const kSmallClose[] = {
		"WW...WW.",
		"WWW.WWWS",
		".WWWWWSS",
		"..WWWSS.",
		".WWWWW..",
		"WWWSWWW.",
		"WWSS.WWS",
		".SS...SS"
	};
	static const char* const kSmallClosePressed[] = {
		"........",
		".BB...BB",
		".BBB.BBB",
		"..BBBBB.",
		"...BBB..",
		"..BBBBB.",
		".BBB.BBB",
		".BB...BB"
	};
	_DrawGlyph(rect, tab->closePressed ? kClosePressed : kClose,
		tab->closePressed ? kSmallClosePressed : kSmallClose, direct);
}


void
GonxDecorator::_DrawZoom(Decorator::Tab* tab, bool direct, BRect rect)
{
	static const char* const kZoom[] = {
		"WWWW.WWWW.",
		"WWSSS.SWWS",
		"WSS.....WS",
		"WS......WS",
		".S..W....S",
		"W....S..W.",
		"WS......WS",
		"WW.....WWS",
		"WWWW.WWWWS",
		".SSSS.SSSS"
	};
	static const char* const kZoomPressed[] = {
		"..........",
		".BBBB.BBBB",
		".BB.....BB",
		".B.......B",
		".B.......B",
		".....B....",
		".B.......B",
		".B.......B",
		".BB.....BB",
		".BBBB.BBBB"
	};
	static const char* const kSmallZoom[] = {
		"WWW.WWW.",
		"WWSS.WWS",
		"WSS...WS",
		".S.W...S",
		"W...S.W.",
		"WW...WWS",
		"WWW.WWWS",
		".SSS.SSS"
	};
	static const char* const kSmallZoomPressed[] = {
		"........",
		".BBB.BBB",
		".BB...BB",
		".B.....B",
		"....B...",
		".B.....B",
		".BB...BB",
		".BBB.BBB"
	};
	_DrawGlyph(rect, tab->zoomPressed ? kZoomPressed : kZoom,
		tab->zoomPressed ? kSmallZoomPressed : kSmallZoom, direct);
}


void
GonxDecorator::_DrawMinimize(Decorator::Tab* tab, bool direct, BRect rect)
{
	static const char* const kMinimize[] = {
		"WWWW......",
		"WWWWS.....",
		"WWWWS.....",
		"WWWWSWWWW.",
		".SSSSWWWWS",
		"...WWWWWWS",
		"...WWWWWWS",
		"...WWWWWWS",
		"...WWWWWWS",
		"....SSSSSS"
	};
	static const char* const kMinimizePressed[] = {
		"..........",
		".BBBB.....",
		".BBBB.....",
		".BBBB.....",
		".BBBBBBBBB",
		"....BBBBBB",
		"....BBBBBB",
		"....BBBBBB",
		"....BBBBBB",
		"....BBBBBB"
	};
	static const char* const kSmallMinimize[] = {
		"WWW.....",
		"WWWS....",
		"WWWSWWW.",
		".SSSWWWS",
		"..WWWWWS",
		"..WWWWWS",
		"..WWWWWS",
		"...SSSSS"
	};
	static const char* const kSmallMinimizePressed[] = {
		"........",
		".BBB....",
		".BBB....",
		".BBB.BBB",
		".....BBB",
		"...BBBBB",
		"...BBBBB",
		"...BBBBB"
	};
	_DrawGlyph(rect, tab->minimizePressed ? kMinimizePressed : kMinimize,
		tab->minimizePressed ? kSmallMinimizePressed : kSmallMinimize,
		direct);
}


void
GonxDecorator::_SetTitle(Decorator::Tab* tab, const char* string,
	BRegion* updateRegion)
{
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion == NULL)
		return;

	BRegion footprint;
	_GetFootprint(&footprint);
	updateRegion->Include(&footprint);
}


void
GonxDecorator::_SetFocus(Decorator::Tab* tab)
{
	tab->buttonFocus = IsFocus(tab)
		|| ((tab->look == B_FLOATING_WINDOW_LOOK
			|| tab->look == kLeftTitledWindowLook)
			&& (tab->flags & B_AVOID_FOCUS) != 0);
}


bool
GonxDecorator::_SetTabLocation(Decorator::Tab* _tab, float location,
	bool isShifting, BRegion* updateRegion)
{
	// Tabs of a stack sit side by side; only a lone tab slides.
	if (CountTabs() != 1 || !_HasTab())
		return false;

	Tab* tab = static_cast<Tab*>(_tab);
	const Geometry geometry = _Geometry();

	const float available = fFrame.Width() + 1 + 2 * fBorderWidth
		+ geometry.overhang;
	const float maxOffset = floorf(available - _TabsWidth(geometry));
	if (maxOffset <= 0)
		return false;

	location = std::max(0.0f, std::min(location, maxOffset));
	if ((uint32)floorf(location) == tab->tabOffset)
		return false;

	if (updateRegion != NULL) {
		updateRegion->Include(fTitleBarRect);
		updateRegion->Include(_FlapRect(geometry));
	}

	tab->tabLocation = location / maxOffset;
	_LayoutTabs(geometry);

	if (updateRegion != NULL) {
		updateRegion->Include(fTitleBarRect);
		updateRegion->Include(_FlapRect(geometry));
	}

	return true;
}


bool
GonxDecorator::_SetSettings(const BMessage& settings, BRegion* updateRegion)
{
	float location;
	bool modified = false;
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		if (settings.FindFloat("tab location", i, &location) != B_OK)
			return false;
		modified |= _SetTabLocation(_TabAt(i), location, false, updateRegion);
	}
	return modified;
}


void
GonxDecorator::_UpdateFont(DesktopSettings& settings)
{
	// bold, as in the concept's tabs
	ServerFont font;
	if (_IsSmall())
		settings.GetDefaultPlainFont(font);
	else
		settings.GetDefaultBoldFont(font);

	font.SetFlags(B_FORCE_ANTIALIASING);
	font.SetSpacing(B_STRING_SPACING);
	fDrawState.SetFont(font);
}


void
GonxDecorator::_MoveBy(BPoint offset)
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(i));
		offset_rect(tab->tabRect, offset);
		offset_rect(tab->titleRect, offset);
		offset_rect(tab->shelfRect, offset);
		offset_rect(tab->closeRect, offset);
		offset_rect(tab->zoomRect, offset);
		offset_rect(tab->minimizeRect, offset);
	}

	fFrame.OffsetBy(offset);
	offset_rect(fTitleBarRect, offset);
	offset_rect(fResizeRect, offset);
	offset_rect(fBorderRect, offset);
	offset_rect(fLeftBorder, offset);
	offset_rect(fRightBorder, offset);
	offset_rect(fTopBorder, offset);
	offset_rect(fBottomBorder, offset);
}


void
GonxDecorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	if (dirty != NULL) {
		BRegion before;
		_GetFootprint(&before);
		dirty->Include(&before);
	}

	fFrame.right += offset.x;
	fFrame.bottom += offset.y;
	_DoLayout();

	if (dirty != NULL) {
		BRegion after;
		_GetFootprint(&after);
		dirty->Include(&after);
	}
}


bool
GonxDecorator::_AddTab(DesktopSettings& settings, int32 index,
	BRegion* updateRegion)
{
	if (updateRegion != NULL && fTitleBarRect.IsValid())
		updateRegion->Include(fTitleBarRect);

	_UpdateFont(settings);
	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


bool
GonxDecorator::_RemoveTab(int32 index, BRegion* updateRegion)
{
	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


bool
GonxDecorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);

	_DoLayout();

	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


void
GonxDecorator::_GetFootprint(BRegion* region)
{
	if (region == NULL || fTopTab->look == B_NO_BORDER_WINDOW_LOOK
		|| fBorderWidth <= 0)
		return;

	if (fTopTab->look == B_BORDERED_WINDOW_LOOK) {
		region->Include(fTopBorder);
		region->Include(fLeftBorder);
		region->Include(fRightBorder);
		region->Include(fBottomBorder);
		return;
	}

	const Geometry geometry = _Geometry();

	BRegion frame;
	_IncludeRounded(&frame, fBorderRect, geometry.frameRadius);
	frame.Exclude(fFrame);
	region->Include(&frame);

	// the content's rounded corners (see _DrawInnerCorners())
	const bool knob = fTopTab->look == B_DOCUMENT_WINDOW_LOOK
		&& (fTopTab->flags & B_NOT_RESIZABLE) == 0;
	region->Include(BRect(fFrame.left, fFrame.top, fFrame.left + 1,
		fFrame.top));
	region->Include(BRect(fFrame.left, fFrame.top + 1, fFrame.left,
		fFrame.top + 1));
	region->Include(BRect(fFrame.right - 1, fFrame.top, fFrame.right,
		fFrame.top));
	region->Include(BRect(fFrame.right, fFrame.top + 1, fFrame.right,
		fFrame.top + 1));
	region->Include(BRect(fFrame.left, fFrame.bottom, fFrame.left + 1,
		fFrame.bottom));
	region->Include(BRect(fFrame.left, fFrame.bottom - 1, fFrame.left,
		fFrame.bottom - 1));
	if (!knob) {
		region->Include(BRect(fFrame.right - 1, fFrame.bottom, fFrame.right,
			fFrame.bottom));
		region->Include(BRect(fFrame.right, fFrame.bottom - 1, fFrame.right,
			fFrame.bottom - 1));
	}

	if (fTopTab->look == B_DOCUMENT_WINDOW_LOOK
		&& (fTopTab->flags & B_NOT_RESIZABLE) == 0)
		region->Include(fResizeRect);

	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(i));
		if (!tab->titleRect.IsValid())
			continue;

		_IncludeTopRounded(region, tab->titleRect, geometry.tabRadius,
			geometry.tabRadius);
		_IncludeShelf(region, tab);
	}

	_IncludeFlap(region, geometry);
}


//	#pragma mark - Private methods


bool
GonxDecorator::_HasTab() const
{
	switch ((int)fTopTab->look) {
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
			return true;
	}
	return false;
}


bool
GonxDecorator::_IsSmall() const
{
	return fTopTab != NULL && (fTopTab->look == B_FLOATING_WINDOW_LOOK
		|| fTopTab->look == kLeftTitledWindowLook);
}


GonxDecorator::Geometry
GonxDecorator::_Geometry() const
{
	// Sizes measured from the concept screenshots at a 12 pt font, then
	// scaled with the font like the stock decorators do.
	Geometry geometry;
	const bool small = _IsSmall();
	const float scale = std::max(fDrawState.Font().Size() / 12.0f, 1.0f);

	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);
	const float textHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	geometry.scale = scale;
	geometry.sideBorder = roundf((small ? 3 : 5) * scale);
	geometry.topBand = roundf((small ? 7 : 11) * scale);
	// The tab rises well above the band, as in Dano's.
	geometry.tabHeight = std::max(textHeight + roundf(3 * scale),
		geometry.topBand + roundf((small ? 5 : 9) * scale));
	// the tab's foot stops short of the content, leaving a strip of the
	// band (and the content's dark edge) showing under it, as in the concept
	geometry.tabGap = roundf((small ? 2 : 3) * scale);
	// the flap hangs entirely outside the window, beside its left border
	geometry.flapWidth = roundf((small ? 7 : 11) * scale);
	geometry.overhang = geometry.flapWidth;
	// The flap runs well down the window's side and ends in a 45 degree
	// cut that lands on the frame's outer edge.
	geometry.flapDepth = roundf((small ? 6 : 11) * scale);
	geometry.tabRadius = roundf((small ? 5 : 8) * scale);
	geometry.tabRightRadius = roundf((small ? 6 : 10) * scale);
	geometry.tabFootRadius = roundf((small ? 5 : 8) * scale);
	geometry.frameRadius = roundf((small ? 4 : 6) * scale);
	geometry.titlePadding = roundf((small ? 7 : 10) * scale);
	geometry.buttonSize = roundf((small ? 8 : 10) * scale);
	geometry.buttonGap = roundf(2 * scale);
	geometry.shelfMargin = roundf((small ? 3 : 5) * scale);
	geometry.shelfRise = roundf((small ? 3 : 4) * scale);
	geometry.slope = roundf((small ? 5 : 8) * scale);
	return geometry;
}


int32
GonxDecorator::_ButtonCount(Tab* tab) const
{
	int32 count = 0;
	if ((tab->flags & B_NOT_CLOSABLE) == 0)
		count++;
	if ((tab->flags & B_NOT_ZOOMABLE) == 0)
		count++;
	if ((tab->flags & B_NOT_MINIMIZABLE) == 0)
		count++;
	return count;
}


float
GonxDecorator::_TabsWidth(const Geometry& geometry) const
{
	float width = 0;
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		const Tab* tab = static_cast<const Tab*>(fTabList.ItemAt(i));
		if (tab->tabRect.IsValid())
			width += tab->tabRect.Width() + 1;
	}
	return width;
}


float
GonxDecorator::_TabsOrigin(const Geometry& geometry) const
{
	return fFrame.left - fBorderWidth - geometry.overhang;
}


void
GonxDecorator::_LayoutTabs(const Geometry& geometry)
{
	const int32 count = fTabList.CountItems();
	if (count == 0) {
		fTitleBarRect.Set(0, 0, -1, -1);
		return;
	}

	const ServerFont& font = fDrawState.Font();
	const float origin = _TabsOrigin(geometry);
	const float available = fFrame.Width() + 1 + 2 * fBorderWidth
		+ geometry.overhang;

	const float tabBottom = fFrame.top - geometry.tabGap - 1;
	const float tabTop = tabBottom - geometry.tabHeight + 1;
	const float shelfTop = fTopBorder.top - geometry.shelfRise;
	// the title starts past the hatched grip at the tab's left end
	const float textOffset = geometry.flapWidth + roundf(5 * geometry.scale);
	const float minTitle = textOffset + geometry.titlePadding
		+ roundf(16 * geometry.scale);

	// what every tab would like, and what it could live with
	float wanted[count];
	float shelves[count];
	float sumWanted = 0;
	float sumShrinkable = 0;
	float sumMinimum = 0;
	for (int32 i = 0; i < count; i++) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(i));
		const char* title = Title(tab);
		wanted[i] = std::max(minTitle,
			ceilf(font.StringWidth(title, strlen(title)))
				+ textOffset + geometry.titlePadding);

		const int32 buttons = _ButtonCount(tab);
		shelves[i] = buttons == 0 ? 0
			: 2 * geometry.shelfMargin + buttons * geometry.buttonSize
				+ (buttons - 1) * geometry.buttonGap + geometry.slope;

		sumWanted += wanted[i] + shelves[i];
		sumShrinkable += wanted[i] - minTitle;
		sumMinimum += minTitle + shelves[i];
	}

	float shrink = 0;
	if (sumWanted > available && sumShrinkable > 0)
		shrink = std::min(1.0f, (sumWanted - available) / sumShrinkable);

	float x = origin;
	if (count == 1) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(0));
		const float width = wanted[0] - (wanted[0] - minTitle) * shrink
			+ shelves[0];
		const float maxOffset = std::max(0.0f, floorf(available - width));
		x += floorf(tab->tabLocation * maxOffset);
	}

	for (int32 i = 0; i < count; i++) {
		Tab* tab = static_cast<Tab*>(fTabList.ItemAt(i));
		const float titleWidth = floorf(wanted[i]
			- (wanted[i] - minTitle) * shrink);

		tab->titleRect.Set(x, tabTop, x + titleWidth - 1, tabBottom);
		tab->textOffset = textOffset;

		tab->closeRect.Set(0, 0, -1, -1);
		tab->zoomRect.Set(0, 0, -1, -1);
		tab->minimizeRect.Set(0, 0, -1, -1);

		if (shelves[i] > 0) {
			tab->shelfRect.Set(tab->titleRect.right + 1, shelfTop,
				tab->titleRect.right + shelves[i], fTopBorder.top + 1);
			tab->slope = geometry.slope;

			// buttons in the concept's order: close, zoom, minimize
			const float size = geometry.buttonSize;
			// centred beside the tab, but clear of the shelf's top edge
			const float top = std::max(shelfTop + 2,
				floorf((shelfTop + tabBottom + 1 - size) / 2));
			float left = tab->shelfRect.left + geometry.shelfMargin;
			BRect* rects[3] = { &tab->closeRect, &tab->zoomRect,
				&tab->minimizeRect };
			const uint32 hidden[3] = { B_NOT_CLOSABLE, B_NOT_ZOOMABLE,
				B_NOT_MINIMIZABLE };
			for (int32 b = 0; b < 3; b++) {
				if ((tab->flags & hidden[b]) != 0)
					continue;
				rects[b]->Set(left, top, left + size - 1, top + size - 1);
				left += size + geometry.buttonGap;
			}
		} else {
			tab->shelfRect.Set(0, 0, -1, -1);
			tab->slope = 0;
		}

		tab->tabRect.Set(tab->titleRect.left, tabTop,
			tab->shelfRect.IsValid() ? tab->shelfRect.right
				: tab->titleRect.right, tabBottom);
		tab->tabOffset = (uint32)(tab->tabRect.left - origin);

		tab->minTabSize = 0;
		tab->maxTabSize = wanted[i] + shelves[i];

		tab->truncatedTitle = Title(tab);
		font.TruncateString(&tab->truncatedTitle,
			count > 1 ? B_TRUNCATE_END : B_TRUNCATE_MIDDLE,
			titleWidth - textOffset - geometry.titlePadding);
		tab->truncatedTitleLength = tab->truncatedTitle.Length();

		if (i == 0)
			fTitleBarRect = tab->tabRect;
		else
			fTitleBarRect = fTitleBarRect | tab->tabRect;

		x = tab->tabRect.right + 1;
	}

	// Decorator::GetSizeLimits() keeps the window at least this wide
	static_cast<Tab*>(fTabList.ItemAt(0))->minTabSize
		= sumMinimum - geometry.overhang;
}


/*!	The first tab's left end turns down the window's side as a flap,
	outside the frame's left border, to well below the top of the content.
	Only while the tab sits at the left edge.
*/
BRect
GonxDecorator::_FlapRect(const Geometry& geometry) const
{
	const Tab* tab = static_cast<const Tab*>(fTabList.ItemAt(0));
	if (tab == NULL || !tab->titleRect.IsValid() || !_HasTab()
		|| tab->titleRect.left > fLeftBorder.left - 2)
		return BRect(0, 0, -1, -1);

	// it hangs beside the frame's left border without covering it
	return BRect(tab->titleRect.left, tab->titleRect.bottom + 1,
		fLeftBorder.left - 1, fFrame.top + geometry.flapDepth);
}


/*!	The tab's colour at row \a y: a blend from light at the tab's top to
	dark at the bottom of the flap, so the flap carries on where the tab
	leaves off.
*/
rgb_color
GonxDecorator::_TabColorAt(const Tab* tab, float y) const
{
	const bool focus = tab->isFocused;
	const rgb_color tabColor = focus ? fFocusTabColor : fNonFocusTabColor;
	// the concept's yellow stays bright down the tab and only darkens to
	// gold towards the bottom of the flap
	const rgb_color light = mix(tabColor, kWhite, focus ? 0.35f : 0.45f);
	const rgb_color dark = mix(tabColor, kBlack, focus ? 0.16f : 0.1f);

	const float top = tab->titleRect.top;
	const float bottom = fFrame.top + _Geometry().flapDepth;
	const float position = bottom > top
		? std::max(0.0f, std::min(1.0f, (y - top) / (bottom - top))) : 0;
	return mix(light, dark, position);
}


rgb_color
GonxDecorator::_TabOutline(const Tab* tab) const
{
	// a dark shade of the tab's own colour, as in the concept's tabs
	return tab->isFocused ? mix(fFocusTabColor, kBlack, 0.72f)
		: mix(fNonFocusTabColor, kBlack, 0.38f);
}


/*!	Dano's grip: light diagonals every fourth pixel, each followed by a
	slightly darker one, over the tab's colour.
*/
void
GonxDecorator::_DrawHatch(const Tab* tab, BRect rect)
{
	if (!rect.IsValid())
		return;

	for (float y = rect.top; y <= rect.bottom; y++) {
		const rgb_color color = _TabColorAt(tab, y);
		const rgb_color light = mix(color, kWhite, 0.4f);
		const rgb_color dark = mix(color, kBlack, 0.06f);
		for (float x = rect.left; x <= rect.right; x++) {
			const int32 phase = ((int32)(x + y) % 4 + 4) % 4;
			if (phase == 0)
				fDrawingEngine->FillRect(BRect(x, y, x, y), light);
			else if (phase == 2)
				fDrawingEngine->FillRect(BRect(x, y, x, y), dark);
		}
	}
}


/*!	How far row \a y of the flap is cut in from its left edge: nothing down
	the first part of its length, then a rounded bottom left corner whose
	bottom edge meets the frame's outer edge at a right angle.
*/
float
GonxDecorator::_FlapInset(const BRect& flap, float y) const
{
	// a quarter circle as wide as the flap: the left edge curves round
	// until the bottom edge runs level into the frame, meeting it square
	const float radius = std::min(flap.Width() + 1, flap.Height() + 1);
	return corner_inset(radius, (int32)(flap.bottom - y));
}


/*!	The flap's rounded bottom left corner, drawn smoothly: each pixel near
	the curve mixes the outline, the flap's colour (darkening just inside the
	edge, as in the concept) and what lies outside, by how much of it each
	covers. Outside, a soft shadow falls down and to the right. With \a region
	set this only collects the pixels it would touch, for the footprint.

	The decorator can't blend with what is behind the window, so outside the
	curve the desktop colour stands in for it.
*/
void
GonxDecorator::_FlapEdge(const BRect& flap, BRegion* region) const
{
	if (!flap.IsValid())
		return;

	// the flap spans [left, right + 1) x [top, bottom + 1) in pixel edges
	const float left = flap.left;
	const float bottom = flap.bottom + 1;
	const float radius = std::min(flap.Width() + 1, flap.Height() + 1);
	const float centerX = left + radius;
	const float centerY = bottom - radius;

	// signed distance from the flap's edge (positive outside), for the part
	// of its outline that faces left and down
	auto distance = [&](float x, float y) {
		if (x < centerX && y > centerY)
			return hypotf(x - centerX, y - centerY) - radius;
		return std::max(left - x, y - bottom);
	};

	const Tab* tab = static_cast<const Tab*>(fTabList.ItemAt(0));
	const rgb_color outline = _TabOutline(tab);
	const rgb_color desktop = _DesktopColor();
	// only a hint of a shadow
	const float kShadowOffset = 1.0f;
	const float kShadowReach = 2.0f;
	const float kShadowStrength = 0.22f;

	const float lastX = std::min(flap.right + 4, fLeftBorder.left - 1);
	for (float py = floorf(centerY) - 1; py <= flap.bottom + 4; py++) {
		for (float px = flap.left - 1; px <= lastX; px++) {
			const float x = px + 0.5f;
			const float y = py + 0.5f;
			const float d = distance(x, y);

			// coverage of outside, the one pixel outline, and the inside
			const float outside = std::max(0.0f, std::min(1.0f, d + 0.5f));
			const float inside = std::max(0.0f, std::min(1.0f, -d - 0.5f));
			const float line = 1 - outside - inside;

			const float shadowDistance = std::max(0.0f,
				distance(x - kShadowOffset, y - kShadowOffset));
			const float shadow = kShadowStrength
				* std::max(0.0f, 1 - shadowDistance / kShadowReach);

			const bool rowInside = py <= flap.bottom
				&& px >= flap.left + _FlapInset(flap, py);
			if (outside >= 1 && shadow < 0.02f && !rowInside)
				continue;
			if (inside >= 1 && d < -2.5f)
				continue;

			if (region != NULL) {
				region->Include(BRect(px, py, px, py));
				continue;
			}

			const rgb_color under = mix(desktop, kBlack, shadow);
			const rgb_color fill = mix(_TabColorAt(tab, py), kBlack,
				0.3f * std::max(0.0f, std::min(1.0f, 2.5f + d)));

			rgb_color color;
			color.red = (uint8)roundf(fill.red * inside + outline.red * line
				+ under.red * outside);
			color.green = (uint8)roundf(fill.green * inside
				+ outline.green * line + under.green * outside);
			color.blue = (uint8)roundf(fill.blue * inside + outline.blue * line
				+ under.blue * outside);
			color.alpha = 255;
			fDrawingEngine->FillRect(BRect(px, py, px, py), color);
		}
	}
}


/*!	The colour the flap's shadow darkens, standing in for what is behind the
	window (a decorator can't blend with it): the Gonx desktop colour, which
	gonx-theme sets. Reading the actual workspace colour would mean reaching
	into app_server's Desktop internals, which change between builds.
*/
rgb_color
GonxDecorator::_DesktopColor() const
{
	static const rgb_color kGonxDesktop = { 128, 157, 189, 255 };
	return kGonxDesktop;
}


void
GonxDecorator::_IncludeFlap(BRegion* region, const Geometry& geometry) const
{
	const BRect flap = _FlapRect(geometry);
	if (!flap.IsValid())
		return;

	for (float y = flap.top; y <= flap.bottom; y++) {
		region->Include(BRect(flap.left + _FlapInset(flap, y), y, flap.right,
			y));
	}

	_FlapEdge(flap, region);
}


void
GonxDecorator::_DrawFlap(BRect invalid)
{
	const Geometry geometry = _Geometry();
	const BRect flap = _FlapRect(geometry);
	// the shadow reaches three pixels past the flap
	if (!flap.IsValid()
		|| !invalid.Intersects(BRect(flap.left, flap.top, flap.right + 3,
			flap.bottom + 3)))
		return;

	const Tab* tab = static_cast<const Tab*>(fTabList.ItemAt(0));
	const rgb_color outline = _TabOutline(tab);

	for (float y = flap.top; y <= flap.bottom; y++) {
		const float inset = _FlapInset(flap, y);
		const float left = flap.left + inset;

		// the bottom row closes the flap over the frame's border
		if (y == flap.bottom) {
			fDrawingEngine->FillRect(BRect(left, y, flap.right, y), outline);
			continue;
		}

		const rgb_color color = _TabColorAt(tab, y);
		fDrawingEngine->FillRect(BRect(left, y, flap.right - 1, y), color);

		// outline: the left edge and its cut, joining this row's edge to the
		// next row's, and the side facing the content
		const float below = _FlapInset(flap, y + 1);
		fDrawingEngine->FillRect(BRect(left, y,
			flap.left + std::max(inset, below - 1), y), outline);
		fDrawingEngine->FillRect(BRect(flap.right, y, flap.right, y),
			outline);
		if (inset == 0 && below == 0) {
			fDrawingEngine->FillRect(BRect(flap.left + 1, y, flap.left + 1,
				y), mix(color, kWhite, 0.35f));
		} else {
			// round the curve the edge is heavier, as in the concept
			const float shade = flap.left + std::max(inset, below - 1) + 1;
			fDrawingEngine->FillRect(BRect(shade, y, shade, y),
				mix(color, kBlack, 0.3f));
		}

		// the grip follows the cut down
		_DrawHatch(tab, BRect(flap.left + std::max(inset, below - 1) + 2, y,
			flap.right - 2, y));
	}

	// redraw the rounded corner smoothly, with its shadow
	_FlapEdge(flap, NULL);

	// The content's top left corner is the frame's: its dark inner line
	// curves round it between the tab's foot and the flap.
	static const char* const kCorner[] = {
		"BBBD",
		"BBD.",
		"BD..",
		"D..."
	};
	const rgb_color base = _FrameColor(REGION_LEFT_BORDER);
	for (int32 y = 0; y < 4; y++) {
		for (int32 x = 0; x < 4; x++) {
			const char pixel = kCorner[y][x];
			if (pixel == '.')
				continue;
			const float px = fFrame.left - 1 + x;
			const float py = fFrame.top - 1 + y;
			fDrawingEngine->FillRect(BRect(px, py, px, py),
				pixel == 'D' ? kFrameInnerDark : base);
		}
	}
}


void
GonxDecorator::_IncludeRounded(BRegion* region, BRect rect,
	float radius) const
{
	if (!rect.IsValid())
		return;

	const int32 rows = (int32)std::min(radius, floorf((rect.Height() + 1) / 2));
	for (int32 row = 0; row < rows; row++) {
		const float inset = corner_inset(radius, row);
		region->Include(BRect(rect.left + inset, rect.top + row,
			rect.right - inset, rect.top + row));
		region->Include(BRect(rect.left + inset, rect.bottom - row,
			rect.right - inset, rect.bottom - row));
	}
	if (rect.top + rows <= rect.bottom - rows) {
		region->Include(BRect(rect.left, rect.top + rows, rect.right,
			rect.bottom - rows));
	}
}


/*!	Outlines the four rounded outer corners of the frame, light at the top
	left, dark at the bottom right. Each row's outline runs from its own edge
	to the edge of the row nearer the middle, and the pixel inside it takes
	the line that runs beside the outline on that side.
*/
void
GonxDecorator::_DrawCornerOutlines(float radius, rgb_color highlight)
{
	const BRect& r = fBorderRect;
	const int32 rows = (int32)std::min(radius, floorf((r.Height() + 1) / 2));
	const float half = rows / 2.0f;

	for (int32 row = 0; row < rows; row++) {
		const float inset = corner_inset(radius, row);
		const float previous = row == 0 ? inset + 1
			: corner_inset(radius, row - 1);
		const float end = std::max(inset, previous - 1);

		for (int32 i = 0; i < 2; i++) {
			const bool top = i == 0;
			const float y = top ? r.top + row : r.bottom - row;

			// near the top edge the top's colours win, near the sides theirs
			const rgb_color leftOutline = top || row >= half
				? kFrameOutlineLight : kFrameOutlineDark;
			const rgb_color rightOutline = top && row < half
				? kFrameOutlineLight : kFrameOutlineDark;
			const rgb_color leftInner = top || row >= half
				? highlight : kFrameEdgeDark;
			const rgb_color rightInner = top && row < half
				? highlight : kFrameEdgeDark;

			fDrawingEngine->FillRect(BRect(r.left + inset, y, r.left + end, y),
				leftOutline);
			fDrawingEngine->FillRect(BRect(r.right - end, y, r.right - inset,
				y), rightOutline);
			if (row > 0) {
				fDrawingEngine->FillRect(BRect(r.left + end + 1, y,
					r.left + end + 1, y), leftInner);
				fDrawingEngine->FillRect(BRect(r.right - end - 1, y,
					r.right - end - 1, y), rightInner);
			}
		}
	}
}


/*!	The content's corners are rounded too: the frame takes a few of their
	pixels (see _GetFootprint()) and the dark line round the content follows
	the curve. A document window's resize knob already fills its corner.
*/
void
GonxDecorator::_DrawInnerCorners(rgb_color base)
{
	// relative to the corner of the dark line: 'B' body, 'D' dark line
	static const char* const kCorner[] = {
		"BBBD",
		"BBD.",
		"BD..",
		"D..."
	};

	const BRect line(fFrame.left - 1, fFrame.top - 1, fFrame.right + 1,
		fFrame.bottom + 1);
	const bool knob = fTopTab->look == B_DOCUMENT_WINDOW_LOOK
		&& (fTopTab->flags & B_NOT_RESIZABLE) == 0;

	for (int32 corner = 0; corner < 4; corner++) {
		const bool right = corner == 1 || corner == 3;
		const bool bottom = corner >= 2;
		if (right && bottom && knob)
			continue;

		for (int32 y = 0; y < 4; y++) {
			for (int32 x = 0; x < 4; x++) {
				const char pixel = kCorner[y][x];
				if (pixel == '.')
					continue;
				const float px = right ? line.right - x : line.left + x;
				const float py = bottom ? line.bottom - y : line.top + y;
				fDrawingEngine->FillRect(BRect(px, py, px, py),
					pixel == 'D' ? kFrameInnerDark : base);
			}
		}
	}
}


/*!	A two-tone bracket inside the bottom right corner marks where the window
	is resized, like Dano's frame.
*/
void
GonxDecorator::_DrawResizeGrip(const Geometry& geometry, rgb_color highlight)
{
	if (fBorderWidth < 4)
		return;

	const float length = roundf(12 * geometry.scale);
	const float right = fBorderRect.right;
	const float bottom = fBorderRect.bottom;
	const rgb_color dark = kFrameEdgeDark;

	// down the right side, round the corner, along the bottom
	fDrawingEngine->StrokeLine(BPoint(right - 3, bottom - length),
		BPoint(right - 3, bottom - 5), dark);
	fDrawingEngine->StrokeLine(BPoint(right - 2, bottom - length),
		BPoint(right - 2, bottom - 4), highlight);
	fDrawingEngine->StrokeLine(BPoint(right - 4, bottom - 4),
		BPoint(right - 4, bottom - 4), dark);
	fDrawingEngine->StrokeLine(BPoint(right - 3, bottom - 3),
		BPoint(right - 3, bottom - 3), highlight);
	fDrawingEngine->StrokeLine(BPoint(right - length, bottom - 3),
		BPoint(right - 5, bottom - 3), dark);
	fDrawingEngine->StrokeLine(BPoint(right - length, bottom - 2),
		BPoint(right - 4, bottom - 2), highlight);
}


void
GonxDecorator::_IncludeTopRounded(BRegion* region, BRect rect,
	float leftRadius, float rightRadius) const
{
	if (!rect.IsValid())
		return;

	const int32 rows = (int32)std::max(leftRadius, rightRadius);
	int32 row = 0;
	for (; row < rows && rect.top + row <= rect.bottom; row++) {
		const float y = rect.top + row;
		region->Include(BRect(rect.left + corner_inset(leftRadius, row), y,
			rect.right - corner_inset(rightRadius, row), y));
	}
	if (rect.top + row <= rect.bottom)
		region->Include(BRect(rect.left, rect.top + row, rect.right,
			rect.bottom));
}


void
GonxDecorator::_IncludeShelf(BRegion* region, Tab* tab) const
{
	const BRect& shelf = tab->shelfRect;
	if (!shelf.IsValid())
		return;

	// rows above the band: the right edge slopes down to meet it
	const int32 rows = (int32)(fTopBorder.top - shelf.top);
	for (int32 row = 0; row < rows; row++) {
		const float right = shelf.right
			- roundf(tab->slope * (rows - 1 - row) / rows);
		region->Include(BRect(shelf.left, shelf.top + row, right,
			shelf.top + row));
	}
}


/*!	Fills \a rect with a vertical blend from \a top to \a bottom, rounding
	the two top corners, and outlines its top and sides. The row under the
	top outline and the column inside the left outline get \a highlight.
*/
void
GonxDecorator::_FillTopRounded(BRect rect, float leftRadius,
	float rightRadius, rgb_color top, rgb_color bottom, rgb_color outline,
	rgb_color highlight, float bottomRightRadius)
{
	if (!rect.IsValid())
		return;

	const int32 height = rect.IntegerHeight() + 1;
	float previousLeft = 0;
	float previousRight = 0;

	for (int32 row = 0; row < height; row++) {
		const float y = rect.top + row;
		const float insetLeft = corner_inset(leftRadius, row);
		const int32 fromBottom = height - 1 - row;
		const float footInset = corner_inset(bottomRightRadius, fromBottom);
		const float insetRight = std::max(corner_inset(rightRadius, row),
			footInset);
		const float left = rect.left + insetLeft;
		const float right = rect.right - insetRight;

		if (row == 0) {
			fDrawingEngine->FillRect(BRect(left, y, right, y), outline);
			previousLeft = insetLeft;
			previousRight = insetRight;
			continue;
		}

		const rgb_color color = mix(top, bottom,
			height > 1 ? (float)row / (height - 1) : 0);
		fDrawingEngine->FillRect(BRect(left, y, right, y),
			row == 1 ? highlight : color);

		// outline runs that join this row's edge to the previous one
		const float leftEnd = rect.left
			+ std::max(insetLeft, previousLeft - 1);
		// at the bottom right the curve heads inwards, so join to the row
		// below instead
		float rightJoin = std::max(insetRight, previousRight - 1);
		if (fromBottom < bottomRightRadius) {
			const float below = fromBottom == 0 ? footInset + 2
				: corner_inset(bottomRightRadius, fromBottom - 1);
			rightJoin = std::max(rightJoin, below - 1);
		}
		const float rightStart = rect.right - rightJoin;
		fDrawingEngine->FillRect(BRect(left, y, leftEnd, y), outline);
		fDrawingEngine->FillRect(BRect(rightStart, y, right, y), outline);

		if (row > 1 && leftEnd + 1 < rightStart) {
			fDrawingEngine->FillRect(BRect(leftEnd + 1, y, leftEnd + 1, y),
				highlight);
		}

		previousLeft = insetLeft;
		previousRight = insetRight;
	}
}


void
GonxDecorator::_DrawShelf(Tab* tab, rgb_color base, rgb_color outline,
	rgb_color highlight)
{
	const BRect& shelf = tab->shelfRect;
	const int32 rows = (int32)(fTopBorder.top - shelf.top);

	float previousRight = shelf.right;
	for (int32 row = 0; row < rows; row++) {
		const float y = shelf.top + row;
		const float right = shelf.right
			- roundf(tab->slope * (rows - 1 - row) / rows);

		if (row == 0) {
			fDrawingEngine->FillRect(BRect(shelf.left, y, right, y), outline);
		} else {
			fDrawingEngine->FillRect(BRect(shelf.left, y, right, y),
				row == 1 ? highlight : base);
			fDrawingEngine->FillRect(BRect(std::min(previousRight + 1, right),
				y, right, y), outline);
		}
		previousRight = right;
	}

	// Merge into the band: cover its top outline and highlight rows under
	// the shelf, so only the slope separates the two.
	for (int32 row = 0; row < 2; row++) {
		const float y = fTopBorder.top + row;
		if (y > shelf.bottom)
			break;
		fDrawingEngine->FillRect(BRect(shelf.left, y,
			row == 0 ? shelf.right - 1 : shelf.right, y), base);
	}
}


void
GonxDecorator::_DrawResizeKnob(rgb_color base)
{
	// the part of the knob that reaches into the content area
	BRect knob(fResizeRect.left, fResizeRect.top, fFrame.right, fFrame.bottom);
	if (!knob.IsValid())
		return;

	fDrawingEngine->FillRect(knob, base);
	fDrawingEngine->StrokeLine(knob.LeftTop(), knob.RightTop(),
		mix(base, fOutlineColor, 0.7f));
	fDrawingEngine->StrokeLine(knob.LeftTop(), knob.LeftBottom(),
		mix(base, fOutlineColor, 0.7f));

	if (!fTopTab->isFocused)
		return;

	// three ridges across the corner
	const rgb_color light = mix(base, kWhite, 0.5f);
	for (int32 i = 1; i <= 3; i++) {
		const float d = i * 4;
		fDrawingEngine->StrokeLine(BPoint(knob.right - d, knob.bottom),
			BPoint(knob.right, knob.bottom - d), fOutlineColor);
		fDrawingEngine->StrokeLine(BPoint(knob.right - d + 1, knob.bottom),
			BPoint(knob.right, knob.bottom - d + 1), light);
	}
}


/*!	Draws a 10 x 10 pixel glyph, scaled up by whole pixels for large fonts,
	centred in \a rect on the frame colour; or the 8 x 8 \a smallRows when
	\a rect is too small for it.
*/
void
GonxDecorator::_DrawGlyph(BRect rect, const char* const* rows,
	const char* const* smallRows, bool direct)
{
	if (!rect.IsValid())
		return;

	const bool copyToFront = fDrawingEngine->CopyToFrontEnabled();
	fDrawingEngine->SetCopyToFrontEnabled(direct);

	fDrawingEngine->FillRect(rect, _FrameColor(REGION_TOP_BORDER));

	const int32 count = rect.Width() + 1 < 10 ? 8 : 10;
	if (count == 8)
		rows = smallRows;

	const float pixel = std::max(1.0f, floorf((rect.Width() + 1) / count));
	const BPoint origin(
		rect.left + floorf((rect.Width() + 1 - count * pixel) / 2),
		rect.top + floorf((rect.Height() + 1 - count * pixel) / 2));

	for (int32 y = 0; y < count; y++) {
		for (int32 x = 0; x < count; x++) {
			rgb_color color;
			switch (rows[y][x]) {
				case 'W':
					color = kWhite;
					break;
				case 'S':
					color = kGlyphShadowColor;
					break;
				case 'B':
					color = kBlack;
					break;
				default:
					continue;
			}
			const float left = origin.x + x * pixel;
			const float top = origin.y + y * pixel;
			fDrawingEngine->FillRect(BRect(left, top, left + pixel - 1,
				top + pixel - 1), color);
		}
	}

	fDrawingEngine->SetCopyToFrontEnabled(copyToFront);
}


rgb_color
GonxDecorator::_FrameColor(Region region) const
{
	const rgb_color base = fTopTab != NULL && fTopTab->isFocused
		? fFrameColor : fInactiveFrameColor;

	if (RegionHighlight(region) == HIGHLIGHT_RESIZE_BORDER)
		return mix(base, kResizeHighlightColor, 0.45f);

	return base;
}


extern "C" DecorAddOn*
instantiate_decor_addon(image_id id, const char* name)
{
	return new (std::nothrow) GonxDecorAddOn(id, name);
}
