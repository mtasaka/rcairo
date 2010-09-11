/* -*- c-file-style: "gnu"; indent-tabs-mode: nil -*- */
/*
 * Ruby Cairo Binding
 *
 * Copyright 2010 Kouhei Sutou <kou@cozmixng.org>
 *
 * This file is made available under the same terms as Ruby
 *
 */

#include "rb_cairo.h"
#include "rb_cairo_private.h"
#include "rb_cairo_io.h"

#ifdef HAVE_RUBY_ST_H
#  include <ruby/st.h>
#else
#  include <st.h>
#endif

#ifdef CAIRO_HAS_SCRIPT_SURFACE
#  include <cairo-script.h>
#endif

VALUE rb_cCairo_Device = Qnil;
VALUE rb_cCairo_DRMDevice = Qnil;
VALUE rb_cCairo_GLDevice = Qnil;
VALUE rb_cCairo_ScriptDevice = Qnil;
VALUE rb_cCairo_XCBDevice = Qnil;
VALUE rb_cCairo_XlibDevice = Qnil;
VALUE rb_cCairo_XMLDevice = Qnil;

static cairo_user_data_key_t cr_closure_key;
static cairo_user_data_key_t cr_object_holder_key;
static cairo_user_data_key_t cr_finished_key;

#define _SELF  (RVAL2CRDEVICE(self))

#if CAIRO_CHECK_VERSION(1, 10, 0)
static inline void
cr_device_check_status (cairo_device_t *device)
{
  rb_cairo_check_status (cairo_device_status (device));
}

static VALUE
cr_device_get_klass (cairo_device_t *device)
{
  VALUE klass;
  cairo_device_type_t type;

  type = cairo_device_get_type (device);
  switch (type)
    {
    case CAIRO_DEVICE_TYPE_DRM:
      klass = rb_cCairo_DRMDevice;
      break;
    case CAIRO_DEVICE_TYPE_GL:
      klass = rb_cCairo_GLDevice;
      break;
    case CAIRO_DEVICE_TYPE_SCRIPT:
      klass = rb_cCairo_ScriptDevice;
      break;
    case CAIRO_DEVICE_TYPE_XCB:
      klass = rb_cCairo_XCBDevice;
      break;
    case CAIRO_DEVICE_TYPE_XLIB:
      klass = rb_cCairo_XlibDevice;
      break;
    case CAIRO_DEVICE_TYPE_XML:
      klass = rb_cCairo_XMLDevice;
      break;
    default:
      klass = rb_cCairo_Device;
      break;
    }

  if (NIL_P (klass))
    rb_raise (rb_eArgError, "unknown device type: %d", type);

  return klass;
}

/* constructor/de-constructor */
cairo_device_t *
rb_cairo_device_from_ruby_object (VALUE obj)
{
  cairo_device_t *device;
  if (!rb_cairo__is_kind_of (obj, rb_cCairo_Device))
    {
      rb_raise (rb_eTypeError, "not a cairo device");
    }
  Data_Get_Struct (obj, cairo_device_t, device);
  if (!device)
    rb_cairo_check_status (CAIRO_STATUS_NULL_POINTER);
  return device;
}

static rb_cairo__object_holder_t *
cr_object_holder_new (VALUE object)
{
  return rb_cairo__object_holder_new (rb_cCairo_Device, object);
}

static void
cr_object_holder_free (void *ptr)
{
  rb_cairo__object_holder_free (rb_cCairo_Device, ptr);
}

static void
cr_device_free (void *ptr)
{
  cairo_device_t *device = ptr;

  if (device)
    cairo_device_destroy (device);
}

VALUE
rb_cairo_device_to_ruby_object (cairo_device_t *device)
{
  if (device)
    {
      VALUE klass;
      klass = cr_device_get_klass (device);
      cairo_device_reference (device);
      return Data_Wrap_Struct (klass, NULL, cr_device_free, device);
    }
  else
    {
      return Qnil;
    }
}

VALUE
rb_cairo_device_to_ruby_object_with_destroy (cairo_device_t *device)
{
  VALUE rb_device;

  rb_device = rb_cairo_device_to_ruby_object (device);
  if (device)
    cairo_device_destroy (device);

  return rb_device;
}

static VALUE
cr_device_allocate (VALUE klass)
{
  return Data_Wrap_Struct (klass, NULL, cr_device_free, NULL);
}

/* Backend device manipulation */
static VALUE
cr_device_destroy (VALUE self)
{
  cairo_device_t *device;

  device = _SELF;
  cairo_device_destroy (device);
  DATA_PTR (self) = NULL;

  return self;
}

