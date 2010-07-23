from ..msg import die
from ..ast import *

def unexpected(item):
      die("unexpected construct '%s'" % item.get('type','unknown'), item)


def parse_varuse(varuse, item):
      #print "parse varuse %x: item %x: %r" % (id(varuse), id(item), item)
      varuse.loc = item['loc']
      varuse.name = item['name'].strip()
      if item.has_key('rhs'):
            varuse.rhs = parse_block(item['rhs'])
      return varuse

def parse_memvaruse(VarUseClass, item):
      #print "parse varuse %x: item %x: %r" % (id(varuse), id(item), item)
      varuse = VarUseClass(loc = item['loc'], name = item['name'])
      if item.has_key('rhs'):
            varuse.rhs = item['rhs'].strip()
      if item.has_key('lhs'):
            varuse.lhs = item['lhs'].strip()
      return varuse

def parse_create(item):
      c = Create(loc = item['loc'],
                 loc_end = item['loc_end'],
                 label = item['lbl'],
                 place = parse_block(item['place']),
                 start = parse_block(item['start']),
                 limit = parse_block(item['limit']),
                 step = parse_block(item['step']),
                 block = parse_block(item['block']),
                 mapping = parse_extras(item['mapping']),
                 extras = parse_extras(item['extras']),
                 sync_type = item['sync'],
                 fun = parse_block(item['fun']),
                 body = parse_block(item['body']))
                 
      for p in item['args']:
            if p['type'].endswith('_mem'):
                  c.args.append(parse_argparm_mem(CreateArgMem(), 'arg', p))
            else:
                  c.args.append(parse_argparm(CreateArg(), 'arg', p))
                  
      if 'fid' in item and item['fid']:
            c.fid_lvalue = parse_block(item['fid'])
      if 'result' in item and item['result']:
            c.result_lvalue = parse_block(item['result'])

      return c

def parse_indexdecl(item):
      return IndexDecl(loc = item['loc'],
                       indexname = item['name'].strip())

def parse_scope(item):
      s = Scope(loc = item['loc'],
                loc_end = item['loc_end'])
      s += parse_block(item['body'])
      return s
  
def parse_attr(item):
      n = item['name'].strip()
      del item['type']
      del item['name']

      for k in item:
            if type(item[k]) == str:                  
               item[k] = item[k].strip()

      return Attr(name = n, 
                  payload = item)

def parse_extras(items):
      if len(items) == 0:
            return None
      b = Extras()
      for item in items:
            if isinstance(item, dict):
                  t = item['type']
                  if t == 'attr': b += parse_attr(item)
                  else: unexpected(item)
            else:
                  assert isinstance(item, str)
                  # ignore strings
      if len(b) > 0:
            return b
      return None

def parse_kind(item):
      if item['type'] != 'memkind': 
            unexpected(item)
      itemtype = None
      if 'itemtype' in item:
            itemtype = parse_block(item['itemtype'])
      return MemKind(loc = item['loc'],
                     name = item['name'],
                     itemtype = itemtype,
                     size = parse_block(item['size']))
                     

def parse_mem(item):
      return MemDef(name = item['name'], loc = item['loc'])

def parse_memalloc(item):
      return MemAlloc(loc = item['loc'], lhs = item['name'], 
                      kind = parse_kind(item['kind']))

def parse_memdesc(item):
      return MemDesc(loc = item['loc'],
                     lhs = item['name'],
                     kind = parse_kind(item['kind']),
                     cptr = parse_block(item['rhs']))

def parse_memrestrict(item):
      return MemRestrict(loc = item['loc'],
                         lhs = item['name'],
                         rhs = item['rhs'],
                         offset = parse_block(item['offset']),
                         size = parse_block(item['size']))

def parse_memextend(item):
      return MemExtend(loc = item['loc'],
                       lhs = item['name'],
                       rhs = item['rhs'])

def parse_activate(item):
      return MemActivate(loc = item['loc'],
                         lhs = item['lhs'],
                         rhs = item['rhs'])

def parse_propagate(item):
      return MemPropagate(loc = item['loc'],
                          rhs = item['name'],
                          dir = item['dir'])

def parse_gather_affine(item):
      return GatherAffine(loc = item['loc'],
                          name = item['name'],
                          a = parse_block(item['a']),
                          b = parse_block(item['b']),
                          c = parse_block(item['c']))

def parse_scatter_affine(item):
      return ScatterAffine(loc = item['loc'],
                           name = item['name'],
                           rhs = item['rhs'],
                           a = parse_block(item['a']),
                           b = parse_block(item['b']),
                           c = parse_block(item['c']))

