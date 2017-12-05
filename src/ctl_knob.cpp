/* Calf DSP Library
 * Knob control.
 * Copyright (C) 2007-2010 Krzysztof Foltman, Torben Hohn, Markus Schmidt
 * and others
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */
#include "config.h"
#include <calf/ctl_knob.h>
#include <calf/drawingutils.h>
#include <gdk/gdkkeysyms.h>
#include <cairo/cairo.h>
#if !defined(__APPLE__)
#include <malloc.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include <algorithm>
#include <stdlib.h>

#define range01(tick) std::min(1., std::max(0., tick))

///////////////////////////////////////// knob ///////////////////////////////////////////////

static float
calf_knob_get_color (CalfKnob *self, float deg, float phase, float start, float last, float tickw)
{
    double on  = 1.0;
    double off = 0.22;
    //printf ("get color: phase %.2f deg %.2f\n", phase, deg);
    if (self->type == 0) {
        // normal
        if (deg > phase or phase == start)
            return off;
        else return on;
    }
    if (self->type == 1) {
        // centered
        if (deg > 270 and deg <= phase and phase > 270)
            return on;
        if (deg <= 270 and deg > phase and phase < 270)
            return on;
        if ((deg == start and phase == start)
        or  (deg == 270.  and phase > 270.))
            return on;
        return off;
    }
    if (self->type == 2) {
        // reverse
        if (deg > phase or phase == start)
            return on;
        else return off;
    }
    if (self->type == 3) {
        for (unsigned j = 0; j < self->ticks.size(); j++) {
            float tp = fmod((start + range01(self->ticks[j]) * 360.) - phase + 360, 360);
            if (tp > 360 - tickw or tp < tickw) {
                return on;
            }
        }
        if (deg > phase and deg > last + tickw and last < phase)
            return on;
        
    }
    return off;
        
        
        
}

