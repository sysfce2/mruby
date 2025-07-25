#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/internal.h>
#include <mruby/presym.h>

// Defined by mruby-proc-ext on which mruby-method depends
mrb_value mrb_proc_parameters(mrb_state *mrb, mrb_value proc);
mrb_value mrb_proc_source_location(mrb_state *mrb, const struct RProc *p);

static mrb_value
args_shift(mrb_state *mrb)
{
  mrb_callinfo *ci = mrb->c->ci;
  mrb_value *argv = ci->stack + 1;

  if (ci->n < 15) {
    if (ci->n == 0) { goto argerr; }
    mrb_assert(ci->nk == 0 || ci->nk == 15);
    mrb_value obj = argv[0];
    int count = ci->n + (ci->nk == 0 ? 0 : 1) + 1 /* block */ - 1 /* first value */;
    memmove(argv, argv + 1, count * sizeof(mrb_value));
    ci->n--;
    return obj;
  }
  else if (RARRAY_LEN(*argv) > 0) {
    return mrb_ary_shift(mrb, *argv);
  }
  else {
  argerr:
    mrb_argnum_error(mrb, 0, 1, -1);
    return mrb_undef_value(); /* not reached */
  }
}

static void
args_unshift(mrb_state *mrb, mrb_value obj)
{
  mrb_callinfo *ci = mrb->c->ci;
  mrb_value *argv = ci->stack + 1;

  if (ci->n < 15) {
    mrb_assert(ci->nk == 0 || ci->nk == 15);
    mrb_value args = mrb_ary_new_from_values(mrb, ci->n, argv);
    if (ci->nk == 0) {
      mrb_value block = argv[ci->n];
      argv[0] = args;
      argv[1] = block;
    }
    else {
      mrb_value keyword = argv[ci->n];
      mrb_value block = argv[ci->n + 1];
      argv[0] = args;
      argv[1] = keyword;
      argv[2] = block;
    }
    ci->n = 15;
  }

  mrb_ary_unshift(mrb, *argv, obj);
}

static const struct RProc*
method_missing_prepare(mrb_state *mrb, mrb_sym *mid, mrb_value recv, struct RClass **tc)
{
  const mrb_sym id_method_missing = MRB_SYM(method_missing);
  mrb_callinfo *ci = mrb->c->ci;

  if (*mid == id_method_missing) {
  method_missing: ;
    int n = ci->n;
    mrb_value *argv = ci->stack + 1;
    mrb_value args = (n == 15) ? argv[0] : mrb_ary_new_from_values(mrb, n, argv);
    mrb_method_missing(mrb, id_method_missing, recv, args);
  }

  *tc = mrb_class(mrb, recv);
  mrb_method_t m = mrb_method_search_vm(mrb, tc, id_method_missing);
  if (MRB_METHOD_UNDEF_P(m)) {
    goto method_missing;
  }

  const struct RProc *proc;
  if (MRB_METHOD_FUNC_P(m)) {
    struct RProc *p = mrb_proc_new_cfunc(mrb, MRB_METHOD_FUNC(m));
    MRB_PROC_SET_TARGET_CLASS(p, *tc);
    proc = p;
  }
  else {
    proc = MRB_METHOD_PROC(m);
  }

  args_unshift(mrb, mrb_symbol_value(*mid));
  *mid = id_method_missing;

  return proc;
}

static struct RObject *
method_object_alloc(mrb_state *mrb, struct RClass *mclass)
{
  return MRB_OBJ_ALLOC(mrb, MRB_TT_OBJECT, mclass);
}

static const struct RProc*
method_extract_proc(mrb_state *mrb, mrb_value self)
{
  mrb_value obj = mrb_iv_get(mrb, self, MRB_SYM(_proc));
  if (mrb_nil_p(obj)) {
    return NULL;
  }
  else {
    mrb_check_type(mrb, obj, MRB_TT_PROC);
    return mrb_proc_ptr(obj);
  }
}

static mrb_value
method_extract_receiver(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, MRB_SYM(_recv));
}

static mrb_sym
method_extract_mid(mrb_state *mrb, mrb_value self)
{
  mrb_value obj = mrb_iv_get(mrb, self, MRB_SYM(_name));
  mrb_check_type(mrb, obj, MRB_TT_SYMBOL);
  return mrb_symbol(obj);
}

