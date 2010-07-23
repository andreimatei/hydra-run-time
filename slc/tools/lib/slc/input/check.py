from ..visitors import DefaultVisitor
from ..msg import log, warn
from ..ast import *
import sys

class CheckVisitor(DefaultVisitor):

    def err(self, msg, context = None):
        log('error: %s' % msg, context)
        self.has_errors = True

    def __init__(self, *args, **kwargs):
        super(CheckVisitor, self).__init__(*args, **kwargs)

        self.save_stack = []

        self.has_errors = False

        self.cur_create = None
        self.cur_scope = None
        self.cur_fun = None
        self.in_parm_list = False
        self.in_arg_list = False

    def visit_program(self, p):
        super(CheckVisitor, self).visit_program(p)
        if self.has_errors:
            sys.exit(1)

    def enter(self, **kwargs):       
        save = {}
        for k in kwargs:
            save[k] = getattr(self, k)
        self.save_stack.insert(0, save)
        self.__dict__.update(kwargs)

    def leave(self):
        assert len(self.save_stack) > 0
        save = self.save_stack.pop(0)
        self.__dict__.update(save)

    def visit_funparm(self, parm):
        if not self.in_parm_list:
            self.err("thread parameter declaration outside of parameter list", parm)
            return parm

        assert self.cur_fun is not None

        pdic = self.cur_fun.parm_dic
        if parm.name in pdic:
            self.err("redefinition of parameter '%s'" % parm.name, parm)
            self.err("  previous definition of '%s' was here" % parm.name, pdic[parm.name])
            return parm

        # cross-link for later
        parm.fun = self.cur_fun
        pdic[parm.name] = parm

        return parm

    def visit_funparmmem(self, parm):
        p = self.visit_funparm(parm)
        p.kind.accept(self)
        return p

    def visit_fundecl(self, fd):
        fd.parm_dic = {}

        self.enter(cur_fun = fd, in_parm_list = True)
        for p in fd.parms:
            p.accept(self)
        self.leave()

    def visit_fundeclptr(self, fd):
        fd.parm_dic = {}

        self.enter(cur_fun = fd, in_parm_list = True)
        for p in fd.parms:
            p.accept(self)
        self.leave()

    def visit_fundef(self, fd):
        if self.cur_fun is not None:
            self.err("cannot nest thread function definition", fd)
            self.err("  in function definition starting here", self.cur_fun)
            return fd
        
        self.dispatch(fd, seen_as = FunDecl)
        
        self.enter(cur_fun = fd)
        fd.body.accept(self)
        self.leave()

        # some checks on parameters
        for p in fd.parms:
            if not p.seen_set and not p.seen_get:
                warn("unused thread parameter '%s'" % p.name, fd)
            else:
                if not p.seen_set and p.type.startswith('sh'):
                    warn("shared parameter '%s' is read but not written to" % p.name, fd)
                if not p.seen_get and p.type.startswith('sh'):
                    warn("shared parameter '%s' is written but not read from" % p.name, fd)
                        
    def visit_block(self, block):
        self.enter(in_parm_list = False)
        for item in block:
            item.accept(self)
        self.leave()

    def visit_break(self, br):
        if self.cur_fun is None or self.cur_scope is None:
            self.err("'break' outside of thread function body", br)
        if self.cur_create is not None:
            self.err("'break' inside of 'create' construct", br)
        br.fun = self.cur_fun
        return br

    def visit_endthread(self, et):
        if self.cur_fun is None or self.cur_scope is None:
            self.err("'end thread' outside of thread function body", et)
        if self.cur_create is not None:
            self.err("'end thread' inside of 'create' construct", et)
        et.fun = self.cur_fun
        return et

    def visit_indexdecl(self, idecl):
        if self.cur_fun is None or self.cur_scope is None:
            self.err("index declaration outside of thread function body", idecl)
        idecl.fun = self.cur_fun
        idecl.scope = self.cur_scope
        return idecl

    def visit_scope(self, scope):
        scope.arg_dic = {}
        scope.local_args = []
        scope.hot_args = set()
        scope.mem_dic = {}
        scope.local_mems = []

        if self.cur_scope is not None:
            scope.hot_args.update(self.cur_scope.hot_args)
            scope.arg_dic.update(self.cur_scope.arg_dic)
            scope.mem_dic.update(self.cur_scope.mem_dic)

        self.enter(cur_scope = scope)
        self.dispatch(scope, seen_as = Block)
        self.leave()

        return scope

    def visit_getsetp_gen(self, p):

        if self.cur_fun is None:
            self.err("access to thread parameter outside of thread function definition", p)
            return p

        if p.name not in self.cur_fun.parm_dic:
            self.err("thread parameter '%s' undeclared (first use; reported only once)" % p.name, p)
            self.cur_fun.parm_dic[p.name] = None
            return p

        p.decl = self.cur_fun.parm_dic[p.name]
        p.fun = self.cur_fun

        if hasattr(p, 'rhs'):
            if self.cur_fun.parm_dic[p.name].type.startswith('gl'):
                self.err("invalid write to global thread parameter '%s'" % p.name, p)
            if isinstance(p.rhs, Block):
                p.rhs.accept(self)
            p.decl.seen_set = True
        else:
            p.decl.seen_get = True

        return p

    def visit_getsetp(self, p):
        p = self.visit_getsetp_gen(p)
        if not isinstance(p.decl, FunParm):
            self.err("invalid getp/setp on memory argument '%s' (use getmp/setmp)" % p.name, p)
        return p

    visit_getp = visit_getsetp
    visit_setp = visit_getsetp

    def visit_getsetmp(self, p):
        p = self.visit_getsetp_gen(p)
        if not isinstance(p.decl, FunParmMem):
            self.err("invalid memory use of scalar parameter '%s' (use getp/setp)" % p.name, p)
        if hasattr(p, 'lhs'):
            p.lhs_decl = self.check_memwrite(p.lhs, p)
        if hasattr(p, 'rhs'):
            p.rhs_decl = self.check_memuse(p.rhs, p)
        return p

    visit_getmemp = visit_getsetmp
    visit_setmemp = visit_getsetmp

    def visit_getseta_gen(self, p, DefA = CreateArg):
        if self.cur_scope is None:
            self.err("access to create argument outside of C block", p)
            return p

        adic = self.cur_scope.arg_dic
        if p.name not in adic:
            self.err("create argument '%s' undeclared (first use in this scope; reported only once)" % p.name, p)
            a = DefA(loc = p.loc, name = p.name)
            a.scope = self.cur_scope
            adic[p.name] = a
            self.cur_scope.local_args.append(p.name)
            p.decl = a
            p.scope = self.cur_scope
            return p

        p.decl = adic[p.name]
        p.scope = p.decl.scope

        if hasattr(p, 'rhs'):
            if not p.name in p.scope.hot_args:
                self.err("write to create argument '%s' outside of create construct" % p.name, p)
                self.err("  after create construct starting here", p.decl.create)
                self.err("  after create construct ending here", p.decl.create.loc_end)
            if isinstance(p.rhs, Block):
                p.rhs.accept(self)
            p.decl.seen_set = True
        else:
            if p.name in p.scope.hot_args:
                self.err("read from create argument '%s' inside create construct" % p.name, p)
                self.err("  in create construct starting here", p.decl.create)
                self.err("  in create construct ending here", p.decl.create.loc_end)
            else:
                # FIXME: check that geta does not occur after detach in a continuation create
                pass
            p.decl.seen_get = True

        return p

    def visit_getseta(self, p):
        p = self.visit_getseta_gen(p)
        if not isinstance(p.decl, CreateArg):
            self.err("invalid geta/seta on memory argument '%s' (use getma/setma)" % p.name, p)
        return p

    visit_geta = visit_getseta
    visit_seta = visit_getseta

    def visit_createarg(self, arg):
        if not self.in_arg_list:
            self.err("create argument declaration outside of argument list", arg)
            return arg

        assert self.cur_create is not None
        assert self.cur_scope is not None

        scope = self.cur_scope

        sadic = scope.arg_dic
        sal = scope.local_args
        if arg.name in sal:
            self.err("redefinition of create argument '%s'" % arg.name, arg)
            self.err("previous definition of '%s' was here" % arg.name, sadic[arg.name])
            return arg
        
        # cross-link for later
        arg.create = self.cur_create
        arg.scope = scope
        scope.hot_args.add(arg.name)
        sal.append(arg.name)
        sadic[arg.name] = arg
        self.cur_create.arg_dic[arg.name] = arg

        return arg

    def visit_createargmem(self, arg):
        a = self.visit_createarg(arg)
        a.kind.accept(self)
        return a

    def visit_create(self, c):
        if self.cur_scope is None:
            self.err("'create' outside of C block", c)
            return c

        c.scope = self.cur_scope

        if c.label in c.scope.creates:
            self.err("'create' with label %s already in scope" % c.label)
            self.err("previous definition of %s was here" % c.label, c.scope.creates[c.label])
            return c
        c.scope.creates[c.label] = c

        for b in (c.place, c.start, c.step, c.limit, c.block):
            b.accept(self)

        if c.funtype == c.FUN_OPAQUE:
            c.fun.accept(self)

        # preprocess initializers before
        # defining arguments, so that they can reference upper
        # level declarations
        for a in c.args:
            if hasattr(a, 'init') and a.init is not None:
                # scalar initializer
                assert isinstance(a, CreateArg)
                a.init.accept(self)
                a.seen_set = True
            if hasattr(a, 'rhs') and a.rhs is not None:
                # memory initializer
                assert isinstance(a, CreateArgMem)
                a.rhs_decl = self.check_memuse(a.rhs, a)
                a.seen_set = True
                a.getset_mode = a.GETSET

        c.arg_dic = {}

        self.enter(cur_create = c, in_arg_list = True)
        for a in c.args:
            a.accept(self)
        self.leave()

        self.enter(cur_create = c)
        c.body.accept(self)
        self.leave()
        
        for a in c.args:
            c.scope.hot_args.discard(a.name)
            if not a.seen_set:
                warn("create argument '%s' not initialized" % a.name, c.loc_end)
                warn("in create construct starting here", c.loc) 
            

        if c.result_lvalue is not None:
            c.result_lvalue.accept(self)

        return c

    def check_memuse(self, rhs, ctx):
        mdic = self.cur_scope.mem_dic
        if rhs not in mdic:
            self.err("memory object '%s' undeclared (first use in this scope; reported only once)" % rhs, ctx)
            m = MemDef(loc = ctx.loc, name = rhs)
            m.scope = self.cur_scope
            mdic[rhs] = m
            return m

        m = mdic[rhs]
        return m

    def check_memwrite(self, lhs, ctx):
        m = self.check_memuse(lhs, ctx)

        if m.set_op is None:
            m.set_op = ctx
        elif not isinstance(m.set_op, MemExtend):
            if isinstance(ctx, m.set_op.__class__):
                warn("possible duplicate initialization of '%s'" % lhs, ctx)
                warn("  previous initialization was here", m.set_op)
            else:
                self.err("incompatible initialization of memory object '%s'" % lhs, ctx)
                self.err("  previous initialization was here", m.set_op)            
        return m

    def visit_usema(self, p):
        p = self.visit_getseta_gen(p, CreateArgMem)
        if not isinstance(p.decl, CreateArgMem):
            self.err("invalid memory use of scalar argument '%s' (use geta/seta)" % p.name, p)
        if hasattr(p, 'lhs'):
            p.lhs_decl = self.check_memwrite(p.lhs, p)
        if hasattr(p, 'rhs'):
            p.rhs_decl = self.check_memuse(p.rhs, p)
        return p

    def visit_getsetma(self, p):
        p = self.visit_usema(p)
        if p.decl.getset_mode is not None and p.decl.getset_mode != p.decl.GETSET:
            self.err("get/set on argument '%s' incompatible with previous scatter/gather" % p.name, p)
        p.decl.getset_mode = p.decl.GETSET
        return p

    visit_getmema = visit_getsetma
    visit_setmema = visit_getsetma

    def visit_scattergather(self, p):
        p = self.visit_usema(p)
        if p.decl.getset_mode is not None and p.decl.getset_mode != p.decl.SCATTERGATHER:
            self.err("gather on argument '%s' incompatible with previous setma/getma" % p.name, p)
        p.decl.getset_mode = p.decl.SCATTERGATHER
        p.a.accept(self)
        p.b.accept(self)
        p.c.accept(self)
        return p
    
    visit_gatheraffine = visit_scattergather
    visit_scatteraffine = visit_scattergather

    def visit_memactivate(self, a):
        if a.lhs is not None:
            a.lhs_decl = self.check_memwrite(a.lhs, a)
        a.rhs_decl = self.check_memuse(a.rhs, a)
        return a

    def visit_mempropagate(self, p):
        p.rhs_decl = self.check_memuse(p.rhs, p)
        return p

    def visit_memextend(self, me):
        me.lhs_decl = self.check_memwrite(me.lhs, me)
        me.rhs_decl = self.check_memuse(me.rhs, me)
        return me

    def visit_memrestrict(self, mr):
        mr.lhs_decl = self.check_memwrite(mr.lhs, mr)
        mr.rhs_decl = self.check_memuse(mr.rhs, mr)
        mr.offset.accept(self)
        mr.size.accept(self)
        return mr

    def visit_memalloc(self, ma):
        ma.lhs_decl = self.check_memwrite(ma.lhs, ma)
        ma.kind.accept(self)
        return ma
    
    def visit_memdesc(self, md):
        md.lhs_decl = self.check_memwrite(md.lhs, md)
        md.kind.accept(self)
        md.cptr.accept(self)
        return md

    def visit_memdef(self, md):
        assert self.cur_scope is not None

        scope = self.cur_scope

        mdic = scope.mem_dic
        ml = scope.local_mems
        if md.name in ml:
            self.err("redefinition of memory object '%s'" % md.name, md)
            self.err("previous definition of '%s' was here" % md.name, mdic[md.name])
            return md
        
        # cross-link for later
        md.scope = scope
        ml.append(md.name)
        mdic[md.name] = md

        return md
        

# FIXME: need extra visitor to check kind coherency:
# - extend only applies to multiarray
# - desc only applies to arrays
# - restrict only applies to arrays

__all__ = ['CheckVisitor']