static gboolean
calf_knob_expose (GtkWidget *widget, GdkEventExpose *event)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);
    CalfKnobClass *cls = CALF_KNOB_CLASS(GTK_OBJECT_GET_CLASS(widget));
    GdkPixbuf *pixbuf = cls->knob_image[self->size - 1];
    gint iw = gdk_pixbuf_get_width(pixbuf);
    gint ih = gdk_pixbuf_get_height(pixbuf);
    
    float widths[6]  = {0, 2.2, 3.5, 3.5, 4.2, 5.5};
    float margins[6] = {0, 2.2, 3.5, 3.8, 4.2, 4.5};
    float pins_m[6]  = {0, 6,   10,   10,   11,  13};
    float pins_s[6]  = {0, 4,   4,   4,   4,   4};
    
    GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(widget));
    cairo_t *ctx = gdk_cairo_create(GDK_DRAWABLE(widget->window));
    
    float r, g, b;
    get_fg_color(widget, NULL, &r, &g, &b);
    
    double ox   = widget->allocation.x + (widget->allocation.width - iw) / 2;
    double oy   = widget->allocation.y + (widget->allocation.height - ih) / 2;
    double size = iw;
    float  rad  = size / 2;
    double xc   = ox + rad;
    double yc   = oy + rad;
    
    unsigned int tick;
    double phase;
    double base;
    double deg;
    double end;
    double last;
    double start;
    double nend; 
    double zero;
    double opac;
    
    double lwidth = widths[self->size];
    double lmarg  = margins[self->size];
    double perim  = (rad - lmarg) * 2 * M_PI;
    double tickw  = 2. / perim * 360.;
    double tickw2 = tickw / 2.;
    
    const unsigned int debug = 0;
    
    cairo_rectangle(ctx, ox, oy, size + size / 2, size + size / 2);
    cairo_clip(ctx);
    
    // draw background
    gdk_draw_pixbuf(GDK_DRAWABLE(widget->window), widget->style->fg_gc[0], pixbuf,
                    0, 0, ox, oy, iw, ih, GDK_RGB_DITHER_NORMAL, 0, 0);
    
    switch (self->type) {
        default:
        case 0:
            // normal knob
            start = 135.;
            end   = 405.;
            base  = 270.;
            zero  = 135.;
        case 1:
            // centered @ 270°
            start = 135.;
            end   = 405.;
            base  = 270.;
            zero  = 270.;
        case 2:
            // reversed
            start = 135.;
            end   = 405.;
            base  = 270.;
            zero  = 135.;
            break;
        case 3:
            // 360°
            start = -90.;
            end   = 270.;
            base  = 360.;
            zero  = -90.;
            break;
    }
    tick  = 0;
    nend  = 0.;
    deg = last = start;
    phase = (adj->value - adj->lower) * base / (adj->upper - adj->lower) + start;
    
    // draw pin
    float x1 = ox + rad + (rad - pins_m[self->size]) * cos(phase * (M_PI / 180.));
    float y1 = oy + rad + (rad - pins_m[self->size]) * sin(phase * (M_PI / 180.));
    float x2 = ox + rad + (rad - pins_s[self->size] - pins_m[self->size]) * cos(phase * (M_PI / 180.));
    float y2 = oy + rad + (rad - pins_s[self->size] - pins_m[self->size]) * sin(phase * (M_PI / 180.));
    cairo_move_to(ctx, x1, y1);
    cairo_line_to(ctx, x2, y2);
    cairo_set_source_rgba(ctx, r, g, b, 0.99);
    cairo_set_line_width(ctx, lwidth / 2.);
    cairo_stroke(ctx);
    
    cairo_set_line_width(ctx, lwidth);
    
    // draw ticks and rings
    unsigned int evsize = 4;
    double events[4] = { start, zero, end, phase };
    if (self->type == 3)
        evsize = 3;
    std::sort(events, events + evsize);
    if (debug) {
        printf("start %.2f end %.2f last %.2f deg %.2f tick %d ticks %d phase %.2f base %.2f nend %.2f\n", start, end, last, deg, tick, int(self->ticks.size()), phase, base, nend);
        for (unsigned int i = 0; i < self->ticks.size(); i++) {
            printf("tick %d %.2f\n", i, self->ticks[i]);
        }
    }
    while (deg <= end) {
        if (debug) printf("tick %d deg %.2f last %.2f end %.2f\n", tick, deg, last, end);
        if (self->ticks.size() and deg == start + range01(self->ticks[tick]) * base) {
            // seems we want to draw a tick on this angle.
            // so we have to fill the void between the last set angle
            // and the point directly before the tick first.
            // (draw from last known angle to tickw2 + tickw before actual deg)
            if (last < deg - tickw - tickw2) {
                opac = calf_knob_get_color(self, (deg - tickw - tickw2), phase, start, last, tickw + tickw2);
                cairo_set_source_rgba(ctx, r, g, b, opac);
                cairo_arc(ctx, xc, yc, rad - lmarg, last * (M_PI / 180.), std::max(last, std::min(nend, (deg - tickw - tickw2))) * (M_PI / 180.));
                cairo_stroke(ctx);
                if (debug) printf("fill from %.2f to %.2f @ %.2f\n", last, (deg - tickw - tickw2), opac);
            }
            // draw the tick itself
            opac = calf_knob_get_color(self, deg, phase, start, end, tickw + tickw2);
            cairo_set_source_rgba(ctx, r, g, b, opac);
            cairo_arc(ctx, xc, yc, rad - lmarg, (deg - tickw2) * (M_PI / 180.), (deg + tickw2) * (M_PI / 180.));
            cairo_stroke(ctx);
            if (debug) printf("tick from %.2f to %.2f @ %.2f\n", (deg - tickw2), (deg + tickw2), opac);
            // set last known angle to deg plus tickw + tickw2
            last = deg + tickw + tickw2;
            // and count up tick
            tick ++;
            // remember the next ticks void end
            if (tick < self->ticks.size())
                nend = range01(self->ticks[tick]) * base + start - tickw - tickw2;
            else
                nend = end;
        } else {
            // seems we want to fill a gap between the last event and
            // the actual one, while the actual one isn't a tick (but a
            // knobs position or a center)
            if ((last < deg)) {
                opac = calf_knob_get_color(self, deg, phase, start, last, tickw + tickw2);
                cairo_set_source_rgba(ctx, r, g, b, opac);
                cairo_arc(ctx, xc, yc, rad - lmarg, last * (M_PI / 180.), std::min(nend, std::max(last, deg)) * (M_PI / 180.));
                cairo_stroke(ctx);
                if (debug) printf("void from %.2f to %.2f @ %.2f\n", last, std::min(nend, std::max(last, deg)), opac);
            }
            last = deg;
        }
        if (deg >= end)
            break;
        // set deg to next event
        for (unsigned int i = 0; i < evsize; i++) {
            if (debug > 1) printf("checking %.2f (start %.2f zero %.2f phase %.2f end %.2f)\n", events[i], start, zero, phase, end);
            if (events[i] > deg) {
                deg = events[i];
                if (debug > 1) printf("taken.\n");
                break;
            }
        }
        if (tick < self->ticks.size()) {
            deg = std::min(deg, start + range01(self->ticks[tick]) * base);
            if (debug > 1) printf("checking tick %d %.2f\n", tick, start + range01(self->ticks[tick]) * base);
        }
        //deg = std::max(last, deg);
        if (debug > 1) printf("finally! deg %.2f\n", deg);
    }
    if (debug) printf("\n");
    cairo_destroy(ctx);
    return TRUE;
}

