.PHONY: clean librocksdb

PLATFORM_LDFLAGS  = -lrt -lpthread -lm -lnuma -ldl -lconfig ../deps/rocksdb/librocksdb.a -lgflags -lsnappy -lz -llz4

all: create_db

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@ -I../deps/rocksdb/include

create_db: librocksdb create_db.o
	$(CXX) $@.o -o$@ ../deps/rocksdb/librocksdb.a $(PLATFORM_LDFLAGS) $(EXEC_LDFLAGS)

clean:
	rm -rf create_db create_db.o

librocksdb:
	cd ../deps/rocksdb && $(MAKE) static_lib
