/*
 * Copyright 2026, Gonx contributors.
 * Distributed under the terms of the MIT License.
 *
 * Window decorator after the "Gonx" BeOS concept artwork (cotito.free.fr,
 * c. 2001): a rounded title tab that hangs past the left edge of the window,
 * the close/zoom/minimize buttons squeezed together on a raised shelf just
 * right of the tab, and a thick blue-grey frame whose top band is taller
 * than the sides.
 */
#ifndef GONX_DECORATOR_H
#define GONX_DECORATOR_H


#include <Region.h>

#include "Decorator.h"
#include "DecorManager.h"


class Desktop;


class GonxDecorAddOn : public DecorAddOn {
public:
								GonxDecorAddOn(image_id id, const char* name);

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


class GonxDecorator : public Decorator {
public:
	struct Tab : public Decorator::Tab {
		BRect					titleRect;
			// the painted, rounded title tab
		BRect					shelfRect;
			// the raised strip carrying the buttons, slope included
		float					slope;
	};

	struct Geometry {
		float					scale;
		float					sideBorder;
		float					topBand;
		float					tabHeight;
		float					tabGap;
			// band left visible below the tab
		float					overhang;
		float					flapWidth;
			// the flap's width, frame border included
		float					flapDepth;
			// how far the tab's left side runs down the window's edge
		float					tabRadius;
		float					tabRightRadius;
		float					tabFootRadius;
			// where the tab's right end meets the band
		float					frameRadius;
		float					titlePadding;
		float					buttonSize;
		float					buttonGap;
		float					shelfMargin;
		float					shelfRise;
		float					slope;
	};

								GonxDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~GonxDecorator();

	virtual	void				Draw(BRect updateRect);
	virtual	void				Draw();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;

	virtual	void				UpdateColors(DesktopSettings& settings);

protected:
	virtual	Decorator::Tab*		_AllocateNewTab();

	virtual	void				_DoLayout();
	virtual	void				_DoOutlineLayout();

	virtual	void				_DrawFrame(BRect rect);
	virtual	void				_DrawOutlineFrame(BRect rect);

	virtual	void				_DrawTab(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawButtons(Decorator::Tab* tab,
									const BRect& invalid);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawMinimize(Decorator::Tab* tab,
									bool direct, BRect rect);
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect rect);

	virtual	void				_SetTitle(Decorator::Tab* tab,
									const char* string,
									BRegion* updateRegion = NULL);
	virtual	void				_SetFocus(Decorator::Tab* tab);
	virtual	bool				_SetTabLocation(Decorator::Tab* tab,
									float location, bool isShifting,
									BRegion* updateRegion = NULL);
	virtual	bool				_SetSettings(const BMessage& settings,
									BRegion* updateRegion = NULL);

	virtual	void				_UpdateFont(DesktopSettings& settings);

	virtual	void				_MoveBy(BPoint offset);
	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);

	virtual	bool				_AddTab(DesktopSettings& settings,
									int32 index = -1,
									BRegion* updateRegion = NULL);
	virtual	bool				_RemoveTab(int32 index,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

	virtual	void				_GetFootprint(BRegion* region);

private:
			bool				_HasTab() const;
			bool				_IsSmall() const;
			Geometry			_Geometry() const;
			void				_LayoutTabs(const Geometry& geometry);
			int32				_ButtonCount(Tab* tab) const;
			float				_TabsWidth(const Geometry& geometry) const;
			float				_TabsOrigin(const Geometry& geometry) const;

			BRect				_FlapRect(const Geometry& geometry) const;
			rgb_color			_TabColorAt(const Tab* tab, float y) const;
			rgb_color			_TabOutline(const Tab* tab) const;
			void				_DrawHatch(const Tab* tab, BRect rect);
			float				_FlapInset(const BRect& flap, float y) const;
			void				_FlapEdge(const BRect& flap,
									BRegion* region) const;
			rgb_color			_DesktopColor() const;
			void				_IncludeFlap(BRegion* region,
									const Geometry& geometry) const;
			void				_DrawFlap(BRect invalid);
			void				_IncludeRounded(BRegion* region, BRect rect,
									float radius) const;
			void				_DrawCornerOutlines(float radius,
									rgb_color highlight);
			void				_DrawInnerCorners(rgb_color base);
			void				_DrawResizeGrip(const Geometry& geometry,
									rgb_color highlight);

			void				_IncludeTopRounded(BRegion* region,
									BRect rect, float leftRadius,
									float rightRadius) const;
			void				_IncludeShelf(BRegion* region, Tab* tab) const;

			void				_FillTopRounded(BRect rect, float leftRadius,
									float rightRadius, rgb_color top,
									rgb_color bottom, rgb_color outline,
									rgb_color highlight,
									float bottomRightRadius = 0);
			void				_DrawShelf(Tab* tab, rgb_color base,
									rgb_color outline, rgb_color highlight);
			void				_DrawResizeKnob(rgb_color base);
			void				_DrawGlyph(BRect rect, const char* const* rows,
									const char* const* smallRows, bool direct);

			rgb_color			_FrameColor(Region region) const;

private:
			rgb_color			fFrameColor;
			rgb_color			fInactiveFrameColor;
			rgb_color			fOutlineColor;

			rgb_color			fFocusTabColor;
			rgb_color			fFocusTextColor;
			rgb_color			fNonFocusTabColor;
			rgb_color			fNonFocusTextColor;
};


#endif	// GONX_DECORATOR_H