static void
calf_knob_size_request (GtkWidget *widget,
                           GtkRequisition *requisition)
{
    g_assert(CALF_IS_KNOB(widget));

    CalfKnob *self = CALF_KNOB(widget);

    CalfKnobClass * cls = CALF_KNOB_CLASS(GTK_OBJECT_GET_CLASS(widget));
    requisition->width  = gdk_pixbuf_get_width(cls->knob_image[self->size - 1]);
    requisition->height = gdk_pixbuf_get_height(cls->knob_image[self->size - 1]);
}

static gboolean calf_knob_enter (GtkWidget *widget, GdkEventCrossing* ev)
{
    if (gtk_widget_get_state(widget) == GTK_STATE_NORMAL) {
        gtk_widget_set_state(widget, GTK_STATE_PRELIGHT);
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean calf_knob_leave (GtkWidget *widget, GdkEventCrossing *ev)
{
    if (gtk_widget_get_state(widget) == GTK_STATE_PRELIGHT) {
        gtk_widget_set_state(widget, GTK_STATE_NORMAL);
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static void
calf_knob_incr (GtkWidget *widget, int dir_down)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);
    GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(widget));

    int oldstep = (int)(0.5f + (adj->value - adj->lower) / adj->step_increment);
    int step;
    int nsteps = (int)(0.5f + (adj->upper - adj->lower) / adj->step_increment); // less 1 actually
    if (dir_down)
        step = oldstep - 1;
    else
        step = oldstep + 1;
    if (self->type == 3 && step >= nsteps)
        step %= nsteps;
    if (self->type == 3 && step < 0)
        step = nsteps - (nsteps - step) % nsteps;

    // trying to reduce error cumulation here, by counting from lowest or from highest
    float value = adj->lower + step * double(adj->upper - adj->lower) / nsteps;
    gtk_range_set_value(GTK_RANGE(widget), value);
    // printf("step %d:%d nsteps %d value %f:%f\n", oldstep, step, nsteps, oldvalue, value);
}

static gboolean
calf_knob_key_press (GtkWidget *widget, GdkEventKey *event)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);
    GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(widget));
    gtk_widget_set_state(widget, GTK_STATE_ACTIVE);
    gtk_widget_queue_draw(widget);
    switch(event->keyval)
    {
        case GDK_Home:
            gtk_range_set_value(GTK_RANGE(widget), adj->lower);
            return TRUE;

        case GDK_End:
            gtk_range_set_value(GTK_RANGE(widget), adj->upper);
            return TRUE;

        case GDK_Up:
            calf_knob_incr(widget, 0);
            return TRUE;

        case GDK_Down:
            calf_knob_incr(widget, 1);
            return TRUE;
            
        case GDK_Shift_L:
        case GDK_Shift_R:
            self->start_value = gtk_range_get_value(GTK_RANGE(widget));
            self->start_y = self->last_y;
            return TRUE;
    }

    return FALSE;
}

static gboolean
calf_knob_key_release (GtkWidget *widget, GdkEventKey *event)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);

    if(event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R)
    {
        self->start_value = gtk_range_get_value(GTK_RANGE(widget));
        self->start_y = self->last_y;
        return TRUE;
    }
    gtk_widget_set_state(widget, GTK_STATE_NORMAL);
    gtk_widget_queue_draw(widget);
    return FALSE;
}

static gboolean
calf_knob_button_press (GtkWidget *widget, GdkEventButton *event)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);

    if (event->type == GDK_2BUTTON_PRESS) {
        gtk_range_set_value(GTK_RANGE(widget), self->default_value);
    }

    // CalfKnob *lg = CALF_KNOB(widget);
    gtk_widget_grab_focus(widget);
    gtk_grab_add(widget);
    self->start_x = event->x;
    self->last_y = self->start_y = event->y;
    self->start_value = gtk_range_get_value(GTK_RANGE(widget));
    gtk_widget_set_state(widget, GTK_STATE_ACTIVE);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean
calf_knob_button_release (GtkWidget *widget, GdkEventButton *event)
{
    g_assert(CALF_IS_KNOB(widget));

    if (GTK_WIDGET_HAS_GRAB(widget))
        gtk_grab_remove(widget);
    gtk_widget_set_state(widget, GTK_STATE_NORMAL);
    gtk_widget_queue_draw(widget);
    return FALSE;
}

static inline float endless(float value)
{
    if (value >= 0)
        return fmod(value, 1.f);
    else
        return fmod(1.f - fmod(1.f - value, 1.f), 1.f);
}

static inline float deadzone(GtkWidget *widget, float value, float incr)
{
    // map to dead zone
    float ov = value;
    if (ov > 0.5)
        ov = 0.1 + ov;
    if (ov < 0.5)
        ov = ov - 0.1;
    
    float nv = ov + incr;
    
    if (nv > 0.6)
        return nv - 0.1;
    if (nv < 0.4)
        return nv + 0.1;
    return 0.5;
}

