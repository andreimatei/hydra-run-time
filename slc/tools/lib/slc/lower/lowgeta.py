from ..visitors import DefaultVisitor, flatten, Block
from ..ast import CVarUse, CVarSet

class ReduceGetA(DefaultVisitor):
      def visit_geta(self, geta):
          return CVarUse(loc = geta.loc, decl = geta.decl.cvar)
            
      def visit_getmema(self, getma):
          #print 'in visit_getmema: getma.decl.mem_decl: type = %s id = %d' % (getma.decl.mem_decl.__class__.__name__,
          #        id(getma.decl.mem_decl))
          return CVarSet(loc = getma.loc, 
                         decl = getma.lhs_decl.cvar_stub, 
                         rhs = CVarUse(decl = getma.decl.mem_decl.cvar_stub))
          
      def visit_gatheraffine(self, gather):
        # FIXME: this doesn't work if the create took the sequential path

        a = gather.a
        b = gather.b
        c = gather.c
        create = gather.decl.create
        fam_context_var = create.fam_context_var
        #print ' id of gather.decl.create = ' + str(id(create))
        #print ' id of gather.decl = ' + str(id(gather.decl))

        fam_context = create.fam_context
        cvar_stub = create.scatter_stubs[gather.name]
        new_items = Block()

        # test whether the create path taken at runtime was the regular one (as opposed to the sequential one)
        # we do this in a less than very elegant way, by testing the fam_context var.
        # If the regular path wasn't taken, there's nothing for gather to do.
        # TODO: switch to a more elegant way of figuring out which create path was chosen.
        new_items += (flatten(gather.loc, 'if (')
                     + CVarUse(decl = fam_context_var) + ' != 0) {' 
                    )
        new_items += (flatten(gather.loc, '')
                + '_memgather_affine('
                + CVarUse(decl = fam_context) + ', '
                + 'get_stub_pointer(' + CVarUse(decl = cvar_stub) + ')->ranges[0], '
                + a + ', ' + b + ', ' + c 
                + ', ' + create.start
                + ', ' + create.step
                + ');'
                + '}'  # closing bracket for the if from above

                )
        return new_items

__all__ = ['ReduceGetA']
