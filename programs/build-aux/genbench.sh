#! /bin/bash
set -e
srcdir=$1
top_builddir=$2
binlist=($BENCH_BINFORMATS)
plist=($PLIST)
fdatas=()
pdatas=()
results=()
easydatas=()
shift
shift
echo "## This file is automatically generated. Do not edit!"
cat <<EOF
AM_V_GEN = \$(am__v_GEN_\$(V))
am__v_GEN_ = \$(am__v_GEN_\$(AM_DEFAULT_VERBOSITY))
am__v_GEN_0 = @echo "  GEN   " \$@;
EOF
for bench_src in "$@"; do
   bench_base=${bench_src%.c}
   bench_src=$(test -r "$srcdir/$bench_src" && echo "$srcdir" || echo .)/$bench_src
   ispec_src=$(test -r "$srcdir/$bench_base.inputs" && echo "$srcdir" || echo .)/$bench_base.inputs
   bench_inputs=($(
       if test -r "$ispec_src"; then
	   (while read pat; do
	       for f in "$srcdir"/$pat.d*[0-9] ./$pat.d*[0-9]; do
		   if test -r "$f"; then echo "$f"; fi
	       done; done) <"$ispec_src"
       else 
	   for f in "$srcdir/$bench_base".d*[0-9] ./"$bench_base".d*[0-9]; do
	       if test -r "$f"; then echo "$f"; fi
	   done
       fi | sort | uniq))
   if test 0 = ${#bench_inputs[*]}; then
       echo "$bench_base: no inputs defined" >&2
       continue
   fi
   for b in "${binlist[@]}"; do
       pdatas_here=()
       for i in "${bench_inputs[@]}"; do
	   ibase=${i##*/}
	   fdata="benchdata/$b-$ibase.fdata"
           easy=
	   if grep -q "USE IN MAKE CHECK" "$i" >/dev/null 2>&1; then
               easy=1
           fi
	   # is it already there?
	   found=
	   for search in "${fdatas[@]}"; do [ "$fdata" = "$search" ] && found=1; done
	   if test -z "$found"; then
	       echo "$fdata: $bench_base.x $i ;" \
		   "\$(AM_V_GEN)mkdir -p benchdata && " \
		   "\$(SLR) $bench_base.bin.$b -f $i -wf \$@ -wo 2>/dev/null"
	       fdatas+=("$fdata")
	   fi
	   for p in "${plist[@]}"; do
               if test "$p" != "${plist[0]}" && ! expr "$b" : "mt" >/dev/null 2>&1; then
                   continue
               fi
	       pdata="benchdata/$b-$bench_base-$ibase.p$p.out"
	       echo "$pdata: $bench_base.x $fdata $i ;" \
		   "\$(AM_V_GEN)echo \"## program=$bench_base ncores=$p input=$ibase\" >\$@.err && " \
		   "\$(SLR) -t $bench_base.bin.$b -rf $fdata L= sep_dump= results= format=1 ncores=$p >>\$@.err 2>&1 && " \
		   "mv -f \$@.err \$@"
	       pdatas_here+=("$pdata")
	       pdatas+=("$pdata")
               if test "$p" = "${plist[0]}" -a -n "$easy"; then
                   edata="benchdata/$b-$bench_base-$ibase.check.out"
                   echo "$edata: $bench_base.x $fdata $i ;" \
                       "\$(AM_V_GEN)echo \"## CHECK: program=$bench_base input=$ibase, with output, csv format\" >\$@.err && " \
                       "\$(SLR) -t $bench_base.bin.$b -rf $fdata L=1 sep_dump=1 ncores=1 format=1 results=1 >>\$@.err 2>&1 && " \
                       "echo \"## CHECK: program=$bench_base input=$ibase, fibre format\" >>\$@.err && " \
                       "\$(SLR) -t $bench_base.bin.$b -rf $fdata L=1 ncores=1 format= results= >>\$@.err 2>&1 && " \
                       "mv -f \$@.err \$@"
                   easydatas+=("$edata")
               fi
	   done
       done
       rdata="$bench_base.$b.out"
       echo "$rdata: ${pdatas_here[*]}; \$(AM_V_GEN)cat \$^ >\$@"
       results+=("$rdata")
   done
done
echo ".PHONY: clean fdata results check clean-check recheck"
echo "fdata: ${fdatas[*]}"
echo "results: ${results[*]}"
echo "clean:"
for f in "${fdatas[@]}" "${pdatas[@]}" "${easydatas[@]}" "${results[@]}"; do
    printf '\t@rm -f %s\n' "$f"
done
echo "clean-check:"
for f in "${easydatas[@]}"; do
    printf '\t@rm -f %s\n' "$f"
done
echo "check: clean-check ; \$(MAKE) recheck"
echo "recheck: ${easydatas[*]}"
