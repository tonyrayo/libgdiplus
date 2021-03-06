/*
 * pathgradientbrush.c
 *
 * Copyright (C) 2003-2004,2007 Novell, Inc. http://www.novell.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
 * and associated documentation files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial 
 * portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT 
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Vladimir Vukicevic (vladimir@pobox.com)
 *	Ravindra (rkumar@novell.com)
 *
 */

#include "pathgradientbrush-private.h"
#include "gdiplus-private.h"
#include "graphics-private.h"
#include "graphics-path-private.h"
#include "matrix-private.h"

static GpStatus gdip_pgrad_setup (GpGraphics *graphics, GpBrush *brush);
static GpStatus gdip_pgrad_clone_brush (GpBrush *brush, GpBrush **clonedBrush);
static GpStatus gdip_pgrad_destroy (GpBrush *brush);

static BrushClass pathgradient_vtable = { BrushTypePathGradient,
										 gdip_pgrad_setup,
										 gdip_pgrad_clone_brush,
										 gdip_pgrad_destroy };

static GpStatus
gdip_pathgradient_init (GpPathGradient *pg)
{
	gdip_brush_init (&pg->base, &pathgradient_vtable);
	pg->boundary = NULL;
	pg->boundaryColors = (ARGB *) GdipAlloc (sizeof (ARGB));
	if (!pg->boundaryColors) {
		return OutOfMemory;
	}

	*(pg->boundaryColors) = MAKE_ARGB_ARGB(255,255,255,255); /* default boundary color is white */
	pg->boundaryColorsCount = 1; /* one default boundary color */
	pg->focusScales.X = 0.0f;
	pg->focusScales.Y = 0.0f;
	pg->wrapMode = WrapModeClamp;
	cairo_matrix_init_identity (&pg->transform);
	pg->presetColors = (InterpolationColors *) GdipAlloc (sizeof (InterpolationColors));
	if (!pg->presetColors) {
		GdipFree (pg->boundaryColors);
		return OutOfMemory;
	}

	pg->presetColors->count = 0;
	pg->presetColors->colors = NULL;
	pg->presetColors->positions = NULL;
	pg->blend = (Blend *) GdipAlloc (sizeof (Blend));
	if (!pg->blend) {
		GdipFree (pg->boundaryColors);
		GdipFree (pg->presetColors);
		return OutOfMemory;
	}

	pg->blend->count = 1;
	pg->blend->factors = (float *) GdipAlloc (sizeof (float));
	if (!pg->blend->factors) {
		GdipFree (pg->boundaryColors);
		GdipFree (pg->presetColors);
		GdipFree (pg->blend);
		return OutOfMemory;
	}

	pg->blend->positions = (float *) GdipAlloc (sizeof (float));
	if (!pg->blend->positions) {
		GdipFree (pg->boundaryColors);
		GdipFree (pg->presetColors);
		GdipFree (pg->blend->factors);
		GdipFree (pg->blend);
		return OutOfMemory;
	}

	pg->blend->factors [0] = 1.0;
	pg->blend->positions[0] = 0.0;
	pg->rectangle.X = 0.0;
	pg->rectangle.Y = 0.0;
	pg->rectangle.Width = 0.0;
	pg->rectangle.Height = 0.0;
	pg->pattern = NULL;
	pg->useGammaCorrection = FALSE;

	return Ok;
}

static GpPathGradient*
gdip_pathgradient_new (void)
{
	GpPathGradient *result = (GpPathGradient *) GdipAlloc (sizeof (GpPathGradient));

	if (result && gdip_pathgradient_init (result) != Ok) {
		GdipFree (result);
		return NULL;
	}

	return result;
}

