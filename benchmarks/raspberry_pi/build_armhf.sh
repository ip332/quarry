#!/bin/sh
# Runs inside the cross-build container: /src is read-only, /out is writable.
set -eu
mkdir -p /out/bin /out/lib /out/protobuf
protoc --proto_path=/src/benchmarks/schemas --cpp_out=/out/protobuf /src/benchmarks/schemas/workload.proto
CFLAGS='-static -O3 -DNDEBUG -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard'
arm-linux-gnueabihf-gcc $CFLAGS -std=c99 /src/benchmarks/raspberry_pi/pin_cpu.c -o /out/bin/pin_cpu
arm-linux-gnueabihf-gcc $CFLAGS -std=c99 -I/src/include -I/out/generated \
    /src/benchmarks/c/proof_benchmark.c /src/runtime_c/typed_record.c \
    -o /out/bin/quarry_benchmark_c
arm-linux-gnueabihf-g++ $CFLAGS -std=c++20 -I/src/include -I/out/generated \
    /src/benchmarks/cpp/proof_benchmark.cpp -static-libstdc++ -static-libgcc \
    -o /out/bin/quarry_benchmark_cpp
arm-linux-gnueabihf-gcc $CFLAGS -std=c99 -I/src/include -I/out/generated \
    -c /src/runtime_c/typed_record.c -o /out/typed_record.o
arm-linux-gnueabihf-g++ $CFLAGS -std=c++20 -I/src/include -I/out/wrapper_generated -I/out/generated \
    /src/benchmarks/cpp/c_storage_benchmark.cpp /out/typed_record.o \
    -static-libstdc++ -static-libgcc -o /out/bin/quarry_benchmark_cpp_c_storage
for variant in cpp cpp_arena; do
    arena_flag=
    if [ "$variant" = cpp_arena ]; then arena_flag=-DQUARRY_PROTOBUF_ARENA_DEFAULT=1; fi
    arm-linux-gnueabihf-g++ $CFLAGS -std=c++20 $arena_flag \
        -I/out/protobuf -idirafter /usr/include \
        /src/benchmarks/protobuf/proof_benchmark.cpp /out/protobuf/workload.pb.cc \
        -L/usr/lib/arm-linux-gnueabihf -lprotobuf -lz -latomic -pthread \
        -static-libstdc++ -static-libgcc -o /out/bin/quarry_benchmark_protobuf_$variant
done

arm-linux-gnueabihf-g++ --version > /out/toolchain.txt
protoc --version >> /out/toolchain.txt
printf '%s\n' "$CFLAGS -std=c++20 (C: -std=c99); C++: -static-libstdc++ -static-libgcc" >> /out/toolchain.txt
arm-linux-gnueabihf-readelf -d /out/bin/* > /out/dependencies.txt
