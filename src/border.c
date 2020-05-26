#include "border.h"

extern struct window_manager g_window_manager;
extern int g_connection;

static void border_window_ensure_same_space(struct window *window)
{
    int w_space_count;
    uint64_t *w_space_list = window_space_list(window, &w_space_count);
    if (!w_space_list) return;

    uint64_t w_sid = *w_space_list;
    free(w_space_list);

    if (w_space_count > 1) {
        uint32_t tags[2] = { kCGSStickyTagBit };
        SLSSetWindowTags(g_connection, window->border.id, tags, 32);
        return;
    } else {
        uint32_t tags[2] = { kCGSStickyTagBit };
        SLSClearWindowTags(g_connection, window->border.id, tags, 32);
    }

    bool found = false;
    uint64_t b_sid = 0;

    CFArrayRef border_list_ref = cfarray_of_cfnumbers(&window->border.id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    CFArrayRef border_space_list_ref = SLSCopySpacesForWindows(g_connection, 0x7, border_list_ref);

    for (int i = 0; i < CFArrayGetCount(border_space_list_ref); ++i) {
        CFNumberRef id_ref = CFArrayGetValueAtIndex(border_space_list_ref, i);
        CFNumberGetValue(id_ref, CFNumberGetType(id_ref), &b_sid);

        if (b_sid == w_sid) {
            found = true;
            break;
        }
    }

    if (!found) {
        SLSMoveWindowsToManagedSpace(g_connection, border_list_ref, w_sid);
    }

    CFRelease(border_list_ref);
    CFRelease(border_space_list_ref);
}

static CGMutablePathRef border_normal_shape(CGRect frame, float radius)
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathAddRoundedRect(path, NULL, frame, radius, radius);
    return path;
}

static inline float border_radius_clamp(CGRect frame, float radius, int width)
{
    if (fabs(radius) < 0.01f) {
      radius = 0.0f;
    } else if (radius == -1.0f) {
      radius = 2.0f * width;
    }

    if (radius * 2 > CGRectGetWidth(frame)) {
        radius = CGRectGetWidth(frame) / 2;
    }

    if (radius * 2 > CGRectGetHeight(frame)) {
        radius = CGRectGetHeight(frame) / 2;
    }

    return radius;
}

void border_window_refresh(struct window *window)
{
    if (!window->border.id) return;
    if (!window->border.enabled) return;
    struct border *border = &window->border;
    border_window_ensure_same_space(window);

    CFTypeRef region_ref;
    CGRect border_frame;

    CGRect region = window_ax_frame(window);
    region.origin.x -= border->width;
    region.origin.y -= border->width;
    region.size.width  += (2*border->width);
    region.size.height += (2*border->width);
    CGSNewRegionWithRect(&region, &region_ref);

#if 0
    if (g_window_manager.window_border_placement == BORDER_PLACEMENT_EXTERIOR) {
        border_frame = (CGRect) { { 0.5f*border->width, 0.5f*border->width }, { region.size.width - border->width, region.size.height - border->width} };
    } else if (g_window_manager.window_border_placement == BORDER_PLACEMENT_INTERIOR) {
        border_frame = (CGRect) { { 1.5f*border->width, 1.5f*border->width }, { region.size.width - 3*border->width, region.size.height - 3*border->width } };
    } else {
        border_frame = (CGRect) { { border->width, border->width }, { region.size.width - 2*border->width, region.size.height - 2*border->width } };
    }
#else
    border_frame = (CGRect) { { border->width, border->width }, { region.size.width - 2*border->width, region.size.height - 2*border->width } };
#endif

    float radius = border_radius_clamp(border_frame, border->radius, border->width);
    CGMutablePathRef path = border_normal_shape(border_frame, radius);
    CGRect clear_region = { { 0, 0 }, { region.size.width, region.size.height } };

    SLSDisableUpdate(g_connection);
    SLSOrderWindow(g_connection, border->id, 0, window->id);
    SLSSetWindowShape(g_connection, border->id, 0.0f, 0.0f, region_ref);
    CGContextClearRect(border->context, clear_region);

    CGContextAddPath(border->context, path);
    CGContextStrokePath(border->context);

    CGContextFlush(border->context);
    SLSOrderWindow(g_connection, border->id, 1, window->id);
    SLSReenableUpdate(g_connection);

    CFRelease(region_ref);
    CGPathRelease(path);
}

void border_window_activate(struct window *window)
{
    if (!window->border.id) return;
    if (!window->border.enabled) return;

    struct border *border = &window->border;
    border->color = rgba_color_from_hex(g_window_manager.active_window_border_color);
    CGContextSetRGBStrokeColor(border->context, border->color.r, border->color.g, border->color.b, border->color.a);
    int level = CGWindowLevelForKey(17);
    SLSSetWindowLevel(g_connection, window->border.id, level);

    if (window_is_fullscreen(window)) {
        border_window_hide(window);
    } else {
        border_window_refresh(window);
    }
}

void border_window_deactivate(struct window *window)
{
    if (!window->border.id) return;
    if (!window->border.enabled) return;

    struct border *border = &window->border;
    border->color = rgba_color_from_hex(g_window_manager.normal_window_border_color);
    CGContextSetRGBStrokeColor(border->context, border->color.r, border->color.g, border->color.b, border->color.a);
    SLSSetWindowLevel(g_connection, window->border.id, window_level(window));

    if (window_is_fullscreen(window)) {
        border_window_hide(window);
    } else {
        border_window_refresh(window);
    }
}

void border_window_show(struct window *window)
{
    if (!window->border.id) return;
    if (!window->border.enabled) return;
    SLSOrderWindow(g_connection, window->border.id, 1, window->id);
}

void border_window_hide(struct window *window)
{
    if (!window->border.id) return;
    if (!window->border.enabled) return;
    SLSOrderWindow(g_connection, window->border.id, 0, window->id);
}

void border_window_create(struct window *window)
{
    struct border *border = &window->border;
    if (border->id) return;

    border->color = rgba_color_from_hex(g_window_manager.normal_window_border_color);
    border->width = g_window_manager.window_border_width;
    border->radius = g_window_manager.window_border_radius;
    border->enabled = true;

    CFTypeRef frame_region;
    CGRect frame = window_frame(window);
    CGSNewRegionWithRect(&frame, &frame_region);

    uint32_t tags[2] = {
        kCGSModalWindowTagBit |
        kCGSDisableShadowTagBit |
        kCGSHighQualityResamplingTagBit |
        kCGSIgnoreForExposeTagBit
    };

    SLSNewWindow(g_connection, 2, 0.0f, 0.0f, frame_region, &border->id);
    SLSSetWindowResolution(g_connection, border->id, 2.0f);
    SLSSetWindowTags(g_connection, border->id, tags, 64);
    SLSSetWindowOpacity(g_connection, border->id, 0);
    SLSSetMouseEventEnableFlags(g_connection, border->id, false);
    SLSSetWindowLevel(g_connection, border->id, window_level(window));
    border->context = SLWindowContextCreate(g_connection, border->id, 0);
    CGContextSetAllowsAntialiasing(border->context, true);
    CGContextSetShouldAntialias(border->context, true);
    CGContextSetLineWidth(border->context, border->width);
    CGContextSetRGBStrokeColor(border->context, border->color.r, border->color.g, border->color.b, border->color.a);
    CFRelease(frame_region);
}

void border_window_destroy(struct window *window)
{
    if (window->border.id) {
        CGContextRelease(window->border.context);
        SLSReleaseWindow(g_connection, window->border.id);
        memset(&window->border, 0, sizeof(struct border));
    }
}
