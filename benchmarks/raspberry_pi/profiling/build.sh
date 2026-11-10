#!/bin/sh
set -eu
mkdir -p /out/profile
FLAGS='-static -O3 -g1 -DNDEBUG -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard'
arm-linux-gnueabihf-gcc $FLAGS -std=c11 /src/benchmarks/raspberry_pi/profiling/sample.c -latomic -o /out/profile/sample
arm-linux-gnueabihf-gcc $FLAGS -std=c99 -I/src/include -I/out/generated -c /src/runtime_c/typed_record.c -o /out/profile/typed_record.o
arm-linux-gnueabihf-g++ $FLAGS -std=c++20 -I/src/include -I/out/wrapper_generated -I/out/generated \
  /src/benchmarks/cpp/c_storage_benchmark.cpp /out/profile/typed_record.o \
  -static-libstdc++ -static-libgcc -o /out/profile/quarry_benchmark_cpp_c_storage
arm-linux-gnueabihf-g++ $FLAGS -std=c++20 -I/out/protobuf -idirafter /usr/include \
  /src/benchmarks/protobuf/proof_benchmark.cpp /out/protobuf/workload.pb.cc \
  -L/usr/lib/arm-linux-gnueabihf -lprotobuf -lz -latomic -pthread \
  -static-libstdc++ -static-libgcc -o /out/profile/quarry_benchmark_protobuf_cpp
for name in quarry_benchmark_cpp_c_storage quarry_benchmark_protobuf_cpp; do
  arm-linux-gnueabihf-objcopy -O binary --only-section=.text /out/bin/$name /out/profile/$name.baseline.text
  arm-linux-gnueabihf-objcopy -O binary --only-section=.text /out/profile/$name /out/profile/$name.profile.text
done