static struct RClass*
method_extract_owner(mrb_state *mrb, mrb_value self)
{
  mrb_value obj = mrb_iv_get(mrb, self, MRB_SYM(_owner));
  switch (mrb_type(obj)) {
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
    case MRB_TT_SCLASS:
      break;
    default:
      mrb_raise(mrb, E_TYPE_ERROR, "not class/module as owner of method object");
  }
  return mrb_class_ptr(obj);
}

static void
bind_check(mrb_state *mrb, mrb_value recv, mrb_value owner)
{
  if (!mrb_module_p(owner) &&
      mrb_class_ptr(owner) != mrb_obj_class(mrb, recv) &&
      !mrb_obj_is_kind_of(mrb, recv, mrb_class_ptr(owner))) {
    if (mrb_sclass_p(owner)) {
      mrb_raise(mrb, E_TYPE_ERROR, "singleton method called for a different object");
    }
    else {
      mrb_raisef(mrb, E_TYPE_ERROR, "bind argument must be an instance of %v", owner);
    }
  }
}

/*
 *  call-seq:
 *     unbound_method.bind(obj) -> method
 *
 *  Bind unbound_method to obj. If Klass was the class
 *  from which unbound_method was obtained,
 *  obj.kind_of?(Klass) must be true.
 *
 *     class A
 *       def test
 *         puts "In A"
 *       end
 *     end
 *     class B < A
 *     end
 *     um = B.instance_method(:test)
 *     bm = um.bind(B.new)
 *     bm.call
 *     bm = um.bind(A.new)
 *     bm.call
 *
 *  produces:
 *
 *     In A
 *     In A
 */

static mrb_value
unbound_method_bind(mrb_state *mrb, mrb_value self)
{
  mrb_value owner = mrb_iv_get(mrb, self, MRB_SYM(_owner));
  mrb_value name = mrb_iv_get(mrb, self, MRB_SYM(_name));
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));
  mrb_value klass = mrb_iv_get(mrb, self, MRB_SYM(_klass));
  mrb_value recv = mrb_get_arg1(mrb);

  bind_check(mrb, recv, owner);

  struct RObject *me = method_object_alloc(mrb, mrb_class_get_id(mrb, MRB_SYM(Method)));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_owner), owner);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_recv), recv);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_name), name);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_proc), proc);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_klass), klass);

  return mrb_obj_value(me);
}

static mrb_bool
method_p(mrb_state *mrb, struct RClass *c, mrb_value proc)
{
  if (mrb_type(proc) != MRB_TT_OBJECT) return FALSE;
  if (!mrb_obj_is_instance_of(mrb, proc, c)) return FALSE;

  struct RObject *p = mrb_obj_ptr(proc);
  if (!mrb_obj_iv_defined(mrb, p, MRB_SYM(_owner))) return FALSE;
  if (!mrb_obj_iv_defined(mrb, p, MRB_SYM(_recv))) return FALSE;
  if (!mrb_obj_iv_defined(mrb, p, MRB_SYM(_name))) return FALSE;
  if (!mrb_obj_iv_defined(mrb, p, MRB_SYM(_proc))) return FALSE;
  if (!mrb_obj_iv_defined(mrb, p, MRB_SYM(_klass))) return FALSE;
  return TRUE;
}

#define IV_GET(value, name) mrb_iv_get(mrb, value, name)
/*
 *  call-seq:
 *     method == other_method  -> true or false
 *     method.eql?(other_method)  -> true or false
 *
 *  Two method objects are equal if they are bound to the same
 *  object and refer to the same method definition and their owners are the
 *  same class or module.
 *
 *     a = "cat"
 *     b = "cat"
 *     p a.method(:upcase) == a.method(:upcase)    #=> true
 *     p a.method(:upcase) == b.method(:upcase)    #=> false
 */

