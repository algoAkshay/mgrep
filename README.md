# mgrep

Multithreaded file search tool written in C++17. Recursively searches a directory
tree and distributes files across a thread pool sized from the hardware concurrency
hint (four workers if the hint is unavailable).

## Build

```bash
make
```

Requires g++ with C++17 and pthreads.

```bash
make debug    # build with AddressSanitizer
make clean
```

Run the regression suite with `make test` (requires Python 3).

## Usage

```
./mgrep [options] <pattern> <path>

  -i             case insensitive
  -E             extended regex (ECMAScript)
  -c             count matches per file, no line output
  -n             hide line numbers
  --no-color     disable ANSI colors
  --ext=.cpp     filter by extension, can repeat
  --depth=N      max directory recursion depth
```

## Examples

```bash
./mgrep "connect(" ./src --ext=.cpp --ext=.h

./mgrep -i -E "TODO|FIXME" ./src --ext=.cpp

./mgrep -c "malloc" ./src

./mgrep --depth=2 "main" .
```

## How it works

The main thread walks the directory tree and pushes each file as a task into a
work queue. Worker threads pull from the queue using a mutex and condition variable.
Results go into a shared vector (separate mutex) and get sorted by filename after
all workers finish so output is always deterministic regardless of which thread
finished first.

Plain string search is the default. Regex mode uses std::regex
ECMAScript flavor. Binary files get skipped by checking for null bytes in the
first 512 bytes of each file.

## Structure

```
include/
    thread_pool.h   thread pool
    searcher.h      searcher class
    result.h        Match, FileResult, SearchConfig
src/
    main.cpp        CLI, output, timing
    searcher.cpp    directory walk and file search
```

## Performance

Performance depends on the file tree, storage, pattern, and machine. No reproducible
comparison with GNU grep is included in this repository.

## Notes

- Symlinks encountered during directory walks are skipped; a directly supplied file
  symlink is followed
- Extension filtering is case insensitive (.CPP matches .cpp)
- Exit code 0 if matches were found, 1 if not, and 2 for search errors
- Use --no-color when piping output
