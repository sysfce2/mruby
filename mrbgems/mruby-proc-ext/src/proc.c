#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/opcode.h>
#include <mruby/array.h>
#include <mruby/string.h>
#include <mruby/debug.h>
#include <mruby/presym.h>

static mrb_value
proc_lambda_p(mrb_state *mrb, mrb_value self)
{
  struct RProc *p = mrb_proc_ptr(self);
  return mrb_bool_value(MRB_PROC_STRICT_P(p));
}

mrb_value
mrb_proc_source_location(mrb_state *mrb, const struct RProc *p)
{
  if (MRB_PROC_CFUNC_P(p)) {
    return mrb_nil_value();
  }

  /* handle alias */
  if (MRB_PROC_ALIAS_P(p)) {
    p = p->upper;
  }

  const mrb_irep *irep = p->body.irep;
  int32_t line;
  const char *filename;

  if (!mrb_debug_get_position(mrb, irep, 0, &line, &filename)) {
    return mrb_nil_value();
  }
  return mrb_assoc_new(mrb, mrb_str_new_cstr(mrb, filename), mrb_fixnum_value(line));
}

static mrb_value
proc_source_location(mrb_state *mrb, mrb_value self)
{
  return mrb_proc_source_location(mrb, mrb_proc_ptr(self));
}

static mrb_value
proc_inspect(mrb_state *mrb, mrb_value self)
{
  struct RProc *p = mrb_proc_ptr(self);
  mrb_value str = mrb_str_new_lit(mrb, "#<Proc:");
  mrb_str_cat_str(mrb, str, mrb_ptr_to_str(mrb, mrb_cptr(self)));

  if (!MRB_PROC_CFUNC_P(p)) {
    const mrb_irep *irep = p->body.irep;
    const char *filename;
    int32_t line;
    mrb_str_cat_lit(mrb, str, " ");

    if (mrb_debug_get_position(mrb, irep, 0, &line, &filename)) {
      mrb_str_cat_cstr(mrb, str, filename);
      mrb_str_cat_lit(mrb, str, ":");
      mrb_str_concat(mrb, str, mrb_fixnum_value(line));
    }
    else {
      mrb_str_cat_lit(mrb, str, "-:-");
    }
  }

  if (MRB_PROC_STRICT_P(p)) {
    mrb_str_cat_lit(mrb, str, " (lambda)");
  }

  mrb_str_cat_lit(mrb, str, ">");
  return str;
}

static mrb_value
kernel_proc(mrb_state *mrb, mrb_value self)
{
  mrb_value blk;

  mrb_get_args(mrb, "&!", &blk);

  return blk;
}

/*
 * call-seq:
 *    prc.parameters  -> array
 *
 * Returns the parameter information of this proc.
 *
 *    prc = lambda{|x, y=42, *other|}
 *    prc.parameters  #=> [[:req, :x], [:opt, :y], [:rest, :other]]
 */

mrb_value
mrb_proc_parameters(mrb_state *mrb, mrb_value self)
{
  struct parameters_type {
    mrb_sym name;
    int size;
  } *p, parameters_list [] = {
    {MRB_SYM(req),    0},
    {MRB_SYM(opt),    0},
    {MRB_SYM(rest),   0},
    {MRB_SYM(req),    0},
    {MRB_SYM(keyrest),0},
    {MRB_SYM(block),  0},
    {MRB_SYM(key),    0},
    {0, 0}
  };
  int i;
  const struct RProc *proc = mrb_proc_ptr(self);
  if (MRB_PROC_CFUNC_P(proc)) {
    // TODO cfunc aspec is not implemented yet
    return mrb_ary_new(mrb);
  }
  const struct mrb_irep *irep = proc->body.irep;
  if (!irep || !irep->lv || *irep->iseq != OP_ENTER) {
    return mrb_ary_new(mrb);
  }

  if (!MRB_PROC_STRICT_P(proc)) {
    parameters_list[0].name = MRB_SYM(opt);
    parameters_list[3].name = MRB_SYM(opt);
  }

  mrb_aspec aspec = PEEK_W(irep->iseq+1);
  parameters_list[0].size = MRB_ASPEC_REQ(aspec);
  parameters_list[1].size = MRB_ASPEC_OPT(aspec);
  parameters_list[2].size = MRB_ASPEC_REST(aspec);
  parameters_list[3].size = MRB_ASPEC_POST(aspec);
  parameters_list[4].size = MRB_ASPEC_KDICT(aspec);
  parameters_list[5].size = MRB_ASPEC_BLOCK(aspec);
  parameters_list[6].size = MRB_ASPEC_KEY(aspec);

  int max = 0;
  for (i = 0; parameters_list[i].name; i++) {
    max += parameters_list[i].size;
  }

  mrb_value parameters = mrb_ary_new_capa(mrb, max);
  mrb_value krest = mrb_nil_value();
  mrb_value block = mrb_nil_value();

  for (i = 0, p = parameters_list; p->name; p++) {
    mrb_value sname = mrb_symbol_value(p->name);

    for (int j = 0; j < p->size; i++, j++) {
      mrb_value a = mrb_ary_new(mrb);
      mrb_ary_push(mrb, a, sname);
      if (i < max && irep->lv[i]) {
        mrb_ary_push(mrb, a, mrb_symbol_value(irep->lv[i]));
      }
      if (p->name == MRB_SYM(block)) {
        if (irep->lv[i+1]) {
          mrb_ary_push(mrb, a, mrb_symbol_value(irep->lv[i+1]));
        }
        block = a; continue;
      }
      if (p->name == MRB_SYM(keyrest)) {
        krest = a; continue;
      }
      mrb_ary_push(mrb, parameters, a);
    }
    /* need to skip empty block slot */
    if (p->size == 0 && p->name == MRB_SYM(block)) i++;
  }
  if (!mrb_nil_p(krest)) mrb_ary_push(mrb, parameters, krest);
  if (!mrb_nil_p(block)) mrb_ary_push(mrb, parameters, block);
  return parameters;
}

void
mrb_mruby_proc_ext_gem_init(mrb_state* mrb)
{
  struct RClass *p = mrb->proc_class;
  mrb_define_method_id(mrb, p, MRB_SYM_Q(lambda),        proc_lambda_p,        MRB_ARGS_NONE());
  mrb_define_method_id(mrb, p, MRB_SYM(source_location), proc_source_location, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, p, MRB_SYM(to_s),            proc_inspect,         MRB_ARGS_NONE());
  mrb_define_method_id(mrb, p, MRB_SYM(inspect),         proc_inspect,         MRB_ARGS_NONE());
  mrb_define_method_id(mrb, p, MRB_SYM(parameters),      mrb_proc_parameters,  MRB_ARGS_NONE());

  mrb_define_private_method_id(mrb, mrb->kernel_module, MRB_SYM(proc), kernel_proc,  MRB_ARGS_NONE()|MRB_ARGS_BLOCK());
}

void
mrb_mruby_proc_ext_gem_final(mrb_state* mrb)
{
}
