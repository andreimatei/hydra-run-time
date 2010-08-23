from ..visitors import DefaultVisitor, flatten
from ..ast import CVarUse, CVarSet

class ReduceGetA(DefaultVisitor):
      def visit_geta(self, geta):
          return CVarUse(loc = geta.loc, decl = geta.decl.cvar)
            
      def visit_getmema(self, getma):
          return CVarSet(loc = getma.loc, 
                         decl = getma.lhs_decl.cvar_stub, 
                         rhs = CVarUse(decl = getma.decl.mem_decl.cvar_stub))

__all__ = ['ReduceGetA']