static VALUE
cr_device_finish (VALUE self)
{
  cairo_device_t *device;
  rb_cairo__io_callback_closure_t *closure;

  device = _SELF;
  closure = cairo_device_get_user_data (device, &cr_closure_key);

  cairo_device_finish (device);
  cairo_device_set_user_data (device, &cr_finished_key, (void *)CR_TRUE, NULL);
  cairo_device_set_user_data (device, &cr_object_holder_key, NULL, NULL);

  if (closure && !NIL_P (closure->error))
    rb_exc_raise (closure->error);
  cr_device_check_status (device);

  return self;
}

static VALUE
cr_device_flush (VALUE self)
{
  cairo_device_flush (_SELF);
  cr_device_check_status (_SELF);
  return self;
}

static VALUE
cr_device_release (VALUE self)
{
  cairo_device_release (_SELF);
  cr_device_check_status (_SELF);
  return self;
}

static VALUE
cr_device_acquire (VALUE self)
{
  cairo_device_acquire (_SELF);
  cr_device_check_status (_SELF);
  if (rb_block_given_p ())
    return rb_ensure (rb_yield, self, cr_device_release, self);
  else
    return self;
}

static int
cr_finish_all_guarded_devices_at_end_iter (VALUE key, VALUE value, VALUE data)
{
  cr_device_finish (key);
  return ST_CONTINUE;
}

static void
cr_finish_all_guarded_devices_at_end (VALUE data)
{
  rb_hash_foreach (rb_cairo__gc_guarded_objects (rb_cCairo_Device),
                   cr_finish_all_guarded_devices_at_end_iter,
                   Qnil);
}

static void
yield_and_finish (VALUE self)
{
  cairo_device_t *device;

  rb_yield (self);

  device = _SELF;
  if (!cairo_device_get_user_data (device, &cr_finished_key))
    cr_device_finish (self);
}

#  ifdef CAIRO_HAS_SCRIPT_SURFACE
static VALUE
cr_script_device_initialize (VALUE self, VALUE file_name_or_output)
{
  cairo_device_t *device;

  if (rb_respond_to (file_name_or_output, rb_cairo__io_id_write))
    {
      rb_cairo__io_callback_closure_t *closure;

      closure = rb_cairo__io_closure_new (file_name_or_output);
      device = cairo_script_create_for_stream (rb_cairo__io_write_func,
                                               (void *)closure);
      if (cairo_device_status (device))
        {
          rb_cairo__io_closure_destroy (closure);
        }
      else
        {
          cairo_device_set_user_data (device, &cr_closure_key,
                                      closure, rb_cairo__io_closure_free);
          cairo_device_set_user_data (device, &cr_object_holder_key,
                                      cr_object_holder_new (self),
                                      cr_object_holder_free);
        }
    }
  else
    {
      device = cairo_script_create (StringValueCStr (file_name_or_output));
    }

  cr_device_check_status (device);
  DATA_PTR (self) = device;
  if (rb_block_given_p ())
    yield_and_finish (self);
  return Qnil;
}

#  endif

#endif

void
Init_cairo_device (void)
{
#if CAIRO_CHECK_VERSION(1, 10, 0)
  rb_cCairo_Device =
    rb_define_class_under (rb_mCairo, "Device", rb_cObject);
  rb_define_alloc_func (rb_cCairo_Device, cr_device_allocate);

  rb_cairo__initialize_gc_guard_holder_class (rb_cCairo_Device);
  rb_set_end_proc(cr_finish_all_guarded_devices_at_end, Qnil);


  rb_define_method (rb_cCairo_Device, "destroy", cr_device_destroy, 0);
  rb_define_method (rb_cCairo_Device, "finish", cr_device_finish, 0);
  rb_define_method (rb_cCairo_Device, "flush", cr_device_flush, 0);
  rb_define_method (rb_cCairo_Device, "acquire", cr_device_acquire, 0);
  rb_define_method (rb_cCairo_Device, "release", cr_device_release, 0);

  RB_CAIRO_DEF_SETTERS (rb_cCairo_Device);

#  ifdef CAIRO_HAS_SCRIPT_SURFACE
  rb_cCairo_ScriptDevice =
    rb_define_class_under (rb_mCairo, "ScriptDevice", rb_cCairo_Device);

  rb_define_method (rb_cCairo_ScriptDevice, "initialize",
                    cr_script_device_initialize, 1);

  RB_CAIRO_DEF_SETTERS (rb_cCairo_ScriptDevice);
#  endif

#endif
}