static mrb_value
method_eql(mrb_state *mrb, mrb_value self)
{
  mrb_value other = mrb_get_arg1(mrb);
  mrb_value orig_proc, other_proc;

  if (!method_p(mrb, mrb_class(mrb, self), other))
    return mrb_false_value();

  if (mrb_class_ptr(IV_GET(self, MRB_SYM(_owner))) != mrb_class_ptr(IV_GET(other, MRB_SYM(_owner))))
    return mrb_false_value();

  if (!mrb_obj_equal(mrb, IV_GET(self, MRB_SYM(_recv)), IV_GET(other, MRB_SYM(_recv))))
    return mrb_false_value();

  orig_proc = IV_GET(self, MRB_SYM(_proc));
  other_proc = IV_GET(other, MRB_SYM(_proc));
  if (mrb_nil_p(orig_proc) && mrb_nil_p(other_proc) &&
      mrb_symbol(IV_GET(self, MRB_SYM(_name))) == mrb_symbol(IV_GET(other, MRB_SYM(_name)))) {
    return mrb_true_value();
  }
  if (mrb_nil_p(orig_proc) || mrb_nil_p(other_proc)) {
    return mrb_false_value();
  }
  return mrb_bool_value(mrb_proc_eql(mrb, orig_proc, other_proc));
}

#undef IV_GET

static mrb_value
mcall(mrb_state *mrb, mrb_value self, mrb_value recv)
{
  const struct RProc *proc = method_extract_proc(mrb, self);
  mrb_sym mid = method_extract_mid(mrb, self);
  struct RClass *tc = method_extract_owner(mrb, self);

  if (mrb_undef_p(recv)) {
    recv = method_extract_receiver(mrb, self);
  }
  else {
    bind_check(mrb, recv, mrb_obj_value(tc));
  }

  if (!proc) {
    proc = method_missing_prepare(mrb, &mid, recv, &tc);
  }
  mrb->c->ci->mid = mid;
  mrb->c->ci->u.target_class = tc;

  return mrb_exec_irep(mrb, recv, proc);
}

/*
 *  call-seq:
 *     method.call(args, ...)    -> obj
 *     method[args, ...]         -> obj
 *
 *  Invokes the method with the specified arguments, returning the
 *  method's return value.
 *
 *     m = 12.method("+")
 *     m.call(3)    #=> 15
 *     m.call(20)   #=> 32
 */

static mrb_value
method_call(mrb_state *mrb, mrb_value self)
{
  return mcall(mrb, self, mrb_undef_value());
}

/*
 *  call-seq:
 *     unbound_method.bind_call(obj, args, ...)  -> result
 *
 *  Bind unbound_method to obj and then invoke the method with the
 *  specified arguments. This is semantically equivalent to
 *  unbound_method.bind(obj).call(args, ...).
 *
 *     class A
 *       def test
 *         puts "In A"
 *       end
 *     end
 *     class B < A
 *     end
 *     um = B.instance_method(:test)
 *     um.bind_call(B.new)
 *
 *  produces:
 *
 *     In A
 */

static mrb_value
method_bcall(mrb_state *mrb, mrb_value self)
{
  mrb_value recv = args_shift(mrb);
  mrb_gc_protect(mrb, recv);
  return mcall(mrb, self, recv);
}

/*
 *  call-seq:
 *     method.unbind    -> unbound_method
 *
 *  Dissociates method from its current receiver. The resulting
 *  UnboundMethod can subsequently be bound to a new object
 *  of the same class (see UnboundMethod).
 *
 *     class A
 *       def test
 *         puts "In A"
 *       end
 *     end
 *     a = A.new
 *     m = a.method(:test)
 *     um = m.unbind
 *     um.bind(A.new).call
 *
 *  produces:
 *
 *     In A
 */

static mrb_value
method_unbind(mrb_state *mrb, mrb_value self)
{
  mrb_value owner = mrb_iv_get(mrb, self, MRB_SYM(_owner));
  mrb_value name = mrb_iv_get(mrb, self, MRB_SYM(_name));
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));
  mrb_value klass = mrb_iv_get(mrb, self, MRB_SYM(_klass));

  struct RObject *ume = method_object_alloc(mrb, mrb_class_get_id(mrb, MRB_SYM(UnboundMethod)));
  mrb_obj_iv_set(mrb, ume, MRB_SYM(_owner), owner);
  mrb_obj_iv_set(mrb, ume, MRB_SYM(_recv), mrb_nil_value());
  mrb_obj_iv_set(mrb, ume, MRB_SYM(_name), name);
  mrb_obj_iv_set(mrb, ume, MRB_SYM(_proc), proc);
  mrb_obj_iv_set(mrb, ume, MRB_SYM(_klass), klass);

  return mrb_obj_value(ume);
}

