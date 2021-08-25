# parallel_quick_sort_with_work_stealing_thread_pool

## Compile
```bash
g++ -std=c++20 parallel_quicksort.cpp -pthread
```

## Run
```bash
./a.out $n
```
note: if n is too large, stackoverflow may occur  because the recursion is too deep. Set the stack size to unlimited: `ulimit -s unlimited`。

## Time
Dual Intel(R) Xeon(R) CPU E5-2623 v4 @ 2.60GHz 
(total: 2 sockets * 4 cores = 8 cores)

1 thread:
```log
$ time ./a.out 3000000
0
0
1
2
2
2
3
3
3

real    0m12.414s
user    0m20.669s
sys     0m2.722s
```
16 threads:
```log
$ time ./a.out 3000000
0
0
0
1
2
2
2
3
3
3

real    0m2.506s
user    0m27.119s
sys     0m2.664s
```

4.95 speedup