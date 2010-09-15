
from dump import get_dump_fname
import opts
from ..visitors import *
from ..input.check import *
from ..lower.seq import *
from ..lower.lowercreate import *
from ..lower.split import *
from ..lower.lowgeta import *
from ..lower.lowcvars import *
from ..lower.lowccast import *
from ..lower.lowclabels import *
from ..lower.flavorseta import *
from ..lower.rename import *
from ..lower.remflavors import *
from ..mtalpha.visitors import *
from ..am.visitors import *

_common_prefix = [
    ('walk', DefaultVisitor()),
    ('check', CheckVisitor()),
    ('cr2lc', Create_2_LowCreate()),
    ]

_common_suffix = [
     ('geta', ReduceGetA()),
     ('rnlbls', RenameCLabels()),
     ('rnvars', RenameCVars()),
     ('cvars', ReduceCVars()),
     ('ccast', ReduceCCast()),
     ('clabels', ReduceCLabels()),
     ('remflavors', RemoveFlavors()),
]

_chains = {
    'seq' : _common_prefix + [
        ('lseta', LinkSetA()),
        ('autores', AutoResolve()),
        ('flattencr',Create_2_Loop()),
        ('flattenfun',TFun_2_CFun()),
        ] + _common_suffix,
    # FIXME: after the build errors are corrected, swap hrt with hrt_old. hrt_old is a copy of old hrt (naked), and hrt is where development happens for hybrid.
    'hrt' : _common_prefix + [
        ('flatten_mem', Mem_2_HRT()),
        #('test1', Test_Visitor()),
        #('sep1', Separator()),
        ('splitcr', SplitCreates()),
        #('test2', Test_Visitor()),
        #('sep2', Separator()),
        ('lseta', LinkSetA()),
        ('autores', AutoResolve()),
        ('splitfun', SplitFuns()),
        #('test3', Test_Visitor()),
        #('flatten_mem', ScopedVisitor(Dispatcher({'fmta':Mem_2_HRT(),
        #                                          'fseq':Mem_2_Seq()}))),
        ('flattencr', ScopedVisitor(Dispatcher({'cmta':Create_2_HydraCall(),
                                                'cseq':Create_2_Loop()}))),
        ('flattenfun', ScopedVisitor(Dispatcher({'fmta':TFun_2_HydraCFunctions(),
                                                 'fseq':TFun_2_CFun()}))),
        ] + _common_suffix,
    'hrt-old' : _common_prefix + [
        ('lseta', LinkSetA()),
        ('autores', AutoResolve()),
        ('flatten_mem', Mem_2_HRT()),
        ('flattencr',Create_2_HydraCall()),
        ('flattenfun',TFun_2_HydraCFunctions()),
        ] + _common_suffix,
    'mta' : _common_prefix + [
        ('lseta', LinkSetA()),
        ('autores', AutoResolve()),
        ('flattencr', Create_2_MTACreate()), 
        ('flattenfun',TFun_2_MTATFun()),
        ] + _common_suffix,
    'mta+seq' : _common_prefix + [
        ('splitcr', SplitCreates()),
        ('lseta', LinkSetA()),
        ('flseta', FlavorSetA()),
        ('splitfun', SplitFuns()),
        ('flattencr', ScopedVisitor(Dispatcher({'cmta':Create_2_MTACreate(),
                                                'cseq':Create_2_Loop()}))),
        ('flattenfun', DefaultVisitor(Dispatcher({'fmta':TFun_2_MTATFun(),
                                                  'fseq':TFun_2_CFun()}))),
        ] + _common_suffix
}

_default_chain = 'seq'

for k,v in _chains.items():
    for (a, b) in v:
        opts.register_dump_stage(a)

opts.register_arg('-b', action = "store", nargs = 1, dest = "target",
                  choices = _chains.keys(), default = _default_chain,
                  help="Select translation target.")

def run_chain(program):
    t = _chains[opts.resolved.target]
    for (lbl, visitor) in t:
        #print "XX, ", lbl
        program.accept(visitor)
        
        dname = get_dump_fname(lbl)
        if dname is not None:
            f = file(dname, 'w')
            program.accept(PrintVisitor(stream = f))
            f.close()
    
__all__ = ['run_chain']