static const struct RProc *
method_search_vm(mrb_state *mrb, struct RClass **cp, mrb_sym mid)
{
  mrb_method_t m = mrb_method_search_vm(mrb, cp, mid);
  if (MRB_METHOD_UNDEF_P(m))
    return NULL;
  if (MRB_METHOD_PROC_P(m))
    return MRB_METHOD_PROC(m);

  struct RProc *proc = mrb_proc_new_cfunc(mrb, MRB_METHOD_FUNC(m));
  if (MRB_METHOD_NOARG_P(m)) {
    proc->flags |= MRB_PROC_NOARG;
  }
  return proc;
}

/*
 *  call-seq:
 *     method.super_method  -> method
 *
 *  Returns a Method representing the method in the superclass
 *  of the method's class.  Returns nil if there is no
 *  superclass method.
 *
 *     class A
 *       def test
 *         puts "In A"
 *       end
 *     end
 *     class B < A
 *       def test
 *         puts "In B"
 *       end
 *     end
 *     obj = B.new
 *     obj.method(:test).super_method.call   #=> "In A"
 */

static mrb_value
method_super_method(mrb_state *mrb, mrb_value self)
{
  mrb_value recv = mrb_iv_get(mrb, self, MRB_SYM(_recv));
  mrb_value klass = mrb_iv_get(mrb, self, MRB_SYM(_klass));
  mrb_value owner = mrb_iv_get(mrb, self, MRB_SYM(_owner));
  mrb_value name = mrb_iv_get(mrb, self, MRB_SYM(_name));
  struct RClass *super, *rklass;

  if (mrb_type(owner) == MRB_TT_MODULE) {
    struct RClass *m = mrb_class_ptr(owner);
    rklass = mrb_class_ptr(klass)->super;
    while (rklass && rklass->c != m) {
      rklass = rklass->super;
    }
    if (!rklass) return mrb_nil_value();
    super = rklass->super;
  }
  else {
    super = mrb_class_ptr(owner)->super;
  }

  const struct RProc *proc = method_search_vm(mrb, &super, mrb_symbol(name));
  if (!proc) return mrb_nil_value();

  if (!super) return mrb_nil_value();
  super = mrb_class_real(super);

  struct RObject *me = method_object_alloc(mrb, mrb_obj_class(mrb, self));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_owner), mrb_obj_value(super));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_recv), recv);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_name), name);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_proc), mrb_obj_value((void*)proc));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_klass), mrb_obj_value(super));

  return mrb_obj_value(me);
}

/*
 *  call-seq:
 *     method.arity    -> integer
 *
 *  Returns an indication of the number of arguments accepted by a
 *  method. Returns a nonnegative integer for methods that take a fixed
 *  number of arguments. For Ruby methods that take a variable number of
 *  arguments, returns -n-1, where n is the number of required
 *  arguments. Keyword arguments will be considered as a single additional
 *  argument, that argument being mandatory if any keyword argument is
 *  mandatory. For methods written in C, returns -1 if the call takes a
 *  variable number of arguments.
 *
 *     class C
 *       def one;    end
 *       def two(a); end
 *       def three(*a);  end
 *       def four(a, b); end
 *       def five(a, b, *c);    end
 *       def six(a, b, *c, &d); end
 *     end
 *     c = C.new
 *     c.method(:one).arity     #=> 0
 *     c.method(:two).arity     #=> 1
 *     c.method(:three).arity   #=> -1
 *     c.method(:four).arity    #=> 2
 *     c.method(:five).arity    #=> -3
 *     c.method(:six).arity     #=> -3
 */

static mrb_value
method_arity(mrb_state *mrb, mrb_value self)
{
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));
  mrb_int arity = mrb_nil_p(proc) ? -1 : mrb_proc_arity(mrb_proc_ptr(proc));
  return mrb_fixnum_value(arity);
}

/*
 *  call-seq:
 *     method.source_location  -> [String, Integer] or nil
 *
 *  Returns the Ruby source filename and line number containing this method
 *  or nil if this method was not defined in Ruby (i.e. native).
 *
 *     def foo; end
 *     method(:foo).source_location   #=> ["test.rb", 1]
 *
 *  Note: You need to enable debug option in your build configuration to use
 *  this method.
 */

static mrb_value
method_source_location(mrb_state *mrb, mrb_value self)
{
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));

  if (mrb_nil_p(proc))
    return mrb_nil_value();

  return mrb_proc_source_location(mrb, mrb_proc_ptr(proc));
}

