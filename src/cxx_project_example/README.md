# C++23 Hello World

Esse projeto feito pelo IA Qwen3.8-27B.

## Build with Ninja
```
$ cmake -GNinja -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S .
$ ninja -C build -t clean
$ ninja -v -C build/
```

## Run
```
$ ./build/hello
Hello, world!
```
