from ..msg import die
from ..ast import *

def unexpected(item):
      die("unexpected construct '%s'" % item.get('type','unknown'), item)


def parse_varuse(varuse, item):
      #print "parse varuse %x: item %x: %r" % (id(varuse), id(item), item)
      varuse.loc = item['loc']
      varuse.name = item['name']
      if item.has_key('body'):
            varuse.rhs = parse_block(item['body'], False)
      return varuse

def parse_create(item):
      c = Create(loc = item['loc'],
                 loc_end = item['loc_end'],
                 label = item['lbl'],
                 place = parse_block(item['place'], False),
                 start = parse_block(item['start'], False),
                 limit = parse_block(item['limit'], False),
                 step = parse_block(item['step'], False),
                 block = parse_block(item['block'], False),
                 sync_type = item['sync'],
                 fun = parse_block(item['fun'], False),
                 body = parse_block(item['body'], False))
                 
      for p in item['args']:
            c.args.append(parse_argparm(CreateArg(), 'arg', p))

      if 'result' in item and item['result']:
            c.result_lvalue = parse_block(item['result'], False)

      return c

def parse_block(items, allow_index = False):
      if len(items) == 0:
            return None
      b = Block()
      #print "new block %x (len %d)" % (id(b), len(b))
      for item in items:
            #print "parse block %x (len %d): item %x: %r" % (id(b), len(b), id(item), item)
            if isinstance(item, dict):
                  t = item['type']
                  if t == 'indexdecl' and allow_index: b.indexname = item['name']
                  elif t == 'getp': b += parse_varuse(GetP(), item)
                  elif t == 'setp': b += parse_varuse(SetP(), item)
                  elif t == 'geta': b += parse_varuse(GetA(), item)
                  elif t == 'seta': b += parse_varuse(SetA(), item)
                  elif t == 'create': b += parse_create(item)
                  elif t == 'break': b += parse_break(item)
                  elif t == 'end_thread': b += parse_end_thread(item)
                  else: unexpected(item)
            else: 
                  assert isinstance(item, str)
                  b += Opaque(item)
            #print "parse block %x: item %x -- END (len %d)" % (id(b), id(item), len(b))
      return b

def parse_argparm(p, cat, item):
      #print "parse argparm %x: item %x: %r" % (id(p), id(item), item)
      if not item['type'].endswith(cat):
            unexpected(item)
      p.loc = item['loc']
      p.type = item['type']
      p.ctype = item['ctype']
      p.name = item['name']
      if item.has_key('init'):
         p.init = parse_block(item['init'], False)
      return p            

def parse_break(item):
      return Break(loc = item['loc'])

def parse_end_thread(item):
      return EndThread(loc = item['loc'])

def parse_fundecl(item):
      d = FunDecl(loc = item['loc'], name = item['name'])
      for p in item['params']:
            d += parse_argparm(FunParm(), 'parm', p)
      return d

def parse_fundef(item):
      d = FunDef(loc = item['loc'], 
                 loc_end = item['loc_end'], 
                 name = item['name'],
                 body = parse_block(item['body'], allow_index = True))
      for p in item['params']:
            d += parse_argparm(FunParm(), 'parm', p)
      return d

def parse_program(source):
      p = Program()
      for item in source:
            if type(item) == type({}):
                  t = item['type']
                  if t == 'decl': p += parse_fundecl(item)
                  elif t == 'fundef': p += parse_fundef(item)
                  else: unexpected(item)
            else: p += Opaque(item)
      return p


__all__ = ['parse_program']