/*
 *  call-seq:
 *     method.parameters  -> array
 *
 *  Returns the parameter information of this method.
 *
 *     def foo(bar); end
 *     method(:foo).parameters #=> [[:req, :bar]]
 *
 *     def foo(bar, baz, *qux); end
 *     method(:foo).parameters #=> [[:req, :bar], [:req, :baz], [:rest, :qux]]
 *
 *     def foo(bar, baz, qux: 42); end
 *     method(:foo).parameters #=> [[:req, :bar], [:req, :baz], [:keyreq, :qux]]
 */

static mrb_value
method_parameters(mrb_state *mrb, mrb_value self)
{
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));

  if (mrb_nil_p(proc)) {
    mrb_value rest = mrb_symbol_value(MRB_SYM(rest));
    mrb_value arest = mrb_ary_new_from_values(mrb, 1, &rest);
    return mrb_ary_new_from_values(mrb, 1, &arest);
  }

  return mrb_proc_parameters(mrb, proc);
}

/*
 *  call-seq:
 *     method.to_s      -> string
 *     method.inspect   -> string
 *
 *  Returns the name of the underlying method.
 *
 *     "cat".method(:count).inspect   #=> "#<Method: String#count>"
 */

static mrb_value
method_to_s(mrb_state *mrb, mrb_value self)
{
  mrb_value owner = mrb_iv_get(mrb, self, MRB_SYM(_owner));
  mrb_value klass = mrb_iv_get(mrb, self, MRB_SYM(_klass));
  mrb_value name = mrb_iv_get(mrb, self, MRB_SYM(_name));
  mrb_value str = mrb_str_new_lit(mrb, "#<");
  mrb_value proc = mrb_iv_get(mrb, self, MRB_SYM(_proc));

  mrb_str_cat_cstr(mrb, str, mrb_obj_classname(mrb, self));
  mrb_str_cat_lit(mrb, str, ": ");
  if (mrb_type(owner) == MRB_TT_SCLASS) {
    mrb_value recv = mrb_iv_get(mrb, self, MRB_SYM(_recv));
    if (!mrb_nil_p(recv)) {
      mrb_str_concat(mrb, str, recv);
      mrb_str_cat_lit(mrb, str, ".");
      mrb_str_concat(mrb, str, name);
      goto finish;
    }
  }
  {
    struct RClass *ok = mrb_class_ptr(owner);
    struct RClass *rk = mrb_class_ptr(klass);
    struct RClass *rklass = mrb_class_real(rk); /* skip internal class */
    if (ok == rk || ok == rklass) {
      mrb_str_concat(mrb, str, owner);
      mrb_str_cat_lit(mrb, str, "#");
      mrb_str_concat(mrb, str, name);
    }
    else {
      mrb_str_concat(mrb, str, mrb_obj_value(rklass));
      mrb_str_cat_lit(mrb, str, "(");
      mrb_str_concat(mrb, str, owner);
      mrb_str_cat_lit(mrb, str, ")#");
      mrb_str_concat(mrb, str, name);
    }
  }
 finish:;
  if (!mrb_nil_p(proc)) {
    const struct RProc *p = mrb_proc_ptr(proc);
    if (MRB_PROC_ALIAS_P(p)) {
      mrb_sym mid;
      while (MRB_PROC_ALIAS_P(p)) {
        mid = p->body.mid;
        p = p->upper;
      }
      mrb_str_cat_lit(mrb, str, "(");
      mrb_str_concat(mrb, str, mrb_symbol_value(mid));
      mrb_str_cat_lit(mrb, str, ")");
    }
  }
  mrb_value loc = method_source_location(mrb, self);
  if (mrb_array_p(loc) && RARRAY_LEN(loc) == 2) {
    mrb_str_cat_lit(mrb, str, " ");
    mrb_str_concat(mrb, str, RARRAY_PTR(loc)[0]);
    mrb_str_cat_lit(mrb, str, ":");
    mrb_str_concat(mrb, str, RARRAY_PTR(loc)[1]);
  }
  mrb_str_cat_lit(mrb, str, ">");
  return str;
}

