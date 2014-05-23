all: clean
	gcc merger.c -o merger -g

.SILENT clean:
	rm -rf merger