GpStatus
gdip_pgrad_clone_brush (GpBrush *brush, GpBrush **clonedBrush)
{
	GpPathGradient *pgbrush;
	GpPathGradient *newbrush;

	if (!brush || !clonedBrush)
		return InvalidParameter;

	newbrush = (GpPathGradient *) GdipAlloc (sizeof (GpPathGradient));
	if (!newbrush)
		return OutOfMemory;

	pgbrush = (GpPathGradient *) brush;

	newbrush->base = pgbrush->base;
	if (pgbrush->boundary) {
		GdipClonePath (pgbrush->boundary, &newbrush->boundary);
	} else {
		newbrush->boundary = NULL;
	}

	newbrush->boundaryColors = GdipAlloc (sizeof(ARGB) * pgbrush->boundaryColorsCount);
	if (!newbrush->boundaryColors)
		goto NO_BOUNDARY_COLORS;

	memcpy (newbrush->boundaryColors, pgbrush->boundaryColors, sizeof(ARGB) * pgbrush->boundaryColorsCount);
	newbrush->boundaryColorsCount = pgbrush->boundaryColorsCount;
	newbrush->focusScales = pgbrush->focusScales;
	newbrush->center = pgbrush->center;
	newbrush->centerColor = pgbrush->centerColor;
	newbrush->wrapMode = pgbrush->wrapMode;
	newbrush->useGammaCorrection = pgbrush->useGammaCorrection;
	gdip_cairo_matrix_copy (&newbrush->transform, &pgbrush->transform);

	newbrush->rectangle.X = pgbrush->rectangle.X;
	newbrush->rectangle.Y = pgbrush->rectangle.Y;
	newbrush->rectangle.Width = pgbrush->rectangle.Width;
	newbrush->rectangle.Height = pgbrush->rectangle.Height;

	newbrush->presetColors = (InterpolationColors *) GdipAlloc (sizeof (InterpolationColors));

	if (newbrush->presetColors == NULL) 
		goto NO_PRESET;

	newbrush->presetColors->count = pgbrush->presetColors->count;
	if (pgbrush->presetColors->count > 0) {
		newbrush->presetColors->colors = (ARGB *) GdipAlloc (pgbrush->presetColors->count * sizeof (ARGB));
		if (newbrush->presetColors->colors == NULL) 
			goto NO_PRESET_COLORS;
		memcpy (newbrush->presetColors->colors, pgbrush->presetColors->colors, 
			pgbrush->presetColors->count * sizeof (ARGB));
		newbrush->presetColors->positions = (float *) GdipAlloc (pgbrush->presetColors->count * sizeof (float));
		if (newbrush->presetColors->positions == NULL)
			goto NO_PRESET_POSITIONS;
		memcpy (newbrush->presetColors->positions, pgbrush->presetColors->positions, 
			pgbrush->presetColors->count * sizeof (float));
	} else {
		memcpy (newbrush->presetColors, pgbrush->presetColors, sizeof (InterpolationColors));
	}

	newbrush->blend = (Blend *) GdipAlloc (sizeof (Blend));
	if (newbrush->blend == NULL)
		goto NO_BLEND;

	newbrush->blend->count = pgbrush->blend->count;
	if (pgbrush->blend->count > 0) {
		newbrush->blend->factors = (float *) GdipAlloc (pgbrush->blend->count * sizeof (float));
		if (newbrush->blend->factors == NULL) 
			goto NO_BLEND_FACTORS;
		memcpy (newbrush->blend->factors, pgbrush->blend->factors, pgbrush->blend->count * sizeof (ARGB));
		newbrush->blend->positions = (float *) GdipAlloc (pgbrush->blend->count * sizeof (float));
		if (newbrush->blend->positions == NULL)
			goto NO_BLEND_POSITIONS;
		memcpy (newbrush->blend->positions, pgbrush->blend->positions, pgbrush->blend->count * sizeof (float));
	} else {
		memcpy (newbrush->blend, pgbrush->blend, sizeof (Blend));
	}
	goto SUCCESS;
    
NO_BLEND_POSITIONS:
	GdipFree (newbrush->blend->factors);
NO_BLEND_FACTORS:
	GdipFree (newbrush->blend);
NO_BLEND:
NO_PRESET_POSITIONS:
	GdipFree (newbrush->presetColors->colors);
NO_PRESET_COLORS:
	GdipFree (newbrush->presetColors);
NO_PRESET:
	GdipFree (newbrush->boundaryColors);
NO_BOUNDARY_COLORS:
	GdipFree (newbrush);
	return OutOfMemory;

SUCCESS:
	/* Let the clone to create its own pattern */
	newbrush->base.changed = TRUE;
	newbrush->pattern = NULL;

	*clonedBrush = (GpBrush *) newbrush;    

	return Ok;
}

GpStatus
gdip_pgrad_destroy (GpBrush *brush)
{
	/* a. the NULL check for brush is done by the caller, GdipDeleteBrush */
	/* b. brush itself is freed by the caller */

	GpPathGradient *pgbrush = (GpPathGradient *) brush;

	if (pgbrush->boundary) {
		GdipDeletePath (pgbrush->boundary);
		pgbrush->boundary = NULL;
	}

	if (pgbrush->boundaryColors) {
		GdipFree (pgbrush->boundaryColors);
		pgbrush->boundaryColors = NULL;
	}

	if (pgbrush->pattern) {
		cairo_pattern_destroy (pgbrush->pattern);
		pgbrush->pattern = NULL;
	}

	if (pgbrush->blend) {
		if (pgbrush->blend->count > 0) {
			GdipFree (pgbrush->blend->factors);
			GdipFree (pgbrush->blend->positions);
		}
		GdipFree (pgbrush->blend);
		pgbrush->blend = NULL;
	}
	
	if (pgbrush->presetColors) {
		if (pgbrush->presetColors->count > 0) {
			GdipFree (pgbrush->presetColors->colors);
			GdipFree (pgbrush->presetColors->positions);
		}
		GdipFree (pgbrush->presetColors);
		pgbrush->presetColors = NULL;
	}

	return Ok;
}

static void
add_color_stops_from_blend (cairo_pattern_t *pattern, Blend *blend, ARGB color1, ARGB color2)
{
	int index;
	double sr, sg, sb, sa;
	double er, eg, eb, ea;
	double offset, factor;

	sa = (color1 >> 24) & 0xFF;
	sr = (color1 >> 16) & 0xFF;
	sg = (color1 >> 8) & 0xFF;
	sb = color1 & 0xFF;

	ea = (color2 >> 24) & 0xFF;
	er = (color2 >> 16) & 0xFF;
	eg = (color2 >> 8) & 0xFF;
	eb = color2 & 0xFF;

	for (index = 0; index < blend->count; index++) {
		factor = blend->factors [index];
		offset = blend->positions [index];

		cairo_pattern_add_color_stop_rgba (pattern, offset, 
					      ((sr * (1 - factor)) + (er * factor)) / 255,
					      ((sg * (1 - factor)) + (eg * factor)) / 255,
					      ((sb * (1 - factor)) + (eb * factor)) / 255,
					      ((sa * (1 - factor)) + (ea * factor)) / 255);
	}
}

