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

// the layouts legacy_params() migrates, kept in sync with its local typedefs
typedef struct test_exposure_params_v6_t
{
  dt_iop_exposure_mode_t mode;
  float black;
  float exposure;
  float deflicker_percentile;
  float deflicker_target_level;
  gboolean compensate_exposure_bias;
} test_exposure_params_v6_t;

typedef struct test_exposure_params_v7_t
{
  dt_iop_exposure_mode_t mode;
  float black;
  float exposure;
  float deflicker_percentile;
  float deflicker_target_level;
  gboolean compensate_exposure_bias;
  gboolean compensate_hilite_pres;
} test_exposure_params_v7_t;

static dt_iop_module_t *_alloc_module(dt_develop_t **dev)
{
  *dev = calloc(1, sizeof(dt_develop_t));
  assert_non_null(*dev);
  dt_iop_module_t *module = calloc(1, sizeof(dt_iop_module_t));
  assert_non_null(module);
  module->dev = *dev;
  return module;
}

static void _test_legacy_v7_to_v8(void **state)
{
  const test_exposure_params_v7_t v7 =
    { .mode = EXPOSURE_MODE_MANUAL,
      .black = -0.000244140625f,
      .exposure = 0.7f,
      .deflicker_percentile = 50.0f,
      .deflicker_target_level = -4.0f,
      .compensate_exposure_bias = TRUE,
      .compensate_hilite_pres = TRUE };
  assert_int_equal(offsetof(dt_iop_exposure_params_t, hilite_pres_rev), sizeof(v7));

  dt_develop_t *dev = NULL;
  dt_iop_module_t *module = _alloc_module(&dev);

  void *new_params = NULL;
  int32_t new_params_size = 0;
  int new_version = 0;
  assert_int_equal(legacy_params(module, &v7, 7, &new_params, &new_params_size, &new_version), 0);
  assert_int_equal(new_version, 8);
  assert_int_equal(new_params_size, sizeof(dt_iop_exposure_params_t));

  const dt_iop_exposure_params_t *n = new_params;
  assert_non_null(n);
  assert_int_equal(n->mode, v7.mode);
  assert_float_equal(n->black, v7.black, 1e-6f);
  assert_float_equal(n->exposure, v7.exposure, 1e-6f);
  assert_float_equal(n->deflicker_percentile, v7.deflicker_percentile, 1e-6f);
  assert_float_equal(n->deflicker_target_level, v7.deflicker_target_level, 1e-6f);
  assert_int_equal(n->compensate_exposure_bias, v7.compensate_exposure_bias);
  assert_int_equal(n->compensate_hilite_pres, v7.compensate_hilite_pres);
  assert_int_equal(n->hilite_pres_rev, 1);

  free(new_params);
  free(module);
  free(dev);
}

static void _test_legacy_v6_to_v8_chain(void **state)
{
  const test_exposure_params_v6_t v6 =
    { .mode = EXPOSURE_MODE_DEFLICKER,
      .black = 0.0f,
      .exposure = 0.0f,
      .deflicker_percentile = 50.0f,
      .deflicker_target_level = -4.0f,
      .compensate_exposure_bias = TRUE };

  dt_develop_t *dev = NULL;
  dt_iop_module_t *module = _alloc_module(&dev);

  void *v7 = NULL;
  int32_t v7_size = 0;
  int v7_version = 0;
  assert_int_equal(legacy_params(module, &v6, 6, &v7, &v7_size, &v7_version), 0);
  assert_int_equal(v7_version, 7);
  assert_int_equal(v7_size, offsetof(dt_iop_exposure_params_t, hilite_pres_rev));

  void *v8 = NULL;
  int32_t v8_size = 0;
  int v8_version = 0;
  assert_int_equal(legacy_params(module, v7, 7, &v8, &v8_size, &v8_version), 0);
  assert_int_equal(v8_version, 8);
  assert_int_equal(v8_size, sizeof(dt_iop_exposure_params_t));

  const dt_iop_exposure_params_t *n = v8;
  assert_non_null(n);
  assert_int_equal(n->mode, EXPOSURE_MODE_DEFLICKER);
  assert_int_equal(n->compensate_hilite_pres, FALSE);
  assert_int_equal(n->hilite_pres_rev, 1);

  free(v8);
  free(v7);
  free(module);
  free(dev);
}

static void _test_highlight_bias_pinned_revision(void **state)
{
  dt_develop_t *dev = NULL;
  dt_iop_module_t *module = _alloc_module(&dev);
  dev->image_storage.exif_highlight_preservation_rev[0] = 0.0f;
  dev->image_storage.exif_highlight_preservation_rev[1] = 0.5f;
  dev->image_storage.exif_highlight_preservation_rev[2] = 1.5f;

  dt_iop_exposure_params_t p = { .hilite_pres_rev = 1 };
  assert_float_equal(_get_highlight_bias(module, &p), 0.0f, 1e-6f);
  p.hilite_pres_rev = 2;
  assert_float_equal(_get_highlight_bias(module, &p), 0.5f, 1e-6f);
  p.hilite_pres_rev = 3;
  assert_float_equal(_get_highlight_bias(module, &p), 1.5f, 1e-6f);
  p.hilite_pres_rev = 0;  // clamped to revision 1
  assert_float_equal(_get_highlight_bias(module, &p), 0.0f, 1e-6f);
  p.hilite_pres_rev = 99; // clamped to revision 3
  assert_float_equal(_get_highlight_bias(module, &p), 1.5f, 1e-6f);

  free(module);
  free(dev);
}

static void _test_exposure_compensation_ev(void **state)
{
  dt_develop_t *dev = NULL;
  dt_iop_module_t *module = _alloc_module(&dev);
  dev->image_storage.exif_highlight_preservation_rev[2] = 1.5f;

  dt_iop_exposure_params_t p = { .compensate_hilite_pres = TRUE,
                                 .hilite_pres_rev = 1 };
  assert_float_equal(_exposure_compensation_ev(module, &p), 0.0f, 1e-6f);
  p.hilite_pres_rev = 3;
  assert_float_equal(_exposure_compensation_ev(module, &p), 1.5f, 1e-6f);
  p.compensate_hilite_pres = FALSE;
  assert_float_equal(_exposure_compensation_ev(module, &p), 0.0f, 1e-6f);

  free(module);
  free(dev);
}

int main(int argc, char *argv[])
{
  darktable.num_openmp_threads = 1;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(_test_legacy_v7_to_v8),
    cmocka_unit_test(_test_legacy_v6_to_v8_chain),
    cmocka_unit_test(_test_highlight_bias_pinned_revision),
    cmocka_unit_test(_test_exposure_compensation_ev)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