static mrb_bool
search_method_owner(mrb_state *mrb, struct RClass *c, mrb_value obj, mrb_sym name, struct RClass **owner, const struct RProc **proc, mrb_bool unbound)
{
  *owner = c;
  *proc = method_search_vm(mrb, owner, name);
  if (!*proc) {
    if (unbound) {
      return FALSE;
    }
    if (!mrb_respond_to(mrb, obj, MRB_SYM_Q(respond_to_missing))) {
      return FALSE;
    }
    mrb_value ret = mrb_funcall_id(mrb, obj, MRB_SYM_Q(respond_to_missing), 2, mrb_symbol_value(name), mrb_true_value());
    if (!mrb_test(ret)) {
      return FALSE;
    }
    *owner = c;
  }
  return TRUE;
}

static mrb_noreturn void
singleton_method_error(mrb_state *mrb, mrb_sym name, mrb_value obj)
{
  mrb_raisef(mrb, E_NAME_ERROR, "undefined singleton method '%n' for '%!v'", name, obj);
}

static mrb_value
method_alloc(mrb_state *mrb, struct RClass *c, mrb_value obj, mrb_sym name, mrb_bool unbound, mrb_bool singleton)
{
  struct RClass *owner;
  const struct RProc *proc;

  if (!search_method_owner(mrb, c, obj, name, &owner, &proc, unbound)) {
    if (singleton) {
      singleton_method_error(mrb, name, obj);
    }
    else {
      mrb_raisef(mrb, E_NAME_ERROR, "undefined method '%n' for class '%C'", name, c);
    }
  }
  if (singleton && (owner->tt != MRB_TT_SCLASS && owner->tt != MRB_TT_ICLASS)) {
    singleton_method_error(mrb, name, obj);
  }
  while ((owner)->tt == MRB_TT_ICLASS)
    owner = (owner)->c;

  struct RObject *me = method_object_alloc(mrb, mrb_class_get_id(mrb, unbound ? MRB_SYM(UnboundMethod) : MRB_SYM(Method)));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_owner), mrb_obj_value(owner));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_recv), unbound ? mrb_nil_value() : obj);
  mrb_obj_iv_set(mrb, me, MRB_SYM(_name), mrb_symbol_value(name));
  mrb_obj_iv_set(mrb, me, MRB_SYM(_proc), proc ? mrb_obj_value((void*)proc) : mrb_nil_value());
  mrb_obj_iv_set(mrb, me, MRB_SYM(_klass), mrb_obj_value(c));

  return mrb_obj_value(me);
}

/*
 *  call-seq:
 *     obj.method(sym)    -> method
 *
 *  Looks up the named method as a receiver in obj, returning a
 *  Method object (or raising NameError). The
 *  Method object acts as a closure in obj's object
 *  instance, so instance variables and the value of self
 *  remain available.
 *
 *     class Demo
 *       def initialize(n)
 *         @iv = n
 *       end
 *       def hello()
 *         "Hello, @iv = #{@iv}"
 *       end
 *     end
 *
 *     k = Demo.new(99)
 *     m = k.method(:hello)
 *     m.call   #=> "Hello, @iv = 99"
 *
 *     l = Demo.new('Fred')
 *     m = l.method("hello")
 *     m.call   #=> "Hello, @iv = Fred"
 */

static mrb_value
mrb_kernel_method(mrb_state *mrb, mrb_value self)
{
  mrb_sym name;

  mrb_get_args(mrb, "n", &name);
  return method_alloc(mrb, mrb_class(mrb, self), self, name, FALSE, FALSE);
}

/*
 *  call-seq:
 *     obj.singleton_method(sym)    -> method
 *
 *  Similar to method, searches singleton method only.
 *
 *     class Demo
 *       def initialize(n)
 *         @iv = n
 *       end
 *       def hello()
 *         "Hello, @iv = #{@iv}"
 *       end
 *     end
 *
 *     k = Demo.new(99)
 *     def k.hi
 *       "Hi, @iv = #{@iv}"
 *     end
 *     m = k.singleton_method(:hi)
 *     m.call   #=> "Hi, @iv = 99"
 *     m = k.singleton_method(:hello) #=> NameError
 */

static mrb_value
mrb_kernel_singleton_method(mrb_state *mrb, mrb_value self)
{
  mrb_sym name;

  mrb_get_args(mrb, "n", &name);

  struct RClass *c = mrb_class(mrb, self);
  return method_alloc(mrb, c, self, name, FALSE, TRUE);
}

