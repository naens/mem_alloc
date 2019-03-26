mem_test_ow:
	wcl386 mem_test.c mem.c -fe=mem_test_ow

clean: .symbolic
	rm -f *.o mem_test_ow