static void
add_color_stops_from_interpolation_colors (cairo_pattern_t *pattern, InterpolationColors *presetColors)
{
	int index;
	double r, g, b, a;
	double offset;
	ARGB color;

	/* MS accecpts positions above 1.0 also. Cairo assumes the values above 1.0 as 1.0
	 * and values below 0 are assumed as 0. So we get different results if any of the
	 * offset values is out of [0.0, 1.0].
	 */
	for (index = 0; index < presetColors->count; index++) {
		color = presetColors->colors [index];
		a = (color >> 24) & 0xFF;
		r = (color >> 16) & 0xFF;
		g = (color >> 8) & 0xFF;
		b = color & 0xFF;
		offset = presetColors->positions [index];

		cairo_pattern_add_color_stop_rgba (pattern, offset, r / 255, g / 255, b / 255, a / 255);
	}
}

GpStatus
gdip_pgrad_setup (GpGraphics *graphics, GpBrush *brush)
{
	GpPathGradient *pgbrush;
	GpStatus status;

	if (!graphics || !brush)
		return InvalidParameter;

	pgbrush = (GpPathGradient *) brush;

	if (pgbrush->boundary == NULL)
		return Ok;              /* do nothing */

	/* We create the new pattern for brush, if the brush is changed
	 * or if pattern has not been created yet.
	 */
	if (pgbrush->base.changed || !pgbrush->pattern) {
		cairo_pattern_t *pat;
		float r = MIN (pgbrush->rectangle.Width / 2, pgbrush->rectangle.Height / 2);

		/* destroy the existing pattern */
		if (pgbrush->pattern) {
			cairo_pattern_destroy (pgbrush->pattern);
			pgbrush->pattern = NULL;
		}

		/* FIXME: To fully implement this function we need cairo to support path gradients.
		 * Right now we have radial gradient which can be used, in some cases, to get the right effect.
		 */

		pat = cairo_pattern_create_radial (pgbrush->center.X, pgbrush->center.Y, 0.0f,
			pgbrush->center.X, pgbrush->center.Y, r);
		status = gdip_get_pattern_status (pat);
		if (status != Ok)
			return status;

		cairo_pattern_set_matrix (pat, &pgbrush->transform);

		if ((pgbrush->blend->count > 1) && (pgbrush->boundaryColorsCount > 0)) {
			/* FIXME: blending done using the a radial shape (not the path shape) */
			add_color_stops_from_blend (pat, pgbrush->blend, pgbrush->boundaryColors[0], pgbrush->centerColor);
		} else if (pgbrush->presetColors->count > 1) {
			/* FIXME: copied from lineargradiantbrush, most probably not right */
			add_color_stops_from_interpolation_colors (pat, pgbrush->presetColors);
		} else {
			cairo_pattern_add_color_stop_rgba (pat, 0.0f,
				ARGB_RED_N (pgbrush->centerColor),
				ARGB_GREEN_N (pgbrush->centerColor),
				ARGB_BLUE_N (pgbrush->centerColor),
				ARGB_ALPHA_N (pgbrush->centerColor));

			/* if a single other boundary color is present, then we can do the a real radial */
			if (pgbrush->boundaryColorsCount == 1) {
				ARGB c = pgbrush->boundaryColors[0];
				cairo_pattern_add_color_stop_rgba (pat, 1.0f,
					ARGB_RED_N (c), ARGB_GREEN_N (c), ARGB_BLUE_N (c), ARGB_ALPHA_N (c));
			} else {
				/* FIXME: otherwise we (solid-)fill with the centerColor */
			}
		}

		pgbrush->pattern = pat;
	}

	cairo_set_source (graphics->ct, pgbrush->pattern);
	return gdip_get_status (cairo_status (graphics->ct));
}

static GpPointF
gdip_get_center (GDIPCONST GpPointF *points, int count)
{
	/* Center is the mean of all the points. */
	int i;
	GpPointF center = {0.0, 0.0};

	for (i = 0; i < count; i++) {
		center.X += points[i].X;
		center.Y += points[i].Y;
	}

	center.X /= count;
	center.Y /= count;

	return center;
}

static BOOL
gdip_all_colors_equal (GDIPCONST ARGB *colors, int count)
{
	int i;

	for (i = 1; i < count; i++) {
		if (colors[i] != colors[i - 1])
			return FALSE;
	}

	return TRUE;
}

static void
gdip_rect_expand_by (GpRectF *rect, GpPointF *point)
{
	/* This method is somewhat stupid, because GpRect is x,y width,height,
	* instead of x0,y0 x1,y1.
	*/
	float x0 = rect->X;
	float y0 = rect->Y;
	float x1 = x0 + rect->Width;
	float y1 = y0 + rect->Height;

	if (point->X < x0)
		x0 = point->X;
	else if (point->X > x1)
		x1 = point->X;

	if (point->Y < y0)
		y0 = point->Y;
	else if (point->Y > y1)
		y1 = point->Y;

	rect->X = x0;
	rect->Y = y0;
	rect->Width = (x1 - x0);
	rect->Height = (y1 - y0);
}

