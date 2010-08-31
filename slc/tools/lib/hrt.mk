########## muTC-PTL implementation ##########

nobase_dist_pkgdata_DATA += \
	host-host-hrt/include/sl_hrt.h

EXTRA_DIST += \
	host-host-hrt/tc.c \
	host-host-hrt/network.c \ 
	host-host-hrt/mem-comm.c \
	host-host-hrt/delegate.c

if ENABLE_SLC_HRT

### naked target

nobase_pkglib_DATA += \
	hrt_naked-host-host-hrt/slrt.o \
	hrt_naked-host-host-hrt/network.o \
	hrt_naked-host-host-hrt/mem-comm.o \
	hrt_naked-host-host-hrt/tc.o \
	hrt_naked-host-host-hrt/delegate.o

nobase_pkglib_LIBRARIES += \
	hrt_naked-host-host-hrt/libslc.a \
	hrt_naked-host-host-hrt/libslmain.a 

hrt_naked_host_host_hrt_libslc_a_SOURCES = # empty for now
hrt_naked_host_host_hrt_libslc_a_LIBADD = hrt_naked-host-host-hrt/delegate.o hrt_naked-host-host-hrt/network.o hrt_naked-host-host-hrt/mem-comm.o hrt_naked-host-host-hrt/tc.o hrt_naked-host-host-hrt/bindx.o hrt_naked-host-host-hrt/addrs.o hrt_naked-host-host-hrt/connectx.o  hrt_naked-host-host-hrt/opt_info.o  hrt_naked-host-host-hrt/peeloff.o  hrt_naked-host-host-hrt/recvmsg.o  hrt_naked-host-host-hrt/sendmsg.o
hrt_naked_host_host_hrt_libslmain_a_SOURCES = # empty for now
hrt_naked_host_host_hrt_libslmain_a_LIBADD = hrt_naked-host-host-hrt/main.o
hrt_naked-host-host-hrt/network.o: host-host-hrt/network.c host-host-hrt/include/sl_hrt.h
hrt_naked-host-host-hrt/tc.o: host-host-hrt/tc.c host-host-hrt/include/sl_hrt.h host-host-hrt/mem-comm.h host-host-hrt/network.h host-host-hrt/hrt.h

hrt_naked_host_host_hrt_libslc_a_CPPFLAGS = \
	-I$(srcdir)/host-host-hrt/include \
	-I$(builddir)/host-host-hrt/include

SLC_HRT_N = $(SLC_RUN) -b hrt_n -nostdlib  

# last 2 args for debugging
hrt_naked-host-host-hrt/%.o: $(srcdir)/host-host-hrt/%.c
	$(AM_V_at)$(MKDIR_P) hrt_naked-host-host-hrt
	$(slc_verbose)$(SLC_HRT_N) -c -o $@ $< $(AM_CFLAGS) $(CFLAGS) -O0 -g3

hrt_naked-host-host-hrt/%.o: $(srcdir)/%.c
	$(AM_V_at)$(MKDIR_P) hrt_naked-host-host-hrt
	$(slc_verbose)$(SLC_HRT_N) -c -o $@ $< $(AM_CFLAGS) $(CFLAGS)

CLEANFILES += \
	hrt_naked-host-host-hrt/tc.o \
	hrt_naked-host-host-hrt/network.o \
	hrt_naked-host-host-hrt/mem-comm.o \
	hrt_naked-host-host-hrt/main.o \
	hrt_naked-host-host-hrt/slrt.o

endif

