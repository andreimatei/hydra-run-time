import sys
import pprint
from ..msg import warn
from ..visitors import *
from ..ast import *

def die(msg):
    print >>sys.stderr, "%s: %s" % (sys.argv[0], msg)
    sys.exit(1)

def gen_loop_fun_name(orig_name):  # takes a thread function name and generates a name that will be used as
                                   # the name of the function that implements the corresponding loop
    #return Opaque("_fam_") + orig_name
    #return Opaque("__slFmta") + orig_name
    return orig_name

def gen_meta_loop_fun_name(orig_name):  # takes a thread function name and generates a name that will be used as
                                   # the name of the function that implements the loop over the ranges
    return orig_name + '_metaloop'

class Create_2_HydraCall(ScopedVisitor):

    def __init__(self, *args, **kwargs):
        super(Create_2_HydraCall, self).__init__(*args, **kwargs)
      
    def get_no_shareds(self):
        rez = 0
        for a in self.__cur_cr.args:
            if (a.type.startswith("sh")):
                rez += 1
        return rez
    
    def get_no_globals(self):
        rez = 0
        for a in self.__cur_cr.args:
            if (not(a.type.startswith("sh"))):
                rez += 1
        return rez

    def get_arg_index(self, arg, shared):  # returns the index of a specified argument, among shareds of globals
        index = 0
        for a in self.__cur_cr.args:
            if (a.name == arg.name):
                return index
            else:
                if shared is None:
                    index += 1
                else:
                    if shared and a.type.startswith("sh"):
                        index += 1
                    else:
                        if (not(shared)) and (not(a.type.startswith("sh"))):
                            index += 1

        die("get_arg_index: can't find index for arg %s (shared = %s)" % (arg.name, shared))
        assert(0)

    # S -> the value of the .S(catter) bit to be set if decl refers to a global CreateArgMem
    def do_visit_seta(self, loc, decl, val, S):
        #print 'in do_visit_seta. rhs = ' + rhs
        #b = rhs.accept(self)
        setter = None

        if decl.type.startswith("sh"):      # shared argument
            # find my own index
            index = self.get_arg_index(arg = decl, shared = True)
            
            value = Block()
            if (not isinstance(decl, CreateArgMem)):
                value += Opaque('(long)(') + val + ')'
            else:
                value += Opaque('') + '_stub_2_canonical_stub(' + val + ', 0)'

            if (not isinstance(decl, CreateArgMem)):
                setter = (flatten(loc, "write_istruct(") 
                     + self.__first_tc + ".node_index, &" + self.__first_tc 
                        + ('.tc->shareds[0][%d], ' % index)
                        #+ '(long)(' + val + ')' 
                        + value 
                        + ', &' + self.__first_tc
                        + ');\n'
                        )
            
            else:   # mem argument. Use write_argmem
                setter = (flatten(loc, "write_argmem(") 
                        + self.__first_tc + ".node_index"
                        + ", &" + self.__first_tc + ('.tc->shareds[0][%d], ' % index)
                        + value
                        + ' , &' + self.__first_tc + ('.tc->shared_descs[0][%d]' % index) 
                        + ', &' + self.__first_tc 
                        + ');\n'
                        )
                

        else:                   # global argument
            # find my own index
            index = self.get_arg_index(arg = decl, shared = False)
            # setting a global is done by first writing to a local var (so that the parent can read it
            # back if it does a geta) and then calling "write_global" with the value of the local var (so
            # that we don't execute the rhs again)
            if (not isinstance(decl, CreateArgMem)):
                setter = CVarSet(loc = loc, decl = decl.cvar, rhs = val) + ';  // setting local copy'
                setter += (flatten(loc, "write_global(") +
                    self.__fam_context + ', %s' % index + ', (long)' + CVarUse(decl = decl.cvar) + ', 0);\n')
            else:  # for ArgMem's, we currently don't write to a temporary, so sl_getma() won't work
                # TODO: write to a temporary (we need a .cvar in decl)
                setter = (flatten(loc, "write_global(") +
                    self.__fam_context + ', %s' % index + ', _stub_2_long(_stub_2_canonical_stub(' 
                    + val + ', ' + str(S) +')), 1);\n')

        self.__arg_setters.append(setter)
        return None
        #return CVarSet(loc = loc, decl = decl.cvar, rhs = b) 

    def visit_seta(self, seta):
        b = seta.rhs.accept(self)
        return self.do_visit_seta(seta.loc, seta.decl, b, 0)

    def visit_setmema(self, seta):
        #print 'Create_2_HydraCall: visit_setmema: arg %s (%d)' % (seta.decl.name, id(seta.decl))
        #print 'Create_2_HydraCall: visiting setmema. rhs = %s. id = %d' % (seta.rhs, id(seta.rhs_decl))
        return self.do_visit_seta(seta.loc, seta.decl, CVarUse(decl = seta.rhs_decl.cvar_stub), 0)

    def visit_createarg(self, arg):
        # for shareds, append a pointer to the corresponding local variable to a list
        # of arguments that will be passed to sync
        if arg.type.startswith("sh"):
            self.__callist.append(flatten(None, ', &'))
            self.__callist.append(CVarUse(decl = arg.cvar))
        return arg
    
    def visit_createargmem(self, arg):
        #print 'Create_2_HydraCall: visit_createargmem: adding cvar to arg %s (%d)' % (arg.name, id(arg))

        # for shareds, append a pointer to the corresponding local variable to a list
        # of arguments that will be passed to sync
        if arg.type.startswith("sh"):
            #decls = arg.create.scope.decls
            #var = CVarDecl(loc = arg.loc, name = 'C$a$%s' % arg.create.label, ctype = 'memdesc_stub_t')
            #decls += var
            #arg.cvar = var

            self.__callist.append(Opaque(', &'))
            self.__callist.append(CVarUse(decl = arg.mem_decl.cvar_stub))
        return arg


    def emit_call_allocate_fam(self, create, mapping_decision_var):
        newbl = Block()

        fam_context_var = CVarDecl(loc = create.loc_end, name = 'alloc$%s' % create.label,
                                   ctype = 'fam_context_t*', init = '0') # init to NULL so it can be tested later;
                                                                # if it is found to be != NULL, it will mean that
                                                                # the create path taken at runtime was the regular one
                                                                # (as opposed to the sequential one)
        self.cur_scope.decls += fam_context_var
        self.__cur_fam_context = fam_context_var  # to be used when visiting arguments of type mem_t
        
        no_shareds = str(self.get_no_shareds())
        no_globals = str(self.get_no_globals())
        
        start_index = CVarUse(decl = create.cvar_start)
        end_index = CVarUse(decl = create.cvar_limit) + " - 1"  # this is now inclusive
        step = CVarUse(decl = create.cvar_step)
        
        no_threads_block = Opaque('') + '((' + end_index + ' - ' + start_index + ' + 1) / ' + step + ')'

        newbl += CVarSet(decl = fam_context_var, rhs = (
                    flatten(create.loc_end, 'allocate_fam(')
                    + no_threads_block            # no_threads
                    + ', 0'                       # parent_id. NULL.
                    + ', &' +  CVarUse(decl = mapping_decision_var)
                    + ')'))
        newbl += ';\n'
        return newbl, fam_context_var

    def emit_call_map_fam(self, funvar, create, lowcreate, gencallee):
        newbl = Block()
    
        mapping_decision_var = CVarDecl(loc = create.loc_end, name = 'map$%s' % create.label, ctype = 'mapping_decision')
        self.cur_scope.decls += mapping_decision_var
        
        start_index = CVarUse(decl = create.cvar_start)
        end_index = (Opaque('(') 
                    + CVarUse(decl = create.cvar_limit) + " - 1"  # this is now inclusive
                    + Opaque(')'))
        step = CVarUse(decl = create.cvar_step)
        mapspec = create.mapping.get_attr("mapping", None)
        default_place_policy = Opaque('INHERIT_DEFAULT_PLACE')  # inherit from the parent
        
        # if statically delegating to PLACE_LOCAL, and a jump to the sequential create
        #TODO(kena): is there a better way to test for PLACE_LOCAL on the following line?
        if create.place.__len__() == 1 and create.place.__getitem__(0).text.strip() == "PLACE_LOCAL":
            #print 'found delegation to PLACE_LOCAL; skipping mapping and allocation'
            newbl += CGoto(target = lowcreate.target_next) + '; // compiler detected delegation to PLACE_LOCAL, so jump to creation of sequential version\n'
        else:
            #print "didn't find delegation to PLACE_LOCAL %s \"%s\"" % (create.place.__len__(), create.place.__getitem__(0).text)
            pass

        # add a dynamic jump to the sequential create
        newbl += (Opaque('if ( _places_equal(') + CVarUse(decl = create.cvar_place) + ', PLACE_LOCAL)) {'
                  + CGoto(target = lowcreate.target_next) + ';}\n')
        
        # emit a call to interpret the mapping function
        mapping_hint_var = CVarDecl(loc = create.loc_end, name = 'mapping_hint%s' % create.label, ctype = 'mapping_hint_t')
        self.cur_scope.decls += mapping_hint_var
        no_threads_block = Opaque('') + '((' + end_index + ' - ' + start_index + ' + 1) / ' + step + ')'

        if mapspec is not None:
            mf = mapspec.mf  # the mapping function
            if mf == "spread":
                newbl += CVarSet(decl = mapping_hint_var, rhs = (
                            Opaque('_interpret_mf_spread(')
                            + create.place     # place
                            + ', ' + no_threads_block
                            + ', ' + ('1' if self.get_no_shareds() == 0 else '0')  #independent
                            + ', ' +mapspec.n
                            + ')'
                            ))
                # TODO: set the default_place_policy such that create.place becames the default place for
                # all the threads in the new family. Currently we don't have such a policy.
            elif mf == "distribute":
                default_place_policy = mapspec.m
                newbl += CVarSet(decl = mapping_hint_var, rhs = (
                            Opaque('_interpret_mf_distribute(')
                            + create.place     # place
                            + ', ' + no_threads_block
                            + ', ' + ('1' if self.get_no_shareds() == 0 else '0')  #independent
                            + ', ' + mapspec.n
                            + ')'
                            ))
                newbl += Opaque(';\n')
            elif mf == "distribute_ex":
                default_place_policy = mapspec.default_place_policy
                newbl += CVarSet(decl = mapping_hint_var, rhs = (
                            Opaque('_interpret_mf_distribute_ex(')
                                + mapspec.nodes
                                + ', ' + mapspec.procs
                                + ', ' + mapspec.tcs
                                + ', ' + mapspec.block
                                + ')'
                            ))
                newbl += Opaque(';\n')
            else:
                die("unsupported mapping")
        else:
            newbl += CVarSet(decl = mapping_hint_var, rhs = (
                         Opaque('_interpret_mf_spread(')
                         + create.place     # place
                         + ', ' + no_threads_block
                         + ', ' + ('1' if self.get_no_shareds() == 0 else '0')  #independent
                         + ', ' + create.block
                         + ')'
                         ))
            newbl += Opaque(';\n')
        
        # emit a call to map_fam
        newbl += CVarSet(decl = mapping_decision_var, rhs = (
                    flatten(create.loc_end, 'map_fam(')
                    + '&' + gen_meta_loop_fun_name(funvar)          # func
                    + ', ' + no_threads_block                       # no_threads
                    + ', ' + create.place                           # place
                    + ', ' + gencallee                              # gencallee
                    + ', ' + CVarUse(decl = mapping_hint_var)       # hint
                    + ', 0'                                         # parent_id. NULL.
                    + ')' 
                    ))
        newbl += Opaque(';\n')

        return newbl, mapping_decision_var, default_place_policy


    def visit_lowcreate(self, lc):
        cr = self.cur_scope.creates[lc.label]

        #print >>sys.stderr, "BLOCK create ", cr.fun, cr.block
        #print >>sys.stderr, "PLACE create ", cr.fun, cr.place
        #print >>sys.stderr, "EXTRAS create ", cr.fun, cr.extras
        gencallee = cr.extras.get_attr("gencallee", None)
        if gencallee is not None:
            gencallee = Opaque('1')
            #print >>sys.stderr, "found gencallee on create"
        else:
            #print >>sys.stderr, "didn't find gencallee on create"
            gencallee = Opaque('0')
        #print >>sys.stderr, "MAPPING create ", cr.fun, cr.mapping
        n = cr.mapping.get_attr("localize", None)
        #if n is not None:
            #print >>sys.stderr, "found localize: ", n
            
        #Create place: cr.cvar_place  (CVarUse(decl = cr.cvar_place))



        newbl = []
        lbl = cr.label

        # generate the function pointer
        if cr.funtype == cr.FUN_ID:
            if lc.lowfun is not None:
                funvar = lc.lowfun
            else:
                # not yet split
                funvar = Opaque(cr.fun)
        else:
            die('function pointers not yet supported')
            assert False #TODO

        
        # initialize members to be used when visiting the arguments
        cr = self.cur_scope.creates[lc.label]
        self.__callist = []
        self.__arg_setters = []
        self.__cur_cr = cr

        # expand call to map_fam(..)

        mapcall, mapping_decision_var, default_place_policy = self.emit_call_map_fam(funvar, cr, lc, gencallee)
        newbl.append(mapcall)

        #test is the mapping engine said we should inline the family
        newbl.append(
                    Opaque('if (') + CVarUse(decl = mapping_decision_var) + '.should_inline) {'
                    + CGoto(target = lc.target_next) + ';}\n'
                    )

        #expand call to allocate_fam()
        allocate_call, fam_context_var = self.emit_call_allocate_fam(cr, mapping_decision_var)
        newbl.append(allocate_call)
        cr.fam_context_var = fam_context_var  # link the fam_context_var to the create; it will be used later
                                              # by gather to test whether the create path taken at run-time
                                              # was the regular one (as opposed to the sequential one)

        # if allocation failed, jump to next target, if available
        newbl.append(Opaque('if (') + CVarUse(decl = fam_context_var) + ' == 0) {\n')
        if lc.target_next is not None:
            #newbl.append(Opaque('printf("allocate failed; calling sequential version\\n");'));
            newbl.append(CGoto(target = lc.target_next) + ';\n}\n')
            pass
        else:
            # abort the program
            newbl.append(Opaque('printf(stderr, "allocate failed. Aborting."); exit(1);\n}\n'))

        # expand call to create_fam()
        first_tc_var = CVarDecl(loc = cr.loc_end, name = 'first_tc$%s' % lbl,
                                ctype = 'tc_ident_t')
        self.cur_scope.decls += first_tc_var

        create_call = CVarSet(decl = first_tc_var,
                        rhs = flatten(cr.loc_end, 'create_fam(')
                            + CVarUse(decl = fam_context_var) 
                            + ', &' + gen_meta_loop_fun_name(funvar)
                            + ', ' + CVarUse(decl = cr.cvar_start)  # real_start_index
                            + ', ' + CVarUse(decl = cr.cvar_step)  # step
                            + ', ' + default_place_policy
                            + ')');
        newbl.append(create_call + ';\n')
        self.__first_tc = CVarUse(decl = first_tc_var)

        # FIXME: we also have __cur_fam_context. Keep only one
        self.__fam_context = CVarUse(decl = fam_context_var)  
        
        # consume body
        newbl.append(lc.body.accept(self))
        
        #consume arguments
        for a in cr.args:
            a.accept(self) # accumulate the call/protolists/setters

        #add setters for shareds and globals
        for s in self.__arg_setters:
            newbl.append(s);    
        

        #expand call to sync
        if not(self.__callist):
            self.__callist.append(flatten(None, ", 0"))

        no_shareds = str(self.get_no_shareds())

        sync_call = flatten(cr.loc_end, "sync_fam(") + \
                    CVarUse(decl = fam_context_var) + ', ' + str(no_shareds) # + \
        for node in self.__callist:
            sync_call += node
        sync_call += ')'
        
        sync_call_assignment = CVarSet(decl = cr.cvar_exitcode, rhs = sync_call) + ';'
       
        newbl.append(sync_call_assignment)
                                        
        return newbl
    

    def visit_scatteraffine(self, scatter):
        a = scatter.a
        b = scatter.b
        c = scatter.c
        stub = scatter.rhs_decl.cvar_stub
        
        #print 'scatter_affine: type of scatter.decl = ' + scatter.decl.__class__.__name__
        #print 'scatter_affine: type of scatter.decl.mem_decl = ' + scatter.decl.mem_decl.__class__.__name__
        #print 'scatter_affine: type of scatter.decl.create = ' + scatter.decl.create.__class__.__name__
        #print ' id of scatter.decl.create = ' + str(id(scatter.decl.create))
        #print ' id of scatter.decl = ' + str(id(scatter.decl))

        #scatter.decl.scatter_stub = stub
        #scatter.decl.fam_context = self.__cur_fam_context  #TODO: the fam context should be saved
                                        # in some other place, maybe in the create (scatter.decl.create)...
                                        # Discuss with Raphael

        # save a reference to the fam_context and to the stub in the create, to be used by gather
        scatter.decl.create.fam_context = self.__cur_fam_context
        if not hasattr(scatter.decl.create, 'scatter_stubs'):
            scatter.decl.create.scatter_stubs = {}
        scatter.decl.create.scatter_stubs[scatter.name] = stub

        create = scatter.decl.create
        fam_context_decl = self.__cur_fam_context
        scatter_call = (flatten(scatter.loc, "_memscatter_affine(") 
                        + CVarUse(decl = fam_context_decl) 
                        + ", " + CVarUse(decl = stub) + ', ' 
                        + a + ',' + b + ',' + c + ', ' 
                        + CVarUse(self.__cur_cr.cvar_start) + ',' # fam_start_index
                        + CVarUse(self.__cur_cr.cvar_step)  # step
                        + ');'
                        )
                    
        self.__arg_setters.append(scatter_call)
        # create a stub and treat it as a sl_setma(stub)
        # set the S bit on the stub that is being passed
        #new_rhs = flatten(None, "_stub_2_long(_stub_2_canonical_stub(") + CVarUse(decl = stub) + " , 1))"
        new_rhs = CVarUse(decl = stub)
        self.do_visit_seta(scatter.loc, scatter.decl, new_rhs, 1) 

        return None

        """
    def visit_gatheraffine(self, gather):
        print '~!~!~!~!~!~!~!~!~!~!~!~!~~~~~~~~~~~~~~~~~~~~~~ seen gather'
        a = gather.a
        b = gather.b
        c = gather.c
        cvar_stub = gather.decl.scatter_stub
        fam_context = gather.decl.fam_context
        new_items = Block()
        new_items += (flatten(gather.loc, '')
                + '_memgather_affine('
                + CVarUse(decl = fam_context) + ', '
                + CVarUse(decl = cvar_stub) + ', '
                + a + ', ' + b + ', ' + c + ')'
                )
        return new_items
        """
    
    #def visit_getmema(self, getma):
    #    new_items = (Opaque('') + CVarUse(loc = getma.loc, decl = getma.lhs_decl.cvar_stub) + ' = '
    #                + CVarUse(loc = getma.loc, decl = getma.decl.cvar))
    #    return new_items