/* coverity[+alloc : arg-*3] */
GpStatus WINGDIPAPI
GdipCreatePathGradient (GDIPCONST GpPointF *points, INT count, GpWrapMode wrapMode, GpPathGradient **polyGradient)
{
	int i;
	GpPathGradient *gp;
	GpPath *gppath = NULL;
	GpStatus status;
	GpPointF point;

	if (!polyGradient)
		return InvalidParameter;

	if (!points || count < 2 || wrapMode < WrapModeTile || wrapMode > WrapModeClamp)
		return OutOfMemory;

	status = GdipCreatePath (FillModeAlternate, &gppath);
	if (status != Ok) {
		if (gppath)
			GdipDeletePath (gppath);
		return status;
	}

	GdipAddPathLine2 (gppath, points, count);

	gp = gdip_pathgradient_new ();
	if (!gp)
		return OutOfMemory;

	gp->boundary = gppath;
	gp->wrapMode = wrapMode;
	gp->center = gdip_get_center (points, count);
	gp->centerColor = MAKE_ARGB_ARGB(255,0,0,0); /* black center color */
    
	/* set the bounding rectangle */
	point = g_array_index (gppath->points, GpPointF, 0);
	/* set the first point as the edge of the rectangle */
	gp->rectangle.X = point.X;
	gp->rectangle.Y = point.Y;
	for (i = 1; i < gppath->count; i++) {
		point = g_array_index (gppath->points, GpPointF, i);
		gdip_rect_expand_by (&gp->rectangle, &point);
	}

	*polyGradient = gp;

	return Ok;
}

/* coverity[+alloc : arg-*3] */
GpStatus WINGDIPAPI
GdipCreatePathGradientI (GDIPCONST GpPoint *points, INT count, GpWrapMode wrapMode, GpPathGradient **polyGradient)
{
	int i;
	GpStatus result;
	GpPointF *newPoints;

	if (!polyGradient || !points)
		return InvalidParameter;

	if (count < 2 || wrapMode < WrapModeTile || wrapMode > WrapModeClamp)
		return OutOfMemory;

	newPoints = GdipAlloc (sizeof (GpPointF) * count);
	if (!newPoints)
		return OutOfMemory;

	for (i = 0; i < count; i++) {
		newPoints[i].X = points[i].X;
		newPoints[i].Y = points[i].Y;
	}
	result = GdipCreatePathGradient (newPoints, count, wrapMode, polyGradient);

	GdipFree (newPoints);
	return result;
}

