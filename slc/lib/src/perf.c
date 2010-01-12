//
// perf.c: this file is part of the SL toolchain.
//
// Copyright (C) 2009 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include <svp/perf.h>
#include <svp/testoutput.h>
#include <cmalloc.h>

// the ordering in the following
// table should match the counter
// indices in perf.h.
const char* mtperf_counter_names[] = {
  "clocks",
#ifdef __mt_freestanding__
  "n_exec_insns",
  "n_issued_flops",
  "n_compl_loads",
  "n_compl_stores",
  "n_bytesin_core",
  "n_bytesout_core",
  "n_extmem_cl_in",
  "n_extmem_cl_out"
#endif
};

#define pc(Ch) output_char((Ch), stream)
#define ps(Str) output_string((Str), stream)
#define pn(Num) output_int((Num), stream)
#define pnl  output_char('\n', stream);

#define bfibre(N) do { \
    output_char('[', stream); \
    output_char('1', stream); \
    output_char(',', stream); \
    output_int((N), stream);  \
    output_char(':', stream); \
} while(0)
#define efibre output_char(']', stream)

#define pnlsep pc((fflags & REPORT_NOLF) ? ' ' : '\n');

#ifdef max
#undef max
#endif
#define max(a,b) ((a) > (b) ? (a) : (b))


void mtperf_report_diffs(const counter_t* before, const counter_t* after, int flags)
{
  int i;

  int stream = (flags >> 24) & 0xff;
  if (!stream) stream = 1;
  int format = (flags >> 8) & 0xff;
  int fflags = flags & 0xff;
  switch (format) {
  case 0:
    {
      // output CSV
      int print_headers = fflags & 1;
      int spec_sep = fflags & 2;
      char sep = spec_sep ? ((flags >> 16) & 0xff) : ',';
      if (print_headers) {
	// print column headers
	for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	  if (i) pc(sep);
	  ps(mtperf_counter_names[i]);
	}
	pnl;
      }
      for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	if (i) pc(sep);
	pn(after[i]-before[i]);
      }
      pnlsep;
    }
    break;
  case 1:
    // output raw
    for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
      pn(after[i]-before[i]);
      pnlsep;
    }
    break;
  case 2:
    // output Fibre
    {
      int pad = (flags >> 16) & 0xff;
      bfibre(max(MTPERF_NCOUNTERS, pad));
      for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	pc(' ');
	pn(after[i]-before[i]);
      }
      for ( ; i < pad; ++i) ps(" 0");
      efibre;
      pnlsep;
    }
    break;
  }
}

void mtperf_report_intervals(const struct s_interval* ivs,
			     size_t n,
			     int flags)
{
  int i, j;

  int stream = (flags >> 24) & 0xff;
  if (!stream) stream = 1;
  int format = (flags >> 8) & 0xff;
  int fflags = flags & 0xff;

  switch (format) {
  case 0:
    {
      // output CSV
      int print_headers = fflags & 1;
      int spec_sep = fflags & 2;
      char sep = spec_sep ? ((flags >> 16) & 0xff) : ',';
      if (print_headers) {
	// print column headers
	for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	  if (i) pc(sep);
	  pc('"');
	  ps(mtperf_counter_names[i]);
	  pc('"');
	}
	if (i) pc(sep);
	ps("\"intervals\"");
	pnl;
      }
      for (j = 0; j < n; ++j) {
	for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	  if (i) pc(sep);
	  pn(ivs[j].after[i]-ivs[j].before[i]);
	}
	if (print_headers) {
	  if (i) pc(sep);
	  pc('"');
	  if (ivs[j].num >= 0) { pn(ivs[j].num); pc(' '); }
	  ps(ivs[j].tag ? ivs[j].tag : "(anon)");
	  pc('"');
	}
	pnlsep;
      }
    }
    break;
  case 1:
    // output raw
    ps("# metrics\n");
    for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
      ps(mtperf_counter_names[i]);
      pnlsep;
    }
    for (j = 0; j < n; ++j) {
      ps("# ");
      if (ivs[j].num >= 0) { pn(ivs[j].num); pc(' '); }
      ps(ivs[j].tag ? ivs[j].tag : "(anon)");
      pnl;
      for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	pn(ivs[j].after[i]-ivs[j].before[i]);
	pnlsep;
      }
    }
    break;
  case 2:
    // output Fibre
    {
      int pad = ((flags >> 16) & 0xff);
      int dmax = 0;
      if (pad)
	dmax = max(pad, max(n, MTPERF_NCOUNTERS));

      ps("### begin intervals\n");
      bfibre(n); 
      pnlsep;
      for (j = 0; j < n; ++j) {
	bfibre(MTPERF_NCOUNTERS);
	for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	  pc(' '); 
	  pn(ivs[j].after[i]-ivs[j].before[i]);
	}
	efibre;
	pnlsep;
      }
      efibre;
      pnlsep;
      if (flags & REPORT_NOLF) pnl;
      ps("### end intervals\n### begin descriptions\n");

      bfibre(dmax ? dmax : n);
      for (i = 0; i < n; ++i) {
	pc(' ');
	pc('"');
	if (ivs[i].num >= 0) { pn(ivs[i].num); pc(' '); }
	ps(ivs[i].tag ? ivs[i].tag : "(anon)");
	pc('"');
      }
      for ( ; i < dmax; ++i) ps(" \"\"");
      efibre;
      
      pnlsep;
      
      bfibre(dmax ? dmax : MTPERF_NCOUNTERS);
      for (i = 0; i < MTPERF_NCOUNTERS; ++i) {
	pc(' ');
	pc('"');
	ps(mtperf_counter_names[i]);
	pc('"');
      }
      for ( ; i < dmax; ++i) ps(" \"\"");
      efibre;
      
      ps("\n### end descriptions\n");
    }
    break;
  }
  
}

struct s_interval* mtperf_alloc_intervals(size_t n) 
{ 
  return (struct s_interval*)fast_calloc(n, sizeof(struct s_interval)); 
}

void mtperf_free_intervals(struct s_interval* ivs)
{
  free(ivs);
}

#undef mtperf_start_interval
void mtperf_start_interval(struct s_interval* ivs, size_t p,
			   int numarg,
			   const char *tag)
{
  mtperf_start_interval_(ivs, p, numarg, tag);
}

#undef mtperf_empty_interval
void mtperf_empty_interval(struct s_interval* ivs, size_t p,
			   int numarg,
			   const char *tag)
{
  mtperf_empty_interval_(ivs, p, numarg, tag);
}

#undef mtperf_finish_interval
void mtperf_finish_interval(struct s_interval* ivs, size_t p)
{
  mtperf_finish_interval_(ivs, p);
}


