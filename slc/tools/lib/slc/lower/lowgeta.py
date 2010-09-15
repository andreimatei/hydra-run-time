from ..visitors import DefaultVisitor, flatten, Block
from ..ast import CVarUse, CVarSet

class ReduceGetA(DefaultVisitor):
      def visit_geta(self, geta):
          return CVarUse(loc = geta.loc, decl = geta.decl.cvar)
            
      def visit_getmema(self, getma):
          # FIXME: check this fun. make sure it works even if the create took the sequential path
          return CVarSet(loc = getma.loc, 
                         decl = getma.lhs_decl.cvar_stub, 
                         rhs = CVarUse(decl = getma.decl.mem_decl.cvar_stub))
          
      def visit_gatheraffine(self, gather):
        # FIXME: this doesn't work if the create took the sequential path

        a = gather.a
        b = gather.b
        c = gather.c
        create = gather.decl.create
        print ' id of gather.decl.create = ' + str(id(create))
        print ' id of gather.decl = ' + str(id(gather.decl))

        create = gather.decl.create
        fam_context = gather.decl.create.fam_context
        cvar_stub = gather.decl.create.scatter_stubs[gather.name]
        new_items = Block()
        new_items += (flatten(gather.loc, '')
                + '_memgather_affine('
                + CVarUse(decl = fam_context) + ', '
                + 'get_stub_pointer(' + CVarUse(decl = cvar_stub) + ')->ranges[0], '
                + a + ', ' + b + ', ' + c 
                + ', ' + create.start
                + ', ' + create.step
                + ')'

                )
        return new_items

__all__ = ['ReduceGetA']