static gboolean
calf_knob_pointer_motion (GtkWidget *widget, GdkEventMotion *event)
{
    g_assert(CALF_IS_KNOB(widget));
    CalfKnob *self = CALF_KNOB(widget);

    float scale = (event->state & GDK_SHIFT_MASK) ? 2500 : 250;
    gboolean moved = FALSE;
    
    if (GTK_WIDGET_HAS_GRAB(widget)) 
    {
        if (self->type == 3)
        {
            gtk_range_set_value(GTK_RANGE(widget), endless(self->start_value - (event->y - self->start_y) / scale));
        }
        else
        if (self->type == 1)
        {
            gtk_range_set_value(GTK_RANGE(widget), deadzone(GTK_WIDGET(widget), self->start_value, -(event->y - self->start_y) / scale));
        }
        else
        {
            gtk_range_set_value(GTK_RANGE(widget), self->start_value - (event->y - self->start_y) / scale);
        }
        moved = TRUE;
    }
    self->last_y = event->y;
    return moved;
}

static gboolean
calf_knob_scroll (GtkWidget *widget, GdkEventScroll *event)
{
    calf_knob_incr(widget, event->direction);
    return TRUE;
}

static void
calf_knob_class_init (CalfKnobClass *klass)
{
    // GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->expose_event = calf_knob_expose;
    widget_class->size_request = calf_knob_size_request;
    widget_class->enter_notify_event = calf_knob_enter;
    widget_class->leave_notify_event = calf_knob_leave;
    widget_class->button_press_event = calf_knob_button_press;
    widget_class->button_release_event = calf_knob_button_release;
    widget_class->motion_notify_event = calf_knob_pointer_motion;
    widget_class->key_press_event = calf_knob_key_press;
    widget_class->key_release_event = calf_knob_key_release;
    widget_class->scroll_event = calf_knob_scroll;
    GError *error = NULL;
    klass->knob_image[0] = gdk_pixbuf_new_from_file(PKGLIBDIR "/knob1.png", &error);
    klass->knob_image[1] = gdk_pixbuf_new_from_file(PKGLIBDIR "/knob2.png", &error);
    klass->knob_image[2] = gdk_pixbuf_new_from_file(PKGLIBDIR "/knob3.png", &error);
    klass->knob_image[3] = gdk_pixbuf_new_from_file(PKGLIBDIR "/knob4.png", &error);
    klass->knob_image[4] = gdk_pixbuf_new_from_file(PKGLIBDIR "/knob5.png", &error);
    g_assert(klass->knob_image != NULL);
}

static void
calf_knob_init (CalfKnob *self)
{
    GtkWidget *widget = GTK_WIDGET(self);
    GTK_WIDGET_SET_FLAGS (GTK_WIDGET(self), GTK_CAN_FOCUS);
    widget->requisition.width = 40;
    widget->requisition.height = 40;
}

GtkWidget *
calf_knob_new()
{
    GtkAdjustment *adj = (GtkAdjustment *)gtk_adjustment_new(0, 0, 1, 0.01, 0.5, 0);
    return calf_knob_new_with_adjustment(adj);
}

static gboolean calf_knob_value_changed(gpointer obj)
{
    GtkWidget *widget = (GtkWidget *)obj;
    gtk_widget_queue_draw(widget);
    return FALSE;
}

GtkWidget *calf_knob_new_with_adjustment(GtkAdjustment *_adjustment)
{
    GtkWidget *widget = GTK_WIDGET( g_object_new (CALF_TYPE_KNOB, NULL ));
    if (widget) {
        gtk_range_set_adjustment(GTK_RANGE(widget), _adjustment);
        g_signal_connect(GTK_OBJECT(widget), "value-changed", G_CALLBACK(calf_knob_value_changed), widget);
    }
    return widget;
}

GType
calf_knob_get_type (void)
{
    static GType type = 0;
    if (!type) {
        
        static const GTypeInfo type_info = {
            sizeof(CalfKnobClass),
            NULL, /* base_init */
            NULL, /* base_finalize */
            (GClassInitFunc)calf_knob_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof(CalfKnob),
            0,    /* n_preallocs */
            (GInstanceInitFunc)calf_knob_init
        };
        
        for (int i = 0; ; i++) {
            char *name = g_strdup_printf("CalfKnob%u%d", 
                ((unsigned int)(intptr_t)calf_knob_class_init) >> 16, i);
            if (g_type_from_name(name)) {
                free(name);
                continue;
            }
            type = g_type_register_static(GTK_TYPE_RANGE,
                                          name,
                                          &type_info,
                                          (GTypeFlags)0);
            free(name);
            break;
        }
    }
    return type;
}
