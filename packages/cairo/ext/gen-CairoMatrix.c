/* ruby-cairo - Ruby bindings for Cairo.
 * Copyright (C) 2003 Evan Martin <martine@danga.com>
 *           (C) 2004 Øyvind Kolås <pippin@freedesktop.org>
 *
 */

#include "rbcairo.h"

static VALUE
rcairo_matrix_set_affine (VALUE self,
                          VALUE a, VALUE b,
						  VALUE c, VALUE d,
						  VALUE tx, VALUE ty)
{
	cairo_matrix_set_affine (rmatrix_get_matrix(self),
	                         NUM2DBL(a), NUM2DBL(b),
							 NUM2DBL(c), NUM2DBL(d),
							 NUM2DBL(tx), NUM2DBL(ty));
	return Qnil;
}

static VALUE
rcairo_matrix_set_identity (VALUE self)
{
	return INT2FIX(cairo_matrix_set_identity(rmatrix_get_matrix(self)));
}

static VALUE
rcairo_matrix_invert (VALUE self)
{
	return INT2FIX(cairo_matrix_invert(rmatrix_get_matrix(self)));
}


VALUE gen_CairoMatrix (void)
{
	cCairoMatrix = rb_define_class_under(mCairo, "Matrix", rb_cObject);
	rb_define_method(cCairoMatrix, "set_affine", rcairo_matrix_set_affine, 6);
	rb_define_method(cCairoMatrix, "set_identity", rcairo_matrix_set_identity, 0);
	rb_define_method(cCairoMatrix, "invert", rcairo_matrix_invert, 0);
	return cCairoMatrix;
}