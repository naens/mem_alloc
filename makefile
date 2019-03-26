all: linux arcaos dos .symbolic

arcaos: mem_test_os2.exe .symbolic
linux: mem_test_ow .symbolic
dos: memtst16.exe mem_test_dos32.exe .symbolic

src=mem_test.c mem.c

# 32-bit Linux
mem_test_ow: $src
	set INCLUDE=$(%WATCOM)/lh
	wcl386 $src -q -fe=$^. -bcl=linux

# 32-bit dos
mem_test_dos32.exe: $src
	set INCLUDE=$(%WATCOM)/h
	wcl386 $src -q -fe=$^. -bt=dos -l=dos4g

# 16-bit dos
memtst16.exe: $src
	set INCLUDE=$(%WATCOM)/h
	wcl $src -q -fe=$^. -bcl=dos

# 32-bit ArcaOS
mem_test_os2.exe: $src
	set INCLUDE=$(%WATCOM)/h:$(%WATCOM)/h/os2
	set LIBPATH=$(%WATCOM)/binp/dll:$(%LIBPATH)
	wcl386 $src -q -fe=$^. -bt=os2 -l=os2v2

clean: .symbolic
	rm -f *.o mem_test_ow *.exe