/* coverity[+alloc : arg-*1] */
GpStatus WINGDIPAPI
GdipCreatePathGradientFromPath (GDIPCONST GpPath *path, GpPathGradient **polyGradient)
{
	int i, count;
	GpPathGradient *gp;
	GpPointF *points;

	if (!polyGradient)
		return InvalidParameter;

	if (!path || (path->count < 2))
		return OutOfMemory;

	gp = gdip_pathgradient_new ();
	if (!gp)
		return OutOfMemory;

	GdipClonePath ((GpPath*) path, &(gp->boundary));
	GdipGetPointCount ((GpPath*) path, &count);
	points = (GpPointF*) GdipAlloc (count * sizeof (GpPointF));
	GdipGetPathPoints ((GpPath*) path, points, count);
	gp->center = gdip_get_center (points, count);
	gp->centerColor = MAKE_ARGB_ARGB(255,255,255,255); /* white center color */

	/* set the bounding rectangle */
	/* set the first point as the edge of the rectangle */
	gp->rectangle.X = points [0].X;
	gp->rectangle.Y = points [0].Y;
	for (i = 1; i < count; i++) {
		gdip_rect_expand_by (&gp->rectangle, &points[i]);
	}

	*polyGradient = gp;

	GdipFree (points);
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientCenterColor (GpPathGradient *brush, ARGB *colors)
{
	if (!brush || !colors)
		return InvalidParameter;

	*colors = brush->centerColor;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientCenterColor (GpPathGradient *brush, ARGB colors)
{
	if (!brush)
		return InvalidParameter;

	brush->centerColor = colors;
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientSurroundColorsWithCount (GpPathGradient *brush, ARGB *colors, INT *count)
{
	int i;

	if (!brush || !colors || !count || *count < brush->boundary->count)
		return InvalidParameter;

	for (i = 0; i < brush->boundary->count; i++) {
		if (i < brush->boundaryColorsCount)
			colors[i] = brush->boundaryColors[i];
		else
			colors[i] = brush->boundaryColors[brush->boundaryColorsCount - 1];
	}

	*count = brush->boundaryColorsCount;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientSurroundColorsWithCount (GpPathGradient *brush, GDIPCONST ARGB *colors, INT *count)
{
	int boundaryColorsCount;

	if (!brush || !colors || !count)
		return InvalidParameter;

	if ((*count < 1) || (*count > brush->boundary->count))
		return InvalidParameter;

	boundaryColorsCount = *count;
	
	/* If all the colors are equal then GDI+ collapses them into a single element array */
	if (boundaryColorsCount > 1 && gdip_all_colors_equal(colors, boundaryColorsCount)) {
		boundaryColorsCount = 1;
	}

	if (boundaryColorsCount != brush->boundaryColorsCount) {
		GdipFree (brush->boundaryColors);
		brush->boundaryColors = (ARGB *) GdipAlloc (sizeof(ARGB) * boundaryColorsCount);
	}

	memcpy (brush->boundaryColors, colors, sizeof (ARGB) * boundaryColorsCount);
	brush->boundaryColorsCount = boundaryColorsCount;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientPath(GpPathGradient *brush, GpPath *path)
{
	// GDI+ does not implement this API.
	return NotImplemented;
}

GpStatus WINGDIPAPI
GdipSetPathGradientPath(GpPathGradient *brush, GDIPCONST GpPath *path)
{
	// GDI+ does not implement this API.
	return NotImplemented;
}

GpStatus WINGDIPAPI
GdipGetPathGradientCenterPoint (GpPathGradient *brush, GpPointF *point)
{
	if (!brush || !point)
		return InvalidParameter;

	point->X = brush->center.X;
	point->Y = brush->center.Y;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientCenterPointI (GpPathGradient *brush, GpPoint *point)
{
	if (!brush || !point)
		return InvalidParameter;

	point->X = iround(brush->center.X);
	point->Y = iround(brush->center.Y);
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientCenterPoint (GpPathGradient *brush, GDIPCONST GpPointF *point)
{
	if (!brush || !point)
		return InvalidParameter;
	
	brush->center.X = point->X;
	brush->center.Y = point->Y;
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientCenterPointI (GpPathGradient *brush, GDIPCONST GpPoint *point)
{
	if (!brush || !point)
		return InvalidParameter;

	brush->center.X = (REAL)point->X;
	brush->center.Y = (REAL)point->Y;
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientRect (GpPathGradient *brush, GpRectF *rect)
{
	if (!brush || !rect)
		return InvalidParameter;

	memcpy (rect, &brush->rectangle, sizeof (GpRectF));
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientRectI (GpPathGradient *brush, GpRect *rect)
{
	if (!brush || !rect)
		return InvalidParameter;

	gdip_Rect_from_RectF (&brush->rectangle, rect);
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientPointCount (GpPathGradient *brush, INT* count)
{
	if (!brush || !count)
		return InvalidParameter;

	*count = brush->boundary->count;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientGammaCorrection (GpPathGradient *brush, BOOL useGammaCorrection)
{
	if (!brush)
		return InvalidParameter;

	brush->useGammaCorrection = useGammaCorrection;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientGammaCorrection(GpPathGradient *brush, BOOL *useGammaCorrection)
{
	if (!brush || !useGammaCorrection)
		return InvalidParameter;

	*useGammaCorrection = brush->useGammaCorrection;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientSurroundColorCount (GpPathGradient *brush, INT *count)
{
	if (!brush || !count)
		return InvalidParameter;
	
	*count = brush->boundary->count;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientBlendCount (GpPathGradient *brush, INT *count)
{
	if (!brush || !count)
		return InvalidParameter;
	
	*count = brush->blend->count;
	
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientBlend (GpPathGradient *brush, REAL *blend, REAL *positions, INT count)
{
	if (!brush || !blend || !positions || count <= 0)
		return InvalidParameter;

	if (count < brush->blend->count)
		return InsufficientBuffer;

	memcpy (blend, brush->blend->factors, brush->blend->count * sizeof (float));
	memcpy (positions, brush->blend->positions, brush->blend->count * sizeof (float));

	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientBlend (GpPathGradient *brush, GDIPCONST REAL *blend, GDIPCONST REAL *positions, INT count)
{
	float *blendFactors;
	float *blendPositions;
	int index;

	if (!brush || !blend || !positions || count <= 0)
		return InvalidParameter;

	if (count >= 2 && (positions[0] != 0.0f || positions[count - 1] != 1.0f))
		return InvalidParameter;

	if (brush->blend->count != count) {
		blendFactors = (float *) GdipAlloc (count * sizeof (float));
		if (!blendFactors)
			return OutOfMemory;

		blendPositions = (float *) GdipAlloc (count * sizeof (float));
		if (!blendPositions) {
			GdipFree (blendFactors);
			return OutOfMemory;
		}

		/* free the existing values */
		if (brush->blend->count != 0) {
			GdipFree (brush->blend->factors);
			GdipFree (brush->blend->positions);
		}

		brush->blend->factors = blendFactors;
		brush->blend->positions = blendPositions;
	}

	for (index = 0; index < count; index++) {
		brush->blend->factors [index] = blend [index];
		brush->blend->positions [index] = positions [index];
	}

	brush->blend->count = count;

	/* we clear the preset colors when setting the blend */
	if (brush->presetColors->count != 0) {
		GdipFree (brush->presetColors->colors);
		GdipFree (brush->presetColors->positions);
		brush->presetColors->count = 0;
		brush->presetColors->colors = NULL;
		brush->presetColors->positions = NULL;
	}

	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientPresetBlendCount (GpPathGradient *brush, INT *count)
{
	if (!brush || !count)
		return InvalidParameter;

	*count = brush->presetColors->count;

	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientPresetBlend (GpPathGradient *brush, ARGB *blend, REAL *positions, INT count)
{
	if (!brush || !blend)
		return InvalidParameter;
	
	if (count < 0)
		return OutOfMemory;

	if (!positions || count < 2)
		return InvalidParameter;

	if (brush->presetColors->count == 0)
		return GenericError;
	
	if (brush->presetColors->count != count)
		return InvalidParameter;

	memcpy (blend, brush->presetColors->colors, count * sizeof (ARGB));
	memcpy (positions, brush->presetColors->positions, count * sizeof (float));

	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientPresetBlend (GpPathGradient *brush, GDIPCONST ARGB *blend, GDIPCONST REAL *positions, INT count)
{
	ARGB *blendColors;
	float *blendPositions;
	int index;

	if (!brush || !blend || !positions || count < 2 || positions[0] != 0.0f || positions[count - 1] != 1.0f)
		return InvalidParameter;

	if (brush->presetColors->count != count) {
		blendColors = (ARGB *) GdipAlloc (count * sizeof (ARGB));
		if (!blendColors)
			return OutOfMemory;

		blendPositions = (float *) GdipAlloc (count * sizeof (float));
		if (!blendPositions) {
			GdipFree (blendColors);
			return OutOfMemory;
		}

		/* free the existing values */
		if (brush->presetColors->count != 0) {
			GdipFree (brush->presetColors->colors);
			GdipFree (brush->presetColors->positions);
		}

		brush->presetColors->colors = blendColors;
		brush->presetColors->positions = blendPositions;
	}

	for (index = 0; index < count; index++) {
		brush->presetColors->colors [index] = blend [index];
		brush->presetColors->positions [index] = positions [index];
	}

	brush->presetColors->count = count;

	/* we clear the blend when setting preset colors */
	if (brush->blend->count != 0) {
		GdipFree (brush->blend->factors);
		GdipFree (brush->blend->positions);
		brush->blend->count = 0;
	}

	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientSigmaBlend (GpPathGradient *brush, REAL focus, REAL scale)
{
	float *blends, *positions, *presetPositions;
	ARGB *presetColors;
	float pos = 0.0;
	int count = 511; /* total no of samples */
	int index;
	float sigma;
	float mean;
	float fall_off_len = 2.0; /* curve fall off length in terms of SIGMA */
	float delta; /* distance between two samples */

	/* we get a curve not starting from 0 and not ending at 1.
	 * so we subtract the starting value and divide by the curve
	 * height to make it fit in the 0 to scale range
	 */
	float curve_bottom;
	float curve_top;
	float curve_height;

	if (!brush || focus < 0 || focus > 1 || scale < 0 || scale > 1)
		return InvalidParameter;

	if (focus == 0 || focus == 1) {
		count = 256;
	}

	if (brush->blend->count != count) {
		blends = (float *) GdipAlloc (count * sizeof (float));
		if (!blends)
			return OutOfMemory;

		positions = (float *) GdipAlloc (count * sizeof (float));
		if (!positions) {
			GdipFree (blends);
			return OutOfMemory;
		}

		/* free the existing values */
		if (brush->blend->count != 0) {
			GdipFree (brush->blend->factors);
			GdipFree (brush->blend->positions);
		}

		brush->blend->factors = blends;
		brush->blend->positions = positions;
	}

	/* we clear the preset colors when setting the blend */
	if (brush->presetColors->count != 1) {
		presetColors = (ARGB *) GdipAlloc (sizeof (ARGB));
		if (!presetColors)
			return OutOfMemory;

		presetPositions = (float *) GdipAlloc (sizeof (float));
		if (!presetPositions) {
			GdipFree (presetColors);
			return OutOfMemory;
		}

		GdipFree (brush->presetColors->colors);
		GdipFree (brush->presetColors->positions);
		brush->presetColors->count = 1;
		brush->presetColors->colors = presetColors;
		brush->presetColors->positions = presetPositions;
	}
	brush->presetColors->colors [0] = MAKE_ARGB_ARGB(0,0,0,0);
	brush->presetColors->positions[0] = 0.0;

	/* Set the blend colors. We use integral of the Normal Distribution,
	 * i.e. Cumulative Distribution Function (CFD).
	 *
	 * Normal distribution:
	 *
	 * y (x) = (1 / sqrt (2 * PI * sq (sigma))) * exp (-sq (x - mu)/ (2 * sq (sigma)))
	 *
	 * where, y = height of normal curve, 
	 *        sigma = standard deviation
	 *        mu = mean
	 * OR
	 * y (x) = peak * exp ( - z * z / 2)
	 * where, z = (x - mu) / sigma
	 *
	 * In this curve, peak would occur at mean i.e. for x = mu. This results in
	 * a peak value of peak = (1 / sqrt (2 * PI * sq (sigma))).
	 *
	 * Cumulative distribution function:
	 * Ref: http://mathworld.wolfram.com/NormalDistribution.html
	 *
	 * D (x) = (1 / 2) [1 + erf (z)]
	 * where, z = (x - mu) / (sigma * sqrt (2))
	 *
	 */
	if (focus == 0) {
		/* right part of the curve with a complete fall in fall_off_len * SIGMAs */
		sigma = 1.0 / fall_off_len;
		mean = 0.5;
		delta = 1.0 / 255.0;

		curve_bottom = 0.5 * (1.0 - gdip_erf (1.0, sigma, mean));
		curve_top = 0.5 * (1.0 - gdip_erf (focus, sigma, mean));
		curve_height = curve_top - curve_bottom;

		/* set the start */
		brush->blend->positions [0] = focus;
		brush->blend->factors [0] = scale;

		for (index = 1, pos = delta; index < 255; index++, pos += delta) {
			brush->blend->positions [index] = pos;
			brush->blend->factors [index] = (scale / curve_height) * 
				(0.5 * (1.0 - gdip_erf (pos, sigma, mean)) - curve_bottom);
		}

		/* set the end */
		brush->blend->positions [count - 1] = 1.0;
		brush->blend->factors [count - 1] = 0.0;
	}

	else if (focus == 1) {
		/* left part of the curve with a complete rise in fall_off_len * SIGMAs */
		sigma = 1.0 / fall_off_len;
		mean = 0.5;
		delta = 1.0 / 255.0;

		curve_bottom = 0.5 * (1.0 + gdip_erf (0.0, sigma, mean));
		curve_top = 0.5 * (1.0 + gdip_erf (focus, sigma, mean));
		curve_height = curve_top - curve_bottom;

		/* set the start */
		brush->blend->positions [0] = 0.0;
		brush->blend->factors [0] = 0.0;

		for (index = 1, pos = delta; index < 255; index++, pos += delta) {
			brush->blend->positions [index] = pos;
			brush->blend->factors [index] = (scale / curve_height) * 
				(0.5 * (1.0 + gdip_erf (pos, sigma, mean)) - curve_bottom);
		}

		/* set the end */
		brush->blend->positions [count - 1] = focus;
		brush->blend->factors [count - 1] = scale;
	}

	else {
		/* left part of the curve with a complete fall in fall_off_len * SIGMAs */
		sigma = focus / (2 * fall_off_len);
		mean = focus / 2.0;
		delta = focus / 255.0;

		/* set the start */
		brush->blend->positions [0] = 0.0;
		brush->blend->factors [0] = 0.0;

		curve_bottom = 0.5 * (1.0 + gdip_erf (0.0, sigma, mean));
		curve_top = 0.5 * (1.0 + gdip_erf (focus, sigma, mean));
		curve_height = curve_top - curve_bottom;

		for (index = 1, pos = delta; index < 255; index++, pos += delta) {
			brush->blend->positions [index] = pos;
			brush->blend->factors [index] = (scale / curve_height) * 
				(0.5 * (1.0 + gdip_erf (pos, sigma, mean)) - curve_bottom);
		}

		brush->blend->positions [index] = focus;
		brush->blend->factors [index] = scale;

		/* right part of the curve with a complete fall in fall_off_len * SIGMAs */
		sigma = (1.0 - focus) / (2 * fall_off_len);
		mean = (1.0 + focus) / 2.0;
		delta = (1.0 - focus) / 255.0;

		curve_bottom = 0.5 * (1.0 - gdip_erf (1.0, sigma, mean));
		curve_top = 0.5 * (1.0 - gdip_erf (focus, sigma, mean));
		curve_height = curve_top - curve_bottom;

		index ++;
		pos = focus + delta;

		for (; index < 510; index++, pos += delta) {
			brush->blend->positions [index] = pos;
			brush->blend->factors [index] = (scale / curve_height) * 
				(0.5 * (1.0 - gdip_erf (pos, sigma, mean)) - curve_bottom);
		}

		/* set the end */
		brush->blend->positions [count - 1] = 1.0;
		brush->blend->factors [count - 1] = 0.0;
	}

	brush->blend->count = count;
	brush->base.changed = TRUE;

	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientLinearBlend (GpPathGradient *brush, REAL focus, REAL scale)
{
	float *blends, *positions, *presetPositions;
	ARGB *presetColors;
	int count = 3;

	if (!brush || focus < 0 || focus > 1 || scale < 0 || scale > 1)
		return InvalidParameter;

	if (focus == 0 || focus == 1) {
		count = 2;
	}

	if (brush->blend->count != count) {
		blends = (float *) GdipAlloc (count * sizeof (float));
		if (!blends)
			return OutOfMemory;

		positions = (float *) GdipAlloc (count * sizeof (float));
		if (!positions) {
			GdipFree (blends);
			return OutOfMemory;
		}

		/* free the existing values */
		if (brush->blend->count != 0) {
			GdipFree (brush->blend->factors);
			GdipFree (brush->blend->positions);
		}

		brush->blend->factors = blends;
		brush->blend->positions = positions;
	}

	/* we clear the preset colors when setting the blend */
	if (brush->presetColors->count != 1) {
		presetColors = (ARGB *) GdipAlloc (sizeof (ARGB));
		if (!presetColors)
			return OutOfMemory;

		presetPositions = (float *) GdipAlloc (sizeof (float));
		if (!presetPositions) {
			GdipFree (presetColors);
			return OutOfMemory;
		}

		GdipFree (brush->presetColors->colors);
		GdipFree (brush->presetColors->positions);
		brush->presetColors->count = 1;
		brush->presetColors->colors = presetColors;
		brush->presetColors->positions = presetPositions;
	}
	brush->presetColors->colors [0] = MAKE_ARGB_ARGB(0,0,0,0);
	brush->presetColors->positions[0] = 0.0;

	/* set the blend colors */
	if (focus == 0) {
		brush->blend->positions [0] = focus;
		brush->blend->factors [0] = scale;
		brush->blend->positions [1] = 1;
		brush->blend->factors [1] = 0;
	}

	else if (focus == 1) {
		brush->blend->positions [0] = 0;
		brush->blend->factors [0] = 0;
		brush->blend->positions [1] = focus;
		brush->blend->factors [1] = scale;
	}

	else {
		brush->blend->positions [0] = 0;
		brush->blend->factors [0] = 0;
		brush->blend->positions [1] = focus;
		brush->blend->factors [1] = scale;
		brush->blend->positions [2] = 1;
		brush->blend->factors [2] = 0;
	}

	brush->blend->count = count;
	brush->base.changed = TRUE;

	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientWrapMode (GpPathGradient *brush, GpWrapMode *wrapMode)
{
	if (!brush || !wrapMode)
		return InvalidParameter;

	*wrapMode = brush->wrapMode;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientWrapMode (GpPathGradient *brush, GpWrapMode wrapMode)
{
	if (!brush)
		return InvalidParameter;

	if (wrapMode < WrapModeTile || wrapMode > WrapModeClamp)
		return Ok;

	brush->wrapMode = wrapMode;
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipGetPathGradientTransform (GpPathGradient *brush, GpMatrix *matrix)
{
	if (!brush || !matrix)
		return InvalidParameter;

	/* If presetcolors are set, we are not in a proper state 
	 * to return transform property.
	 */
	if (brush->presetColors->count >= 2)
		return WrongState;

	gdip_cairo_matrix_copy (matrix, &brush->transform);
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientTransform (GpPathGradient *brush, GpMatrix *matrix)
{
	GpStatus status;
	BOOL invertible;

	if (!brush || !matrix)
		return InvalidParameter;

	/* the matrix MUST be invertible to be used */
	status = GdipIsMatrixInvertible ((GpMatrix*) matrix, &invertible);
	if (!invertible || (status != Ok))
		return InvalidParameter;

	gdip_cairo_matrix_copy (&brush->transform, matrix);
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipResetPathGradientTransform (GpPathGradient *brush)
{
	if (!brush)
		return InvalidParameter;

	cairo_matrix_init_identity (&brush->transform);
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipMultiplyPathGradientTransform (GpPathGradient *brush, GDIPCONST GpMatrix *matrix, GpMatrixOrder order)
{
	GpStatus status;
	BOOL invertible;
	cairo_matrix_t mat;

	if (!brush)
		return InvalidParameter;

	if (!matrix)
		return Ok;

	/* the matrix MUST be invertible to be used */
	status = GdipIsMatrixInvertible ((GpMatrix*) matrix, &invertible);
	if (!invertible || (status != Ok))
		return InvalidParameter;

	if (order == MatrixOrderPrepend)
		cairo_matrix_multiply (&mat, matrix, &brush->transform);
	else
		cairo_matrix_multiply (&mat, &brush->transform, matrix);

	gdip_cairo_matrix_copy (&brush->transform, &mat);
	brush->base.changed = TRUE;
	return Ok;
}

GpStatus WINGDIPAPI
GdipTranslatePathGradientTransform (GpPathGradient *brush, REAL dx, REAL dy, GpMatrixOrder order)
{
	GpStatus status;

	if (!brush)
		return InvalidParameter;

	if ((status = GdipTranslateMatrix (&brush->transform, dx, dy, order)) == Ok)
		brush->base.changed = TRUE;
	return status;
}

GpStatus WINGDIPAPI
GdipScalePathGradientTransform (GpPathGradient *brush, REAL sx, REAL sy, GpMatrixOrder order)
{
	GpStatus status;

	if (!brush)
		return InvalidParameter;

	if ((status = GdipScaleMatrix (&brush->transform, sx, sy, order)) == Ok)
		brush->base.changed = TRUE;
	return status;
}

GpStatus WINGDIPAPI
GdipRotatePathGradientTransform (GpPathGradient *brush, REAL angle, GpMatrixOrder order)
{
	GpStatus status;

	if (!brush)
		return InvalidParameter;

	if ((status = GdipRotateMatrix (&brush->transform, angle, order)) == Ok)
		brush->base.changed = TRUE;
	return status;
}

GpStatus WINGDIPAPI
GdipGetPathGradientFocusScales (GpPathGradient *brush, REAL *xScale, REAL *yScale)
{
	if (!brush || !xScale || !yScale)
		return InvalidParameter;

	*xScale = brush->focusScales.X;
	*yScale = brush->focusScales.Y;
	return Ok;
}

GpStatus WINGDIPAPI
GdipSetPathGradientFocusScales (GpPathGradient *brush, REAL xScale, REAL yScale)
{
	if (!brush)
		return InvalidParameter;

	brush->focusScales.X = xScale;
	brush->focusScales.Y = yScale;
	brush->base.changed = TRUE;
	return Ok;
}