def parse_block(items):
      if len(items) == 0:
            return None
      b = Block()
      #print "new block %x (len %d)" % (id(b), len(b))
      for item in items:
            #print "parse block %x (len %d): item %x: %r" % (id(b), len(b), id(item), item)
            if isinstance(item, dict):
                  t = item['type']
                  if t == 'indexdecl': b += parse_indexdecl(item)
                  elif t == 'mem': b += parse_mem(item)
                  elif t == 'memalloc': b += parse_memalloc(item)
                  elif t == 'memdesc': b += parse_memdesc(item)
                  elif t == 'memrestrict': b += parse_memrestrict(item)
                  elif t == 'memextend': b += parse_memextend(item)
                  elif t == 'memactivate': b += parse_activate(item)
                  elif t == 'mempropagate': b += parse_propagate(item)
                  elif t == 'memgather_affine': b += parse_gather_affine(item)
                  elif t == 'memscatter_affine': b += parse_scatter_affine(item)
                  elif t == 'getp': b += parse_varuse(GetP(), item)
                  elif t == 'setp': b += parse_varuse(SetP(), item)
                  elif t == 'geta': b += parse_varuse(GetA(), item)
                  elif t == 'seta': b += parse_varuse(SetA(), item)
                  elif t == 'getmp': b += parse_memvaruse(GetMemP, item)
                  elif t == 'setmp': b += parse_memvaruse(SetMemP, item)
                  elif t == 'getma': b += parse_memvaruse(GetMemA, item)
                  elif t == 'setma': b += parse_memvaruse(SetMemA, item)
                  elif t == 'create': b += parse_create(item)
                  elif t == 'break': b += parse_break(item)
                  elif t == 'end_thread': b += parse_end_thread(item)
                  elif t == 'decl_funptr': b += parse_funptrdecl(item)
                  elif t == 'scope': b += parse_scope(item)
                  else: unexpected(item)
            else: 
                  assert isinstance(item, str)
                  csp = item.strip(' \t')
                  if len(csp) > 0:
                        b += Opaque(item)
            #print "parse block %x: item %x -- END (len %d)" % (id(b), id(item), len(b))
      if len(b) > 0:
            return b
      return None

def parse_argparm(p, cat, item):
      #print "parse argparm %x: item %x: %r" % (id(p), id(item), item)
      t = item['type'].replace('_mutable','')
      if not t.endswith(cat):
            unexpected(item)
      p.loc = item['loc']
      p.type = item['type']
      p.ctype = CType(items = item['ctype'])
      p.name = item['name'].strip()
      if item.has_key('init'):
         p.init = parse_block(item['init'])
      return p            

def parse_argparm_mem(p, cat, item):
      #print "parse argparm %x: item %x: %r" % (id(p), id(item), item)
      t = item['type'].replace('_mem','')
      if not t.endswith(cat):
            unexpected(item)
      p.loc = item['loc']
      p.type = t
      p.kind = parse_kind(item['kind'])
      p.name = item['name'].strip()
      if item.has_key('init'):
         p.rhs = item['init']
      return p            

def parse_break(item):
      return Break(loc = item['loc'])

def parse_end_thread(item):
      return EndThread(loc = item['loc'])

def parse_funptrdecl(item):
      d = FunDeclPtr(loc = item['loc'],
                     loc_end = item['loc_end'],
                     name = item['name'].strip(),
                     extras = parse_extras(item['extras']))
      for p in item['params']:
            if p['type'].endswith('_mem'):
                  d += parse_argparm_mem(FunParmMem(), 'parm', p)
            else:
                  d += parse_argparm(FunParm(), 'parm', p)
      return d

def parse_fundecl(item):
      d = FunDecl(loc = item['loc'], 
                  loc_end = item['loc_end'],
                  name = item['name'].strip(),
                  extras = parse_extras(item['extras']))
      for p in item['params']:
            if p['type'].endswith('_mem'):
                  d += parse_argparm_mem(FunParmMem(), 'parm', p)
            else:
                  d += parse_argparm(FunParm(), 'parm', p)
      return d

def parse_fundef(item):
      d = FunDef(loc = item['loc'], 
                 loc_end = item['loc_end'], 
                 name = item['name'].strip(),
                 extras = parse_extras(item['extras']),
                 body = parse_block(item['body']))
      for p in item['params']:
            if p['type'].endswith('_mem'):
                  d += parse_argparm_mem(FunParmMem(), 'parm', p)
            else:
                  d += parse_argparm(FunParm(), 'parm', p)
      return d

def parse_program(source):

      source = eval(source)

      p = Program()
      for item in source:
            if type(item) == type({}):
                  t = item['type']
                  if t == 'decl': p += parse_fundecl(item)
                  elif t == 'decl_funptr': p += parse_funptrdecl(item)
                  elif t == 'fundef': p += parse_fundef(item)
                  elif t == 'scope': p += parse_scope(item)
                  else: unexpected(item)
            else: p += Opaque(item)
      return p


__all__ = ['parse_program']