class TFun_2_HydraCFunctions(DefaultVisitor):
    def __init__(self, *args, **kwargs):
        super(TFun_2_HydraCFunctions, self).__init__(*args, **kwargs)
        #self.__shlist = None
        #self.__gllist = None


    def visit_getmemp(self, getp):
        newbl = []
        param = self.__paramNames_2_params[getp.name]
        stub_decl = getp.lhs_decl.cvar_stub
        desc_decl = getp.lhs_decl.cvar_desc
       
        if not param.isShared:
            stub_read_cmd = '_long_2_stub(read_istruct(&_cur_tc->globals[%d], _get_parent_ident()))' % param.index
        else:  #shared
            #stub_read_cmd = '_long_2_stub(read_istruct(&_cur_tc->shareds[%d], prev))' % param.index
            if self.__state == 0: #begin
                stub_read_cmd = '_long_2_stub(read_istruct(&_cur_tc->shareds[_cur_tc->current_generation][%d], prev))' % param.index
            elif self.__state == 1: #middle
                stub_read_cmd = '_long_2_stub(read_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d]))' % param.index
            elif self.__state == 2: #end
                stub_read_cmd = '_long_2_stub(read_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d]))' % param.index
            elif self.__state == 3: #generic
                stub_read_cmd = '_long_2_stub(read_istruct(&_cur_tc->shareds[_cur_tc->current_generation][%d], prev))' % param.index
            else:
               assert False
    
        # emit the initialization of the local stub
        newbl.append(flatten(getp.loc, '') + CVarUse(decl = stub_decl) + ' = ' + stub_read_cmd);
        return newbl

    def visit_getp(self, getp):
        newbl = []
        param = self.__paramNames_2_params[getp.name]
       
        if not param.isShared:
            newbl.append(flatten(None, '((') + getp.decl.ctype + ')'
                        'read_istruct(&_cur_tc->globals[%d], _get_parent_ident()))' %
                        param.index)
        else: #shared
            if self.__state == 0: #begin
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct(&_cur_tc->shareds[_cur_tc->current_generation][%d], prev))' %
                            param.index)
            elif self.__state == 1: #middle
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d]))' %
                            param.index)
            elif self.__state == 2: #end
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d]))' %
                            param.index)
            elif self.__state == 3: #generic
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct(&_cur_tc->shareds[_cur_tc->current_generation][%d], prev))' %
                            param.index)
            else:
               assert False

        return newbl

    def visit_setp(self, setp):
        b = setp.rhs.accept(self)
        param = self.__paramNames_2_params[setp.name]
        newbl = []

        #assert we're writing to a shared... TODO: check if it is possible to
        #write to a global (from a child) and, if so, what it's supposed to mean
        assert(param.isShared)

        if setp.decl.seen_get:  # TODO: do I need casting for b in all the branches below?
            if self.__state == 0: #begin
                newbl.append(flatten(None,#setp.loc,
                             'write_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d],' %
                              param.index) + b + ')')
                
            elif self.__state == 1: #middle
                newbl.append(flatten(None,#setp.loc,
                             'write_istruct_same_tc(&_cur_tc->shareds[_cur_tc->current_generation][%d],' %
                              param.index) + b + ')')
            elif self.__state == 2 or self.__state == 3: #end and generic
                newbl.append(flatten(setp.loc,  # end and generic are passed the array of shareds as an argument
                             'write_istruct(next->node_index, &shareds[%d],' % param.index) \
                             + b + ', next);//, 0);')
            else:
                assert(0)
        else:  # no need to write to anything; just generate the rhs
            #TODO(kena): check that you're fine with this
            newbl.append(flatten(setp.loc,'(void)(') + b + ');') # cast to void suppress "statement has no effect warning" 

        return newbl


    def visit_setmemp(self, setp):
        b = (Opaque('') + '_stub_2_canonical_stub(' 
            + CVarUse(decl = setp.rhs_decl.cvar_stub) + ', '
            + CVarUse(decl = setp.rhs_decl.cvar_stub) + '.S)')
        #b = setp.rhs.accept(self)
        param = self.__paramNames_2_params[setp.name]
        newbl = []

        #assert we're writing to a shared... TODO: check if it is possible to
        #write to a global (from a child) and, if so, what it's supposed to mean
        assert(param.isShared)

        #TODO: see if the same optimization regarding seen_get can be used like in setp

        if self.__state == 0 or self.__state == 1:  # begin and middle
            newbl.append((flatten(setp.loc, 'write_argmem('))
                    + 'NODE_INDEX, '
                    + ('&_cur_tc->shareds[_cur_tc->current_generation][%d], ' % param.index)
                    + b + ', '
                    + '&_cur_tc->shared_descs[_cur_tc->current_generation][%d], ' % param.index
                    + '&_cur_tc->ident)'
                   )
        elif self.__state == 2 or self.__state == 3: #end and generic
            newbl.append(Opaque('write_argmem(next->node_index, &shareds[%d], ' % param.index)
                         + b + ', &shared_descs[%d], ' % param.index + ' next)'
                         )
        else:
            assert(0)

        return newbl

    def visit_funparm(self, parm):
        self.__paramNames_2_params[parm.name] = parm
        assert not (parm.type.startswith("shf") or parm.type.startswith("glf"))
        if parm.type.startswith("sh"):
            parm.isShared = True
            parm.index = self.__sh_parm_index
            self.__sh_parm_index += 1
        else:
            parm.isShared = False
            parm.index = self.__gl_parm_index
            self.__gl_parm_index += 1
        return parm

    def visit_funparmmem(self, parm):
        return self.visit_funparm(parm)

    def visit_fundecl(self, fundecl, keep = False, omitextern = False):
        self.__sh_parm_index = 0  # counter for the number of shareds seen
        self.__gl_parm_index = 0  # counter for the number of globals seen
        self.__paramNames_2_params = {}
        for parm in fundecl.parms:
            parm.accept(self)
        
        if fundecl.extras.get_attr('static', None) is not None:
            qual = "static"
        elif omitextern:
            qual = ""
        else:
            qual = "extern"

        newitems = Block()
        #TODO: what does keep mean? :)
        if not keep:
            #newitems = flatten(fundecl.loc, "%s void " % qual) + gen_loop_fun_name(fundecl.name) + "();";
            #newitems = flatten(fundecl.loc, "%s void " % qual) + fundecl.name + "();";

            # emit declaration for metaloop function
            newitems += flatten(fundecl.loc, "%s void " % qual) + gen_meta_loop_fun_name(fundecl.name) + "();";
            # emit declaration for loop function
            newitems += flatten(fundecl.loc, "%s void " % qual) + gen_loop_fun_name(fundecl.name) + "();";
            return newitems
        else:
            return None
 
    def visit_fundeclptr(self, fundecl):
        #TODO
        assert False
        '''
        self.__shlist = []
        self.__gllist = []
        if fundecl.extras.get_attr('typedef', None) is not None:
            qual = "typedef"
        elif fundecl.extras.get_attr('static', None) is not None:
            qual = "static"
        else:
            qual = ''
        self.__buffer = flatten(fundecl.loc, 
                                " %s long (*%s)(const long __slI" 
                                % (qual, fundecl.name))
        for parm in fundecl.parms:
            parm.accept(self)
        self.__buffer += ')'
        ret = self.__buffer
        self.__buffer = None
        self.__shlist = self.__gllist = None
        return ret
        '''

    # Generates the function that loops through the ranges assigned to a TC
    # no_shareds: number of shared parameters of the threads function. Generally used to distinguish between a dependent
    # and an independent family.
    def gen_meta_loop_fun(self, fundef, no_shareds):
        fun_name = gen_meta_loop_fun_name(fundef.name)

        newitems = Block()
        newitems += flatten(fundef.loc, '/* Meta-loop function for family %s */' % fundef.name) 
        newitems += """
        void """ + fun_name + """() { 
            long normalized_start = _cur_tc->start_index;
            const int independent_family = """ + ('1' if no_shareds == 0 else '0') + """;
            int first_generation = 1;
            """ + 'printf("COMP: metaloop. Running %ld generations. \\n", _cur_tc->no_generations_left);' + """
            while (_cur_tc->no_generations_left-- > 0) {
                unsigned long threads_in_gen = (_cur_tc->no_generations_left > 0) ? 
                    _cur_tc->no_threads_per_generation : _cur_tc->no_threads_per_generation_last;
                _cur_tc->index_start = _denormalize_index(normalized_start, _cur_tc->denormalized_fam_start_index, _cur_tc->step);
                _cur_tc->index_stop  = _denormalize_index(normalized_start + threads_in_gen - 1, _cur_tc->denormalized_fam_start_index, _cur_tc->step);
                """ + 'printf("COMP: metaloop. indexes for generation: %ld -> %ld (inclusive). Set of shareds: %d\\n",_cur_tc->index_start, _cur_tc->index_stop, _cur_tc->current_generation);' + """
                """ + gen_loop_fun_name(fundef.name) + """();  // call function to loop over the threads in the current range
                """ + 'printf("COMP: metaloop. back from running a generation. Generations left %ld\\n", _cur_tc->no_generations_left);' + """
                
                // update the start index for next generation
                if (_cur_tc->no_generations_left > 1) {
                    normalized_start += _cur_tc->gap_between_generations;
                } else {
                    normalized_start = _cur_tc->start_index_last_generation;
                }

                if (!independent_family) {
                    // synchronize termination of generations so that they don't get out of sync.
                    // nothing to do for the first generation on the first tc in the family
                    if (!(_cur_tc->is_first_tc_in_fam && first_generation)) {
                        read_istruct(&_cur_tc->prev_range_done, &_cur_tc->prev);
                    }
                }

                // switch sets of shareds
                _advance_generation(""" + str(no_shareds) + """, first_generation);
                first_generation = 0;
                
                // for dependent families, write the prev_range_done on the next TC
                if (!independent_family) {
                    write_istruct(_cur_tc->next.node_index, 
                                  &_cur_tc->next.tc->prev_range_done, 
                                  1,               // value
                                  &_cur_tc->next);  // reader
                                  //0);              // is_mem
                }
            """ + 'printf("COMP: metaloop (tc=%d). End of generation. generations left = %ld \\n", _cur_tc->ident.tc_index, _cur_tc->no_generations_left);' + """
            }

            if (independent_family) {
                if (!_cur_tc->is_first_tc_in_fam) {
                    read_istruct(&_cur_tc->prev_tc_done, &_cur_tc->prev);
                }
                if (!_is_last_tc_in_fam()) {
                    write_istruct(_cur_tc->next.node_index,
                              &_cur_tc->next.tc->prev_tc_done,
                              1,
                              _get_next_ident());
                }
            }

            if (_is_last_tc_in_fam() && _get_parent_ident()->node_index != -1) {
                //tc_ident_t tci = _cur_tc->ident;  // FIXME: remove this
                printf("COMP: metaloop: last thread in the family done. Unblocking parent.\\n");
                write_istruct(_get_parent_ident()->node_index, _get_done_pointer(), 1, _get_parent_ident());//, 0);
            }
            _free_tc(_cur_tc->ident.proc_index, _cur_tc->ident.tc_index);
        """
        if fundef.name <> '__slFfmta___root_fam':
            newitems += "_return_to_scheduler();\n"
        else:
            newitems += "end_main();"
        newitems += """
        }
        """

        return newitems


    # Generates the function that loops through the threads in a range
    def gen_loop_fun(self,fundef):
        newitems = Block()
        newitems += flatten(fundef.loc, '')
        newitems += """
        void """ + gen_loop_fun_name(fundef.name) + """() {
            long __index, __start_index = _get_start_index(), __end_index = _get_end_index();
            long step = _cur_tc->step;
            
            // in the following block of code, attribute(unused) is used because of the special generation
            // of the function for the root family, in which case those variables would be unused.
            //fam_context_t* fam_context = _get_fam_context();
            //const tc_ident_t* parent = _get_parent_ident();
            const tc_ident_t* prev __attribute__((unused)) = _get_prev_ident();
            const tc_ident_t* next = _get_next_ident();
                                                                    
            //i_struct* done = _get_done_pointer();
            long no_threads = (__end_index - __start_index + 1) / step;

            if (no_threads >= 3) {"""
        if fundef.name <> "__slFfmta___root_fam":  # for main, we won't need this branch (and we can't generate it either
                                     # cause we haven't generated the non-generic flavours of the thread func)
            newitems +="""
                i_struct* shareds;
                memdesc_t* shared_descs;
    
                if (_is_last_tc_in_fam()) {
                    if (_cur_tc->no_generations_left == 0) {
                        shareds = _get_final_shareds_pointer();
                        shared_descs = _get_final_descs_pointer();
                    } else {
                        // write to the next generation
                        shareds = next->tc->shareds[_cur_tc->current_generation ^ 1];
                        shared_descs = next->tc->shared_descs[_cur_tc->current_generation ^ 1];
                    }
                } else {
                    // simply write to the next TC, same generation
                    shareds = next->tc->shareds[_cur_tc->current_generation];
                    shared_descs = next->tc->shared_descs[_cur_tc->current_generation];
                }

                %s_begin(prev, __start_index);
                for (__index = __start_index + step; __index + step <= __end_index; __index += step) {
                    %s_middle(__index); // TODO: check for break return value
                }
                %s_end(next, shareds, shared_descs, __index);
            """ % (fundef.name, fundef.name, fundef.name)
        else:
            newitems += "exit(1);  // main should never be created as a family of more than one thread"

        newitems += """
            } else {
                for (__index = __start_index; __index <= __end_index; __index += step) {
                    const tc_ident_t* p, *n;
                    i_struct* s = _cur_tc->shareds[_cur_tc->current_generation];
                    memdesc_t* shared_descs = _cur_tc->shared_descs[_cur_tc->current_generation];
                    if (__index == __start_index) {
                        p = _get_prev_ident();
                    } else {
                        p = &_cur_tc->ident;
                    }
                    if (__index + step > __end_index) {
                        """ + 'printf("COMP: loop (tc=%d): this will be the last thread in the tc in the generation.\\n", _cur_tc->ident.tc_index);' + """
                        n = _get_next_ident();
                        if (_is_last_tc_in_fam()) {
                            """ + 'printf("COMP: loop (tc=%d): this will be the last thread in the family in the generation.\\n", _cur_tc->ident.tc_index);' + """
                            if (_cur_tc->no_generations_left == 0) {
                                // write to the family context
                                """ + 'printf("COMP: loop: got to the last thread in fam.\\n");' + """
                                s = _get_final_shareds_pointer();
                                shared_descs = _get_final_descs_pointer();
                            } else {
                                // write to the next generation
                                """ + 'printf("COMP: loop (tc=%d): got to the last thread in generation across all TCs. Setting shareds to %d\\n", _cur_tc->ident.tc_index, (_cur_tc->current_generation ^ 1));' + """
                                s = next->tc->shareds[_cur_tc->current_generation ^ 1];
                                shared_descs = next->tc->shared_descs[_cur_tc->current_generation ^ 1];
                            }
                        } else {
                            // write to the next tc
                            s = next->tc->shareds[_cur_tc->current_generation];
                            shared_descs = next->tc->shared_descs[_cur_tc->current_generation];
                            """ + 'printf("COMP: loop (tc=%d): setting shareds to next TC (tc:%d shared:%p)\\n", _cur_tc->ident.tc_index, next->tc_index, &s[0]);' + """
                        }
                    } else {
                        n = &_cur_tc->ident;
                    }
                    """ + 'printf("COMP: loop (tc=%d): calling generic with index = %ld\\n", _cur_tc->ident.tc_index, __index);' + """
                    """ + fundef.name + """_generic(p, n, s, shared_descs, __index); // TODO: check for break value
                }

            }
            """ + 'printf("COMP: loop: done with this generation.\\n");' + """
        }

        """
        
        return newitems 


    def visit_fundef(self, fundef):
        # make copies of the body for begin, middle and end
        import sys
        #print >>sys.stderr, "EXTRAS fundef ", fundef.name, fundef.extras
        n = fundef.extras.get_attr('gencallee', None)
        if n is not None:
            #print >>sys.stderr, "got gencallee"
            pass

        begin_body = copy.deepcopy(fundef.body)
        middle_body = copy.deepcopy(fundef.body)
        end_body = copy.deepcopy(fundef.body)
        generic_body = copy.deepcopy(fundef.body)
      
        self.dispatch(fundef, seen_as = FunDecl, keep = True, omitextern = True)
 
        newitems = Block()   

        if fundef.name <> "__slFfmta___root_fam":  # for main, we just need the generic variant
            self.__state = 0  #begin
            newitems = flatten(fundef.loc, ("long %s_begin(const tc_ident_t* prev __attribute__((unused)), " +
                                           "long __index) {\n") % fundef.name)
            newitems += begin_body.accept(self)
            newitems += flatten(fundef.loc_end, "return 0; \n}")

            self.__state = 1  #middle
            newitems += flatten(fundef.loc, "long %s_middle(long __index) {\n" %
                                            fundef.name)
            newitems += middle_body.accept(self)
            newitems += flatten(fundef.loc_end, "return 0; \n}")

            self.__state = 2  #end
            newitems += flatten(fundef.loc, ("long %s_end(const tc_ident_t* next, "
                                + "i_struct* shareds __attribute__((unused)), "
                                + "memdesc_t* shared_descs, long __index) {\n") % fundef.name)
            newitems += end_body.accept(self)
            newitems += flatten(fundef.loc_end, "return 0; \n}")

        
        self.__state = 3  #generic
        newitems += flatten(fundef.loc, ("long %s_generic(const tc_ident_t* prev __attribute__((unused)), "
                            + "const tc_ident_t* next __attribute__((unused)), "
                            + "i_struct* shareds __attribute__((unused)), "
                            + "memdesc_t* shared_descs __attribute__((unused)), "
                            + "long __index __attribute__((unused))) {\n") % fundef.name)
        newitems += generic_body.accept(self)
        newitems += flatten(fundef.loc_end, "return 0; \n}")

        # generate meta loop function
        newitems += self.gen_meta_loop_fun(fundef, self.__sh_parm_index)
        
        # generate loop function
        newitems += self.gen_loop_fun(fundef)
        

        return newitems


    def visit_indexdecl(self, idecl):
        return flatten(idecl.loc, "register const long %s = __index" %
                       idecl.indexname)

    def visit_break(self, br):
        return flatten(br.loc, " return 1 ")

    def visit_endthread(self, et):
        return flatten(et.loc, " return 0 ")

