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

// sidecar discovery for duplicate versions: root.ext.xmp plus
// root_NN.ext.xmp.  root_NN.ext.xmp may instead be the sidecar of
// another image root_NN.ext in the same folder and must not be picked
// up as a version of root.ext.  works on explicit file paths so no
// database or image is needed

#include "common/image.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

typedef struct _fixture_t
{
  gchar *dir;
} _fixture_t;

static int _setup(void **state)
{
  _fixture_t *f = g_new0(_fixture_t, 1);
  GError *err = NULL;
  f->dir = g_dir_make_tmp("dt-dupes-XXXXXX", &err);
  if(!f->dir)
  {
    g_clear_error(&err);
    g_free(f);
    return 1;
  }
  *state = f;
  return 0;
}

static int _teardown(void **state)
{
  _fixture_t *f = *state;
  GDir *d = g_dir_open(f->dir, 0, NULL);
  if(d)
  {
    const gchar *name;
    while((name = g_dir_read_name(d)))
    {
      gchar *p = g_build_filename(f->dir, name, NULL);
      g_unlink(p);
      g_free(p);
    }
    g_dir_close(d);
  }
  g_rmdir(f->dir);
  g_free(f->dir);
  g_free(f);
  return 0;
}

static void _add_file(_fixture_t *f, const char *name)
{
  gchar *p = g_build_filename(f->dir, name, NULL);
  // non-empty: the plain root.ext.xmp sidecar is skipped when empty
  g_file_set_contents(p, "x", 1, NULL);
  g_free(p);
}

static void _add_empty_file(_fixture_t *f, const char *name)
{
  gchar *p = g_build_filename(f->dir, name, NULL);
  g_file_set_contents(p, "", 0, NULL);
  g_free(p);
}

static int _cmp_str(gconstpointer a, gconstpointer b)
{
  return strcmp(a, b);
}

// run dt_image_find_duplicates() on dir/image and compare the
// basenames of the result against the sorted set expected
static void _assert_dupes(_fixture_t *f,
                          const char *image,
                          const char **expected)
{
  gchar *path = g_build_filename(f->dir, image, NULL);
  GList *found = dt_image_find_duplicates(path);
  GList *names = NULL;
  for(GList *l = found; l; l = g_list_next(l))
  {
    const char *base = strrchr(l->data, G_DIR_SEPARATOR);
    names = g_list_prepend(names, g_strdup(base ? base + 1 : l->data));
  }
  names = g_list_sort(names, _cmp_str);
  size_t n = 0;
  while(expected[n]) n++;
  assert_int_equal(g_list_length(names), n);
  size_t i = 0;
  for(GList *l = names; l; l = g_list_next(l), i++)
    assert_string_equal(l->data, expected[i]);
  g_list_free_full(names, g_free);
  g_list_free_full(found, g_free);
  g_free(path);
}

static void test_burst_names(void **state)
{
  _fixture_t *f = *state;
  const char *all[] = { "IMG_x.jpg", "IMG_x.jpg.xmp", "IMG_x_01.jpg.xmp",
                        "IMG_x_1.jpg", "IMG_x_1.jpg.xmp",
                        "IMG_x_10.jpg", "IMG_x_10.jpg.xmp", NULL };
  for(size_t i = 0; all[i]; i++)
    _add_file(f, all[i]);

  // IMG_x_10.jpg.xmp belongs to the image IMG_x_10.jpg, not to IMG_x.jpg
  const char *expect_x[] = { "IMG_x.jpg.xmp", "IMG_x_01.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_x.jpg", expect_x);

  const char *expect_10[] = { "IMG_x_10.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_x_10.jpg", expect_10);

  const char *expect_1[] = { "IMG_x_1.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_x_1.jpg", expect_1);
}

static void test_digits_in_basename(void **state)
{
  _fixture_t *f = *state;
  const char *all[] = { "IMG_2026.jpg", "IMG_2026.jpg.xmp",
                        "IMG_2026_02.jpg.xmp", "IMG_2026_123.jpg.xmp", NULL };
  for(size_t i = 0; all[i]; i++)
    _add_file(f, all[i]);

  // digits in the root name must not confuse the version suffix check
  const char *expect[] = { "IMG_2026.jpg.xmp", "IMG_2026_02.jpg.xmp",
                           "IMG_2026_123.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_2026.jpg", expect);
}

static void test_orphan_one_digit_sidecar(void **state)
{
  _fixture_t *f = *state;
  const char *all[] = { "IMG_z.jpg", "IMG_z.jpg.xmp", "IMG_z_3.jpg.xmp", NULL };
  for(size_t i = 0; all[i]; i++)
    _add_file(f, all[i]);

  // a one-digit suffix is never a darktable version ("_%02d")
  const char *expect[] = { "IMG_z.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_z.jpg", expect);
}

static void test_zero_padded_suffix(void **state)
{
  _fixture_t *f = *state;
  const char *all[] = { "IMG_z.jpg", "IMG_z.jpg.xmp", "IMG_z_3.jpg.xmp",
                        "IMG_z_005.jpg.xmp", "IMG_z_07.jpg.xmp",
                        "IMG_z_100.jpg.xmp", NULL };
  for(size_t i = 0; all[i]; i++)
    _add_file(f, all[i]);

  // "_%02d" never writes "_005"; "_07" and "_100" are fine
  const char *expect[] = { "IMG_z.jpg.xmp", "IMG_z_07.jpg.xmp",
                           "IMG_z_100.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_z.jpg", expect);
}

static void test_empty_file_claims_its_sidecar(void **state)
{
  _fixture_t *f = *state;
  const char *all[] = { "IMG_e.jpg", "IMG_e.jpg.xmp", "IMG_e_02.jpg.xmp", NULL };
  for(size_t i = 0; all[i]; i++)
    _add_file(f, all[i]);
  _add_empty_file(f, "IMG_e_02.jpg");

  // the empty IMG_e_02.jpg still owns IMG_e_02.jpg.xmp
  const char *expect_e[] = { "IMG_e.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_e.jpg", expect_e);

  const char *expect_02[] = { "IMG_e_02.jpg.xmp", NULL };
  _assert_dupes(f, "IMG_e_02.jpg", expect_02);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_burst_names, _setup, _teardown),
    cmocka_unit_test_setup_teardown(test_digits_in_basename, _setup, _teardown),
    cmocka_unit_test_setup_teardown(test_orphan_one_digit_sidecar, _setup, _teardown),
    cmocka_unit_test_setup_teardown(test_zero_padded_suffix, _setup, _teardown),
    cmocka_unit_test_setup_teardown(test_empty_file_claims_its_sidecar, _setup, _teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