/*
 *  call-seq:
 *     mod.instance_method(symbol)   -> unbound_method
 *
 *  Returns an UnboundMethod representing the given
 *  instance method in mod.
 *
 *     class Interpreter
 *       def do_a() print "there, "; end
 *       def do_d() print "Hello ";  end
 *       def do_e() print "!\n";     end
 *       def do_v() print "world";   end
 *     end
 *     Interpreter.instance_method(:do_a).bind(Interpreter.new).call
 *     Interpreter.instance_method(:do_d).bind(Interpreter.new).call
 *     Interpreter.instance_method(:do_v).bind(Interpreter.new).call
 *     Interpreter.instance_method(:do_e).bind(Interpreter.new).call
 *
 *  produces:
 *
 *     there, Hello world!
 */

static mrb_value
mrb_module_instance_method(mrb_state *mrb, mrb_value self)
{
  mrb_sym name;

  mrb_get_args(mrb, "n", &name);
  return method_alloc(mrb, mrb_class_ptr(self), self, name, TRUE, FALSE);
}

/*
 *  call-seq:
 *     method.owner    -> class_or_module
 *
 *  Returns the class or module that defines the method.
 *
 *     (1..3).method(:map).owner #=> Enumerable
 */

static mrb_value
method_owner(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, MRB_SYM(_owner));
}

/*
 *  call-seq:
 *     method.receiver    -> object
 *
 *  Returns the bound receiver of the method.
 *
 *     "hello".method(:upcase).receiver  #=> "hello"
 */

static mrb_value
method_receiver(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, MRB_SYM(_recv));
}

/*
 *  call-seq:
 *     method.name    -> symbol
 *
 *  Returns the name of the method.
 *
 *     "hello".method(:upcase).name  #=> :upcase
 */

static mrb_value
method_name(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, MRB_SYM(_name));
}

void
mrb_mruby_method_gem_init(mrb_state* mrb)
{
  struct RClass *unbound_method = mrb_define_class_id(mrb, MRB_SYM(UnboundMethod), mrb->object_class);
  struct RClass *method = mrb_define_class_id(mrb, MRB_SYM(Method), mrb->object_class);

  MRB_SET_INSTANCE_TT(unbound_method, MRB_TT_OBJECT);
  MRB_UNDEF_ALLOCATOR(unbound_method);
  mrb_undef_class_method_id(mrb, unbound_method, MRB_SYM(new));
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(bind), unbound_method_bind, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(super_method), method_super_method, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_OPSYM(eq), method_eql, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, unbound_method, MRB_SYM_Q(eql), method_eql, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(to_s), method_to_s, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(inspect), method_to_s, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(arity), method_arity, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(source_location), method_source_location, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(parameters), method_parameters, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(bind_call), method_bcall, MRB_ARGS_REQ(1)|MRB_ARGS_ANY());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(owner), method_owner, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, unbound_method, MRB_SYM(name), method_name, MRB_ARGS_NONE());

  MRB_SET_INSTANCE_TT(method, MRB_TT_OBJECT);
  MRB_UNDEF_ALLOCATOR(method);
  mrb_undef_class_method_id(mrb, method, MRB_SYM(new));
  mrb_define_method_id(mrb, method, MRB_OPSYM(eq), method_eql, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, method, MRB_SYM_Q(eql), method_eql, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, method, MRB_SYM(to_s), method_to_s, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(inspect), method_to_s, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(call), method_call, MRB_ARGS_ANY());
  mrb_define_method_id(mrb, method, MRB_OPSYM(aref), method_call, MRB_ARGS_ANY());
  mrb_define_method_id(mrb, method, MRB_SYM(unbind), method_unbind, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(super_method), method_super_method, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(arity), method_arity, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(source_location), method_source_location, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(parameters), method_parameters, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(owner), method_owner, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(receiver), method_receiver, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, method, MRB_SYM(name), method_name, MRB_ARGS_NONE());

  mrb_define_method_id(mrb, mrb->kernel_module, MRB_SYM(method), mrb_kernel_method, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, mrb->kernel_module, MRB_SYM(singleton_method), mrb_kernel_singleton_method, MRB_ARGS_REQ(1));

  mrb_define_method_id(mrb, mrb->module_class, MRB_SYM(instance_method), mrb_module_instance_method, MRB_ARGS_REQ(1));
}

void
mrb_mruby_method_gem_final(mrb_state* mrb)
{
}
