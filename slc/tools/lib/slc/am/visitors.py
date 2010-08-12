import sys
import pprint
from ..visitors import *
from ..ast import *


def gen_loop_fun_name(orig_name):  # takes a thread function name and generates a name that will be used as
                                   # the name of the function that implements the corresponding loop
    return Opaque("_fam_") + orig_name

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
            if (a == arg):
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
        assert(0)

    def do_visit_seta(self, loc, decl, rhs):
        b = rhs.accept(self)
        setter = None

        if decl.type.startswith("sh"):
            # find my own index
            index = self.get_arg_index(arg = decl, shared = True)
            setter = (flatten(loc, "write_istruct(") 
                    + self.__first_tc + ".node_index, &" + self.__first_tc 
                    + ('.tc->shareds[%d], ' % index) 
                    + '(long)(' + b + ')' 
                    + ', &' + self.__first_tc 
                    )
            # emit the 'is_mem' argument
            if (not isinstance(decl, CreateArgMem)):
                setter += ', 0'
            else:
                setter += ', 1'

            setter += ');\n'

        else:
            # find my own index
            index = self.get_arg_index(arg = decl, shared = False)
            # setting a global is done by first writing to a local var (so that the parent can read it
            # back if it does a geta) and then calling "write_global" with the value of the local var (so
            # that we don't execute the rhs again)
            if (not isinstance(decl, CreateArgMem)):
                setter = CVarSet(loc = loc, decl = decl.cvar, rhs = b) + ';  // setting local copy'
                setter += (flatten(loc, "write_global(") +
                    self.__fam_context + ', %s' % index + ', (long)' + CVarUse(decl = decl.cvar) + ', 0);\n')
            else:  # for ArgMem's, we currently don't write to a temporary, so sl_getma() won't work
                # TODO: write to a temporary (we need a .cvar in decl)
                setter = (flatten(loc, "write_global(") +
                    self.__fam_context + ', %s' % index + ',' + rhs + ', 1);\n')

        self.__arg_setters.append(setter)
        return None
        #return CVarSet(loc = loc, decl = decl.cvar, rhs = b) 

    def visit_seta(self, seta):
        return self.do_visit_seta(seta.loc, seta.decl, seta.rhs)

    def visit_setmema(self, seta):
        #TODO: implement this. I don't think seta.rhs can be passed directly to do_visit_seta;
        #maybe I want to say seta.rhs_decl.cvar_stub?

        #return do_visit_seta(self, seta.loc, seta.decl, seta.rhs)
        assert False

    def visit_createarg(self, arg):
        # for shareds, append a pointer to the corresponding local variable to a list
        # of arguments that will be passed to sync
        if arg.type.startswith("sh"):
            self.__callist.append(flatten(None, ', &'))
            self.__callist.append(CVarUse(decl = arg.cvar))
        return arg

    def visit_lowcreate(self, lc):
        cr = self.cur_scope.creates[lc.label]

        print >>sys.stderr, "PLACE create ", cr.fun, cr.place
        print >>sys.stderr, "EXTRAS create ", cr.fun, cr.extras
        n = cr.extras.get_attr("gencallee", None)
        if n is not None:
            print >>sys.stderr, "found gencallee"
        print >>sys.stderr, "MAPPING create ", cr.fun, cr.mapping
        n = cr.mapping.get_attr("localize", None)
        if n is not None:
            print >>sys.stderr, "found localize: ", n
            
        #Create place: cr.cvar_place  (CVarUse(decl = cr.cvar_place))

        if lc.target_next is not None:
            warn("alternative %s not used)" %
                 lc.target_next.name, lc)


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
            assert False #TODO

        
        # initialize members to be used when visiting the arguments
        cr = self.cur_scope.creates[lc.label]
        self.__callist = []
        self.__arg_setters = []
        self.__cur_cr = cr

        # expand call to map_fam(..)
        mapping_decision_var = CVarDecl(loc = cr.loc_end, name =
                'map$%s' % lbl, ctype = 'mapping_decision')
        self.cur_scope.decls += mapping_decision_var
        
        start = CVarUse(decl = cr.cvar_start)
        end_index = CVarUse(decl = cr.cvar_limit) + " - 1"  # this is now inclusive
        step = CVarUse(decl = cr.cvar_step)
       
        rrhs = flatten(cr.loc_end, 'map_fam(&') + gen_loop_fun_name(funvar) + ', (' + end_index \
                       + ' - ' + start + '+ 1) / ' + step+ ', 0, ' + cr.place + ')'

        mapping_call = CVarSet(decl = mapping_decision_var, rhs = rrhs)
        newbl.append(mapping_call + ';\n')

        #expand call to allocate_fam()
        fam_context_var = CVarDecl(loc = cr.loc_end, name = 'alloc$%s' % lbl,
                                   ctype = 'fam_context_t*')
        self.cur_scope.decls += fam_context_var
        self.__cur_fam_context = fam_context_var  # to be used when visiting arguments of type mem_t

        no_shareds = str(self.get_no_shareds())
        no_globals = str(self.get_no_globals())
       
        rrhs = flatten(cr.loc_end, 'allocate_fam(') \
                + start + ', ' \
                + end_index + ', ' + step + ', 0, &' + CVarUse(decl = mapping_decision_var) + ')'
        allocate_call = CVarSet(decl = fam_context_var, 
                               rhs = rrhs)
        newbl.append(allocate_call + ';\n')

        #expand call to create_fam()
        first_tc_var = CVarDecl(loc = cr.loc_end, name = 'first_tc$%s' % lbl,
                                ctype = 'tc_ident_t')
        self.cur_scope.decls += first_tc_var

        create_call = CVarSet(decl = first_tc_var,
                        rhs = flatten(cr.loc_end, 'create_fam(')
                            + CVarUse(decl = fam_context_var) + ',&' + gen_loop_fun_name(funvar) + ')');
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


        sync_call = flatten(cr.loc_end, "sync_fam(") + \
                    CVarUse(decl = fam_context_var) + ', ' + str(no_shareds) # + \
        for node in self.__callist:
            sync_call += node
        sync_call += ')'
        
        sync_call_assignment = CVarSet(decl = cr.cvar_exitcode, rhs = sync_call)
       
        newbl.append(sync_call_assignment)
                                        
        return newbl
    
    def visit_memactivate(self, activate):
        # memactivate has an optional .lhs (and .lhs_decl), which is a memdef for a descriptor that
        # will be initialized to the local copy of the data
        new_items = Block()
        new_items += (flatten(activate.loc, '')
                + '_memactivate(&'
                + CVarUse(decl = activate.rhs_decl.cvar_stub) + ', '
                #+ CVarUse(decl = activate.rhs_decl.cvar_desc) + '.ranges[0], '  #FIXME: remove this arg
                #+ CVarUse(decl = activate.rhs_decl.cvar_desc) + '.no_ranges, '
                )
        if activate.lhs is not None:
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

    def visit_scatteraffine(self, scatter):
        a = scatter.a
        b = scatter.b
        c = scatter.c
        stub = scatter.rhs_decl.cvar_stub
        desc = scatter.rhs_decl.cvar_desc
        # save a pointer to the stub in the argument declaration
        scatter.decl.scatter_stub = stub
        scatter.decl.fam_context = self.__cur_fam_context
        create = scatter.decl.create
        fam_context_decl = self.__cur_fam_context
        #FIXME: remove the first_range argument in the call to memscatter_affine
        first_range = CVarUse(decl = desc) + ".ranges[0]";
        scatter_call = (flatten(scatter.loc, "_memscatter_affine(") 
                        + CVarUse(decl = fam_context_decl) 
                        + ", " + CVarUse(decl = stub) + ', ' 
                        #+ first_range + ', '
                        + a + ',' + b + ',' + c + ');' )
                    
        self.__arg_setters.append(scatter_call)
        # create a stub and treat it as a sl_setma(stub)
        # set the S bit on the stub that is being passed
        new_rhs = flatten(None, "_stub_2_long(_stub_2_canonical_stub(") + CVarUse(decl = stub) + " , 1))"
        self.do_visit_seta(scatter.loc, scatter.decl, new_rhs) 

        return None

    def visit_gatheraffine(self, gather):
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
        else:
            stub_read_cmd = '_long_2_stub(read_istruct(&_cur_tc->shareds[%d], prev))' % param.index
    
        # emit the initialization of the local stub
        newbl.append(flatten(getp.loc, '') + CVarUse(decl = stub_decl) + ' = ' + stub_read_cmd);
        return newbl

    def visit_getp(self, getp):
        newbl = []
        param = self.__paramNames_2_params[getp.name]
       
        """
        #FIXME: remove this part with read_cmd; it was added for trying to support a form of mem parm, needs to go now 
        if not param.isShared:
            read_cmd = 'read_istruct(&_cur_tc->globals[%d], _get_parent_ident())' % param.index
        else: #shared
            if self.__state == 0: #begin
                read_cmd = 'read_istruct(&_cur_tc->shareds[%d], prev)' % param.index
            elif self.__state == 1: #middle
                read_cmd = 'read_istruct_same_tc(&_cur_tc->shareds[%d])' % param.index
            elif self.__state == 2: #end
                read_cmd = 'read_istruct_same_tc(&_cur_tc->shareds[%d]))' % param.index
            elif self.__state == 3: #generic
                read_cmd = 'read_istruct(&_cur_tc->shareds[%d], prev)' % param.index
        
        #print 'getp - "' + getp.decl.ctype[0].text + '"'
        #print getp.decl.ctype[0].text == 'mem_pointer_t' 
        if getp.decl.ctype[0].text == ' mem_pointer_t ':
            print '!!!!!! found reading a mem_pointer_t    ' + read_cmd
            newbl.append(flatten(None, '_activate_from_istruct(') + '(long)(%s))' % read_cmd)
            return newbl
        """

        if not param.isShared:
            newbl.append(flatten(None, '((') + getp.decl.ctype + ')'
                        'read_istruct(&_cur_tc->globals[%d], _get_parent_ident()))' %
                        param.index)
        else: #shared
            if self.__state == 0: #begin
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct(&_cur_tc->shareds[%d], prev))' %
                            param.index)
            elif self.__state == 1: #middle
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct_same_tc(&_cur_tc->shareds[%d]))' %
                            param.index)
            elif self.__state == 2: #end
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct_same_tc(&_cur_tc->shareds[%d]))' %
                            param.index)
            elif self.__state == 3: #generic
                newbl.append(flatten(None, '((') + getp.decl.ctype + ')' +
                            'read_istruct(&_cur_tc->shareds[%d], prev))' %
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

        if setp.decl.seen_get:  # TODO: do I need casting for b in all the branched below?
            if self.__state == 0: #begin
                newbl.append(flatten(None,#setp.loc,
                             'write_istruct_same_tc(&_cur_tc->shareds[%d],' %
                              param.index) + b + ')')
                
            elif self.__state == 1: #middle
                newbl.append(flatten(None,#setp.loc,
                             'write_istruct_same_tc(&_cur_tc->shareds[%d],' %
                              param.index) + b + ')')
            elif self.__state == 2 or self.__state == 3: #end and generic
                newbl.append(flatten(setp.loc,  # end and generic are passed the array of shareds as an argument
                             'write_istruct(next->node_index, &shareds[%d],' % param.index) \
                             + b + ', next, 0);')
            else:
                assert(0)
        else:  # no need to write to anything; just generate the rhs
            #TODO(kena): check that you're fine with this
            newbl.append(flatten(setp.loc,'(void)(') + b + ');') # cast to void suppress "statement has no effect warning" 

        return newbl

    def visit_funparm(self, parm):
        print 'seeing param ' + parm.name
        print 'type of param is ' + parm.type
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
        print 'seeing memparm ' + parm.name
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

        if not keep:
            newitems = flatten(fundecl.loc, "%s void " % qual) + gen_loop_fun_name(fundecl.name) + "();";
            return newitems
        else:
            return None
        """
        self.__buffer = flatten(fundecl.loc, 
                                " %s long %s(const long __slI" 
                                % (qual, fundecl.name))
        for parm in fundecl.parms:
            parm.accept(self)
        self.__buffer += ')'
        ret = self.__buffer
        self.__buffer = None
        return ret
        """
 
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

    def visit_fundef(self, fundef):
        # make copies of the body for begin, middle and end
        import sys
        print >>sys.stderr, "EXTRAS fundef ", fundef.name, fundef.extras
        n = fundef.extras.get_attr('gencallee', None)
        if n is not None:
            print >>sys.stderr, "got gencallee"

        begin_body = copy.deepcopy(fundef.body)
        middle_body = copy.deepcopy(fundef.body)
        end_body = copy.deepcopy(fundef.body)
        generic_body = copy.deepcopy(fundef.body)
      
        self.dispatch(fundef, seen_as = FunDecl, keep = True, omitextern = True)
 
        newitems = Block()   

        if fundef.name <> "__root_fam":  # for main, we just need the generic variant
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
            newitems += flatten(fundef.loc, ("long %s_end(const tc_ident_t* next, " +
                                "i_struct* shareds __attribute__((unused)), long __index) {\n") % fundef.name)
            newitems += end_body.accept(self)
            newitems += flatten(fundef.loc_end, "return 0; \n}")

        
        self.__state = 3  #generic
        newitems += flatten(fundef.loc, ("long %s_generic(const tc_ident_t* " +
                            "prev __attribute__((unused)), const tc_ident_t* next __attribute__((unused)), " +
                            "i_struct* shareds __attribute__((unused)), long __index __attribute__((unused))) {\n") % fundef.name)
        newitems += generic_body.accept(self)
        newitems += flatten(fundef.loc_end, "return 0; \n}")
       
        # generate loop function

        newitems += flatten(fundef.loc, "void ") + \
                    gen_loop_fun_name(fundef.name) + "(void)" +" {\n"
        newitems += "long __index, __start_index = _get_start_index(), " +\
            "__end_index = _get_end_index();\n"
        newitems += """
            //fam_context_t* fam_context = _get_fam_context();
            const tc_ident_t* parent = _get_parent_ident();
            const tc_ident_t* prev __attribute__((unused)) = _get_prev_ident();
            const tc_ident_t* next = _get_next_ident();
            i_struct* shareds __attribute__((unused)) = _get_final_shareds_pointer();
            i_struct* done = _get_done_pointer();       

            if (__end_index - __start_index > 4) {\n
            """
        if fundef.name <> "__root_fam":  # for main, we won't need this branch (and we can't generate it either
                                     # cause we haven't generated the non-generic flavours of the thread func)
            newitems += fundef.name + """_begin(prev, __start_index);
                for (__index = __start_index + 1; __index < __end_index; ++__index) {
                """ + fundef.name + """_middle(__index); // TODO: check for break return value
                }
            """ \
            + fundef.name + """_end(next, shareds, __end_index);"""
            #+ fundef.name + """_end(next, &fam_context->shareds[0], __end_index);"""
        else:
            newitems += "exit(1);  // main should never be created as a family of more than one thread"

        newitems += """
            } else {
                for (__index = __start_index; __index <= __end_index; ++__index) {
                    /*
                    printf("USER: will call generic version of thread func\\n");
                    const tc_ident_t* p = prev;
                    if (__index > __start_index) {
                        p = &_cur_tc->ident;
                        printf("USER: updated p\\n");
                    }
                    const tc_ident_t* n = next;
                    if (__index < __end_index) {
                        n = &_cur_tc->ident;
                        printf("USER: updated n\\n");
                    }
                    i_struct* s = _cur_tc->shareds;
                    //printf("1: shareds: %p\\n", s);
                    if (_is_last_tc()) {
                        s = shareds;
                        printf("USER: updated shareds to the parent\\n");
                    }
                    else { 
                        if (__index == __end_index) {
                            s = next->tc->shareds;
                            printf("USER: updated shareds to next tc\\n");
                        }
                    }
                    //printf("2: shareds: %p\\n", s);
                    */
                    const tc_ident_t* p, *n;
                    i_struct* s = _cur_tc->shareds;
                    if (__index == __start_index) {
                        p = _get_prev_ident();
                    } else {
                        p = &_cur_tc->ident;
                    }
                    if (__index == __end_index) {
                        n = _get_next_ident();
                        if (_is_last_tc()) {
                            // write to the family context
                            s = _get_final_shareds_pointer();
                        } else {
                            // write to the next tc
                            s = next->tc->shareds;
                        }
                    } else {
                        n = &_cur_tc->ident;
                    }
                    """ + fundef.name + """_generic(p, n, s, __index); // TODO: check for break value
                }

                //for (__index = __start_index; __index <= __end_index; ++__index) {
                //""" + fundef.name + """_generic(prev, next, 
                //        shareds, __index); // TODO: check for break value
                //}
            }
            printf("USER: about to finish loop function. last tc: %d\\t parent node: %d\\t\\n",
                   _is_last_tc(), parent->node_index);
            if (_is_last_tc() && parent->node_index != -1) {
                printf("USER: I am the last thread in a family. Unblocking parent.\\n");
                write_istruct(parent->node_index, done, 1, parent, 0);
            }

            _cur_tc->finished = 1;
        """
        if fundef.name <> '__root_fam':
            newitems += "_return_to_scheduler();\n"
        else:
            newitems += "end_main();"
        newitems += """
        }
        """ 

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
        scope = d.scope
        stub_decl = CVarDecl(loc = d.loc, name = "_" + d.name + "_stub", ctype = "memdesc_stub_t")
        scope.decls += stub_decl
        d.cvar_stub = stub_decl
        d.cvar_desc = None
        # is descriptor needed? it is _not_ needed for sl_getmp 
        # FIXME: figure out when the descriptor is needed
        # and when it's not (is there any situation where it's not?)
        print 'in memdef. type of set_op is %s ' % type(d.set_op)
        
        descriptor_needed = 1
        if isinstance(d.set_op, GetMemP):
            descriptor_needed = 0

        if descriptor_needed:
            print 'inserting descriptor declaration in scope'
            desc_decl = CVarDecl(loc = d.loc, name = "_" + d.name + "_desc", ctype = "memdesc_t")
            scope.decls += desc_decl
            d.cvar_desc = desc_decl
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

    def visit_memrestrict(self, re):
        print 'visiting restrict. lhs is ' + re.lhs
        print 'rhs is ' + re.rhs
        new_items = Block()
        new_items += (flatten(re.loc, "") + CVarUse(decl = re.lhs_decl.cvar_stub) + ' = ' +
                      "_memrestrict(" 
                      + CVarUse(decl = re.rhs_decl.cvar_stub) + ', '         #original stub
                      + '&' + CVarUse(decl = re.lhs_decl.cvar_desc) + ', '   #new descriptor
                      #+ CVarUse(decl = re.rhs_decl.cvar_desc) + '.ranges[0], '  # TODO: remove this argument (first range)
                      + re.offset + ', ' + re.size + ')'
                      )
        return new_items

__all__ = ["Create_2_HydraCall", "TFun_2_HydraCFunctions", "Mem_2_HRT"]


