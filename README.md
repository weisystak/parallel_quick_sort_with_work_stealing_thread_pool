# parallel_quick_sort_with_work_stealing_thread_pool

## Compile
```bash
g++ -std=c++17 parallel_quicksort.cpp -pthread
```

## run
```bash
./a.out $n
```
note: if n is too large, stackoverflow may occur  because the recursion is too deep.