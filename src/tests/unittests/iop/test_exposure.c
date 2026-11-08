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

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "iop/exposure.c"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

static void _test_issue_example(void **state)
{
  // black 0.10, sample Y 0.25, target Y 0.50: the white the area mapping must pick
  const float y_in = 0.25f, y_black = 0.10f, black = 0.10f, y_target = 0.50f;

  const float white = _white_for_target_luminance(y_in, y_black, black, y_target);
  assert_float_equal(white, 0.40f, 1e-6f);
  assert_float_equal(white2exposure(white), 1.32193f, 1e-4f);

  float b, s;
  _exposure_black_and_scale(white2exposure(white), black, &b, &s);
  assert_float_equal((y_in - y_black) * s, y_target, 1e-5f);

  // the old pure-gain answer (white 0.5) maps the sample to 0.375 instead
  _exposure_black_and_scale(white2exposure(0.5f), black, &b, &s);
  assert_float_equal((y_in - y_black) * s, 0.375f, 1e-5f);
}

static void _test_round_trip(void **state)
{
  static const float blacks[] = { -0.05f, -0.000244140625f, 0.0f, 0.02f, 0.1f, 0.3f };
  static const float ks[] = { 1.0f, 0.93f };
  static const float y_ins[] = { 0.18f, 0.5f, 1.2f };
  static const float y_targets[] = { 0.05f, 0.18f, 0.5f, 0.9f, 2.0f };

  for(size_t bi = 0; bi < sizeof(blacks) / sizeof(blacks[0]); bi++)
    for(size_t ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
      for(size_t yi = 0; yi < sizeof(y_ins) / sizeof(y_ins[0]); yi++)
        for(size_t ti = 0; ti < sizeof(y_targets) / sizeof(y_targets[0]); ti++)
        {
          const float black = blacks[bi];
          const float y_black = black * ks[ki];
          const float y_in = y_ins[yi];
          const float y_target = y_targets[ti];
          if(y_in <= 1e-5f || y_in - y_black <= 1e-5f) continue;

          const float white = _white_for_target_luminance(y_in, y_black, black, y_target);
          if(white <= 0.0f) continue;

          float b, s;
          _exposure_black_and_scale(white2exposure(white), black, &b, &s);
          assert_true(b == black);
          assert_float_equal((y_in - y_black) * s, y_target, 1e-4f * y_target);
        }
}

static void _test_unreachable(void **state)
{
  assert_true(_white_for_target_luminance(0.10f, 0.10f, 0.10f, 0.5f) == 0.0f);
  assert_true(_white_for_target_luminance(0.05f, 0.10f, 0.10f, 0.5f) == 0.0f);
  // a near-black sample is refused even when black is negative
  assert_true(_white_for_target_luminance(0.0f, -0.000244140625f,
                                          -0.000244140625f, 0.5f) == 0.0f);
  assert_true(_white_for_target_luminance(1e-6f, -0.000244140625f,
                                          -0.000244140625f, 0.5f) == 0.0f);
  assert_true(_white_for_target_luminance(0.5f, 0.10f, 0.10f, 0.0f) == 0.0f);
  assert_true(_white_for_target_luminance(0.5f, 0.10f, 0.10f, -0.5f) == 0.0f);
  assert_true(_white_for_target_luminance(0.5f, 0.10f, 0.10f, 1e-7f) == 0.0f);
  assert_true(_white_for_target_luminance(NAN, 0.10f, 0.10f, 0.5f) == 0.0f);
  assert_true(_white_for_target_luminance(0.5f, 0.10f, 0.10f, NAN) == 0.0f);
  // the solved white would be -0.4
  assert_true(_white_for_target_luminance(-0.4f, -0.5f, -0.5f, 1.0f) == 0.0f);
}

static void _test_black_clamp(void **state)
{
  float b, s;

  // exposure 0 maps white 1.0; black at or above white is pulled under it
  _exposure_black_and_scale(0.0f, 1.0f, &b, &s);
  assert_float_equal(b, 0.99f, 1e-6f);
  assert_float_equal(s, 100.0f, 1e-3f);

  _exposure_black_and_scale(0.0f, 1.5f, &b, &s);
  assert_float_equal(b, 0.99f, 1e-6f);
  assert_float_equal(s, 100.0f, 1e-3f);

  // black below white is kept as is
  _exposure_black_and_scale(0.0f, 0.25f, &b, &s);
  assert_float_equal(b, 0.25f, 1e-6f);
  assert_float_equal(s, 1.0f / (1.0f - 0.25f), 1e-6f);
}

int main(int argc, char *argv[])
{
  darktable.num_openmp_threads = 1;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(_test_issue_example),
    cmocka_unit_test(_test_round_trip),
    cmocka_unit_test(_test_unreachable),
    cmocka_unit_test(_test_black_clamp)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
