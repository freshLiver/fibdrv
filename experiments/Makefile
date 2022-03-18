read_perf: force
	$(CC) $(CFLAGS) read_perf.c -o read_perf.out

measure: 
	$(MAKE) -C .. all unload load
	@sudo chmod 666 /dev/fibonacci
	$(MAKE) read_perf
	@sh measure.sh
	$(MAKE) clean

force:


clean:
	rm -f *.out *.o