class Mem_2_HRT(DefaultVisitor):
    def visit_memdef(self, d):
        #print 'Mem_2_HRT: visiting memdef %s. id = %d' % (d.name, id(d))
        scope = d.scope
        stub_decl = CVarDecl(loc = d.loc, name = "_" + d.name + "_stub", ctype = "memdesc_stub_t")
        scope.decls += stub_decl
        d.cvar_stub = stub_decl
        d.cvar_desc = None
        #d.visited_by = 'mem_2_hrt'  # FIXME: remove this
        # is descriptor needed? it is _not_ needed for sl_getmp 
        # possible values for set_op include MemDesc, GetMemP, SetMemA, MemActivate (lhs), MemExtend (lhs)
        # TODO: figure out if there are other operations when the descriptor is not needed
        descriptor_needed = True
        if isinstance(d.set_op, GetMemP): #or isinstance(d.set_op, SetMemA):
            descriptor_needed = False

        if descriptor_needed:
            #print 'inserting descriptor declaration in scope'
            desc_decl = CVarDecl(loc = d.loc, name = "_" + d.name + "_desc", ctype = "memdesc_t")
            scope.decls += desc_decl
            d.cvar_desc = desc_decl
            # if the initialization operation is extend, we have to init no_ranges since this will not be
            # done as part of extend. For the other operations, this initialization will be a byproduct
            # of those operations.
            if isinstance(d.set_op, MemExtend):
                #print 'adding no_range initialization'
                scope.decls += CVarUse(decl = desc_decl) + Opaque('.no_ranges = 0;')
            # initialize stub to be associated with descriptor
            scope.decls += (CVarUse(decl = stub_decl) + ' = _create_memdesc_stub(&' 
                        + CVarUse(decl = desc_decl) + ', NODE_INDEX, 1, 0);\n'
                        )
        
        return []

    def visit_memdesc(self, desc):
        no_elems = desc.kind.size
        elem_size = Opaque( "sizeof(") + desc.kind.itemtype + ")"
        def_node = desc.lhs_decl
        cpointer = desc.cptr
        new_items = Block()
        new_items += (flatten(desc.loc, "_memdesc(&") + CVarUse(decl = def_node.cvar_desc) +
                "," + cpointer + ',' + no_elems + ',' + elem_size + ');\n')
        return new_items

    def visit_memactivate(self, activate):
        # memactivate has an optional .lhs (and .lhs_decl), which is a memdef for a descriptor that
        # will be initialized to the local copy of the data
        new_items = Block()
        new_items += (flatten(activate.loc, '')
                + '_memactivate(&'
                + CVarUse(decl = activate.rhs_decl.cvar_stub) + ', '
                )
        if activate.lhs is not None:
            assert activate.lhs_decl.cvar_desc is not None
            new_items += (flatten(None, '&')
                +  CVarUse(decl = activate.lhs_decl.cvar_desc) + ', '
                + '&' + CVarUse(decl = activate.lhs_decl.cvar_stub)
                )
        else:  # no lhs; pass NULL for new_desc and new_stub
            new_items += '0, 0' 
        new_items += ')'
        
        return new_items

    def visit_mempropagate(self, prop):
        #print 'in propagate. rhs = ' + prop.rhs
        new_items = Block()
        new_items += (flatten(prop.loc, '')
            + '_mempropagate(' + CVarUse(decl = prop.rhs_decl.cvar_stub) + ')'
            )
        return new_items

    def visit_memrestrict(self, re):
        new_items = Block()
        new_items += (flatten(re.loc, "") + CVarUse(decl = re.lhs_decl.cvar_stub) + ' = ' +
                      "_memrestrict(" 
                      + CVarUse(decl = re.rhs_decl.cvar_stub) + ', '         #original stub
                      + '&' + CVarUse(decl = re.lhs_decl.cvar_desc) + ', '   #new descriptor
                      #+ CVarUse(decl = re.rhs_decl.cvar_desc) + '.ranges[0], '  # TODO: remove this argument (first range)
                      + re.offset + ', ' + re.size + ')'
                      )
        return new_items
    
    def visit_memextend(self, extend):
        new_items = Block()
        new_items += (flatten(extend.loc, "_memextend(")
                + CVarUse(decl = extend.lhs_decl.cvar_stub) + ', '
                + CVarUse(decl = extend.rhs_decl.cvar_stub) + ')')
        return new_items


class Test_Visitor(ScopedVisitor):

    def visit_lowcreate(self, lc):
        # consume body
        lc.body.accept(self)
        
        cr = self.cur_scope.creates[lc.label]
        for a in cr.args:
            a.accept(self) # accumulate the call/protolists

        return lc



    def visit_setmema(self, seta):
        print '~~~~ Test_Visitor: visit_setmema: setmema = %d\targ %s (%d)' % (id(seta), seta.decl.name, id(seta.decl))
        return seta
    
    def visit_createargmem(self, arg):
        print '~~~~ Test_Visitor: visit_createargmem: createargmem = %d\targ %s (%d)' % (id(arg), arg.name, id(arg))
        return arg

class Separator(ScopedVisitor):
    def visit_lowcreate(self, lc):
        print '\n\n'
        return lc


__all__ = ["Create_2_HydraCall", "TFun_2_HydraCFunctions", "Mem_2_HRT", "Test_Visitor", "Separator"]


