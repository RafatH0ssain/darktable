/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Unit tests for the smooth-scroll unit stepping of
 * dt_gui_get_scroll_unit_deltas() (gui/gtk.c). A single wheel notch can
 * arrive as one GDK_SCROLL_SMOOTH event with |delta| > 1 (e.g. 1.5 on
 * Wayland) and must still count as exactly one step (#23019). */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include <gtk/gtk.h>

#include "gui/gtk.h"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#ifdef GDK_WINDOWING_QUARTZ // on macOS deltas are scaled (see gui/gtk.c)
#define SCALE DT_UI_SCROLL_SMOOTH_DELTA_SCALE
#else
#define SCALE 1.0
#endif

static GdkEvent *_smooth(gdouble dx, gdouble dy, gboolean stop)
{
  GdkEvent *e = gdk_event_new(GDK_SCROLL);
  GdkEventScroll *s = (GdkEventScroll *)e;
  s->direction = GDK_SCROLL_SMOOTH;
  s->delta_x = dx * SCALE;
  s->delta_y = dy * SCALE;
  s->is_stop = stop;
  return e;
}

static gboolean _step(gdouble dy, int *out)
{
  GdkEvent *e = _smooth(0.0, dy, FALSE);
  int dx = 0;
  *out = 0;
  const gboolean r = dt_gui_get_scroll_unit_deltas(e, &dx, out);
  gdk_event_free(e);
  return r;
}

// the unit-step accumulator is static, so reset it before every test
static void _reset(void)
{
  GdkEvent *e = _smooth(0, 0, TRUE);
  int x, y;
  dt_gui_get_scroll_unit_deltas(e, &x, &y);
  gdk_event_free(e);
}

// one notch with delta 1.5 is still one step per event (#23019)
static void test_overshoot_steps_once(void **state)
{
  _reset();
  for(int i = 0; i < 4; i++)
  {
    int step = 0;
    assert_true(_step(1.5, &step));
    assert_int_equal(step, 1);
  }
}

// one notch with delta 2.0 is still one step per event
static void test_double_unit_steps_once(void **state)
{
  _reset();
  for(int i = 0; i < 2; i++)
  {
    int step = 0;
    assert_true(_step(2.0, &step));
    assert_int_equal(step, 1);
  }
}

// exactly one unit per event keeps stepping one unit per event
static void test_unit_delta_steps_once(void **state)
{
  _reset();
  for(int i = 0; i < 4; i++)
  {
    int step = 0;
    assert_true(_step(1.0, &step));
    assert_int_equal(step, 1);
  }
}

// sub-unit deltas accumulate and fire one step once they pass a unit
static void test_sub_unit_deltas_accumulate(void **state)
{
  _reset();
  int step = 0;
  assert_false(_step(0.4, &step));
  assert_false(_step(0.4, &step));
  assert_true(_step(0.4, &step));
  assert_int_equal(step, 1);
}

// overshoot in the other direction steps -1 per event
static void test_negative_overshoot_steps_once(void **state)
{
  _reset();
  for(int i = 0; i < 2; i++)
  {
    int step = 0;
    assert_true(_step(-1.5, &step));
    assert_int_equal(step, -1);
  }
}

int main(int argc, char *argv[])
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_overshoot_steps_once),
    cmocka_unit_test(test_double_unit_steps_once),
    cmocka_unit_test(test_unit_delta_steps_once),
    cmocka_unit_test(test_sub_unit_deltas_accumulate),
    cmocka_unit_test(test_negative_overshoot_steps_once)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
