TESTS = test/index.js
C_SOURCES = $(wildcard src/*.c) $(wildcard src/*.h)


build/Debug/daemonify: build/config.gypi $(C_SOURCES)
	./node_modules/.bin/node-gyp build --debug

build/config.gypi:
	./node_modules/.bin/node-gyp configure --debug

test: build/Debug/daemonify
	@# if any of the files contain 'debugger' statements, start with --debug-brk
	@if find -name 'node_modules' -prune -o -type f -name '*.js' -print | xargs grep -q '^\s*debugger'; then \
		./node_modules/.bin/tape --debug-brk $(TESTS); \
		else \
		./node_modules/.bin/tape $(TESTS); \
		fi

clean:
	rm -rf build

.PHONY: test clean
