

from ..visitors import ScopedVisitor, DefaultVisitor, flatten
from ..ast import *

class Create_2_LowCreate(DefaultVisitor):

    def visit_create(self, cr):

        lc = LowCreate(loc = cr.loc,
                       loc_end = cr.loc_end,
                       label = cr.label)
        
        newbl = []
        decls = cr.scope.decls

        cr.cvar_exitcode = CVarDecl(loc = cr.loc_end, name = 'C$R$%s' % cr.label, ctype = 'long')
        decls += cr.cvar_exitcode
        cr.cvar_fid = CVarDecl(loc = cr.loc, name = 'C$F$%s' % cr.label, ctype = 'long')
        decls += cr.cvar_fid

        for item in ('place', 'start', 'limit', 'step', 'block'):
            var = None
            if item != 'place':
                var = CVarDecl(loc = cr.loc, name = 'C$%s$%s' % (item, cr.label), ctype = 'long')
            else:
                var = CVarDecl(loc = cr.loc, name = 'C$%s$%s' % (item, cr.label), ctype = 'sl_place_t')
            decls += var

            setattr(cr, 'cvar_%s' % item, var)

            newbl.append(CVarSet(loc = cr.loc, decl = var, rhs = getattr(cr, item)) + ';')
            
        if cr.funtype == cr.FUN_OPAQUE:
            cr.funtype = cr.FUN_VAR
            var = CVarDecl(loc = cr.loc, name = 'C$F$%s' % cr.label, ctype = 'void *')
            decls += var
            newbl.append(CVarSet(loc = cr.loc, decl = var, 
                                 rhs = CCast(ctype = 'void *',
                                             expr = cr.fun.accept(self))) + ';')
            cr.fun = var
        else:
            assert cr.funtype == cr.FUN_ID

        newbody = Block(loc = cr.body.loc, loc_end = cr.body.loc_end)
        for a in cr.args:
            if isinstance(a, CreateArgMem):
                argdecl = MemDef(name = 'Cai%s' % a.name) # TODO: I had to remove the '$'s because they didn't match some regular exp...?
                argdecl.set_op = SetMemA()  # so that the next visitor only generate a stub definition, no descriptor
                argdecl.scope = cr.scope
                decls += argdecl
                a.mem_decl = argdecl
                
                if a.rhs is not None:
                    setma = SetMemA(loc = cr.loc, name = a.name, rhs = a.rhs)
                    setma.decl = a
                    assert(cr.scope.mem_dic[a.rhs] is not None)  # TODO: replace with an error message
                    setma.rhs_decl = cr.scope.mem_dic[a.rhs]
                    newbody += (Opaque(';') + setma)
            else:
                assert isinstance(a, CreateArg)
                argvar = CVarDecl(loc = a.loc, name = 'C$a$%s' % a.name, ctype = a.ctype)
                argvar.alignment = 8  # request 8-byte alignment; useful for the hrt target, for
                                           # shared arguments, as the sync will write to them
                                           # using a (long*).

                decls += argvar
                a.cvar = argvar

                if a.init is not None:
                    initvar = CVarDecl(loc = a.loc, name = 'C$ai$%s' % a.name, ctype = a.ctype)

                    decls += initvar
                    a.cvar_init = initvar

                    newbl.append(CVarSet(loc = a.loc, decl = initvar, 
                                         rhs = a.init.accept(self)) + ';')

                    seta = SetA(loc = a.loc, name = a.name, 
                                rhs = CVarUse(decl = initvar))
                    seta.decl = a
                    seta.scope = cr.scope
                    a.seen_set = True

                    newbody += (Opaque(';') + seta)

        if cr.fid_lvalue is not None:
            newbody += (flatten(cr.loc, "; (") + cr.fid_lvalue.accept(self) + ') = ' +
                        CVarUse(loc = cr.loc, decl = cr.cvar_fid))

        lc.body = newbody
        lc.body += cr.body.accept(self)

        newbl.append(lc)
        newbl.append(flatten(None, ';'))

        cr.target_resolved = CLabel(loc = cr.loc_end, name = 'Ce$%s' % cr.label) 
        newbl.append(cr.target_resolved)

        if cr.result_lvalue is not None:
            newbl.append(flatten(cr.loc_end, "; (") +
                         cr.result_lvalue.accept(self) +
                         ') = ' +
                         CVarUse(loc = cr.loc, decl = cr.cvar_exitcode))

        return newbl

class AutoResolve(ScopedVisitor):

    def visit_lowcreate(self, lc):
        cr = self.cur_scope.creates[lc.label]
        # FIXME(kena): andreim: why was the goto here? I removed it. Seems it was dubbled in split.py, plus it's missing the semicolon...?
        return lc + ';' #+ CGoto(loc = cr.loc_end, target = cr.target_resolved)

__all__ = ["Create_2_LowCreate", "AutoResolve"]
